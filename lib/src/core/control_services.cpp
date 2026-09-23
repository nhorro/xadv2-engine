#include "control_services.hpp"

#include "engine/core/scene_manager.hpp"
#include "engine/core/scripting.hpp"

#include <sol/sol.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace pac::core {

namespace {

ControlError invalid(std::string message) { return {-32602, std::move(message), std::nullopt}; }
ControlError lua_error(std::string message) { return {-32010, std::move(message), std::nullopt}; }

const ControlValue::Object* object_params(const ControlValue& params) { return params.object(); }

const ControlValue* field(const ControlValue::Object& object, const char* name) {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

const std::string* string_field(const ControlValue::Object& object, const char* name) {
    const ControlValue* value = field(object, name);
    return value ? std::get_if<std::string>(&value->value) : nullptr;
}

std::optional<std::int64_t> integer_field(const ControlValue::Object& object, const char* name) {
    const ControlValue* value = field(object, name);
    if (!value) return std::nullopt;
    if (const auto* integer = std::get_if<std::int64_t>(&value->value)) return *integer;
    return std::nullopt;
}

sol::object control_to_lua(sol::state_view lua, const ControlValue& value, int depth) {
    if (depth > 32) throw std::runtime_error("control value nesting is too deep");
    return std::visit(
        [&](const auto& item) -> sol::object {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return sol::make_object(lua, sol::lua_nil);
            } else if constexpr (std::is_same_v<T, ControlValue::Array>) {
                sol::table table = lua.create_table(static_cast<int>(item.size()), 0);
                for (std::size_t i = 0; i < item.size(); ++i) {
                    table[i + 1] = control_to_lua(lua, item[i], depth + 1);
                }
                return sol::make_object(lua, table);
            } else if constexpr (std::is_same_v<T, ControlValue::Object>) {
                sol::table table = lua.create_table(0, static_cast<int>(item.size()));
                for (const auto& [key, child] : item) {
                    table[key] = control_to_lua(lua, child, depth + 1);
                }
                return sol::make_object(lua, table);
            } else {
                return sol::make_object(lua, item);
            }
        },
        value.value);
}

ControlResult lua_to_control(const sol::object& value, int depth = 0) {
    if (depth > 24) return lua_error("Lua result nesting is too deep or cyclic");
    switch (value.get_type()) {
    case sol::type::none:
    case sol::type::lua_nil: return ControlValue(nullptr);
    case sol::type::boolean: return ControlValue(value.as<bool>());
    case sol::type::number: {
        const double number = value.as<double>();
        if (!std::isfinite(number)) return lua_error("Lua returned a non-finite number");
        const double rounded = std::round(number);
        if (rounded == number && rounded >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
            rounded <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return ControlValue(static_cast<std::int64_t>(rounded));
        }
        return ControlValue(number);
    }
    case sol::type::string: return ControlValue(value.as<std::string>());
    case sol::type::table: {
        const sol::table table = value.as<sol::table>();
        std::size_t count = 0;
        std::size_t largest = 0;
        bool array = true;
        for (const auto& pair : table) {
            ++count;
            const sol::object& key = pair.first;
            if (key.get_type() != sol::type::number) {
                array = false;
                continue;
            }
            const double raw = key.as<double>();
            if (raw < 1.0 || std::floor(raw) != raw) {
                array = false;
                continue;
            }
            largest = std::max(largest, static_cast<std::size_t>(raw));
        }
        if (array && largest == count) {
            ControlValue::Array result;
            result.reserve(count);
            for (std::size_t i = 1; i <= count; ++i) {
                ControlResult child = lua_to_control(table.get<sol::object>(i), depth + 1);
                if (std::holds_alternative<ControlError>(child)) return child;
                result.push_back(std::move(std::get<ControlValue>(child)));
            }
            return ControlValue(std::move(result));
        }
        ControlValue::Object result;
        for (const auto& pair : table) {
            const sol::object& key = pair.first;
            if (key.get_type() != sol::type::string) {
                return lua_error("Lua result tables must be arrays or have only string keys");
            }
            ControlResult child = lua_to_control(pair.second, depth + 1);
            if (std::holds_alternative<ControlError>(child)) return child;
            result[key.as<std::string>()] = std::move(std::get<ControlValue>(child));
        }
        return ControlValue(std::move(result));
    }
    default: return lua_error("Lua result is not JSON-serializable");
    }
}

ControlResult protected_results(const sol::protected_function_result& result) {
    if (!result.valid()) {
        const sol::error error = result;
        return lua_error(error.what());
    }
    if (result.return_count() == 0) return ControlValue(nullptr);
    if (result.return_count() == 1) return lua_to_control(sol::object(result[0]));
    ControlValue::Array values;
    values.reserve(static_cast<std::size_t>(result.return_count()));
    for (int i = 0; i < result.return_count(); ++i) {
        ControlResult value = lua_to_control(sol::object(result[i]));
        if (std::holds_alternative<ControlError>(value)) return value;
        values.push_back(std::move(std::get<ControlValue>(value)));
    }
    return ControlValue(std::move(values));
}

sol::object resolve_lua_name(sol::state_view lua, const std::string& name) {
    sol::object current = lua.globals();
    std::size_t begin = 0;
    while (begin < name.size()) {
        const std::size_t dot = name.find('.', begin);
        const std::string part = name.substr(begin, dot == std::string::npos ? dot : dot - begin);
        if (part.empty() || current.get_type() != sol::type::table) return sol::object{};
        current = current.as<sol::table>().get<sol::object>(part);
        if (!current.valid() || current == sol::lua_nil) return sol::object{};
        if (dot == std::string::npos) break;
        begin = dot + 1;
    }
    return current;
}

} // namespace

EngineControlService::EngineControlService(ControlRouter& router, SceneManager& scenes)
    : router_(router), scenes_(scenes) {}

std::vector<ControlMethod> EngineControlService::methods() const {
    return {{"describe", "List active control services and methods."},
            {"state", "Return the active scene and application state."}};
}

ControlResult EngineControlService::invoke(std::string_view method, const ControlValue&) {
    if (method == "describe") return router_.describe();
    if (method == "state") {
        return ControlValue::Object{{"scene", scenes_.current_scene_id()},
                                    {"running", scenes_.running()},
                                    {"transitioning", scenes_.transitioning()}};
    }
    return ControlError{-32601, "unknown engine method: " + std::string(method), std::nullopt};
}

LuaControlService::LuaControlService(Scripting& scripting) : scripting_(scripting) {}

std::vector<ControlMethod> LuaControlService::methods() const {
    return {{"call", "Call a non-yielding named Lua function."},
            {"eval", "Evaluate a non-yielding Lua chunk."},
            {"spawn", "Schedule a yielding Lua chunk and return its task id."},
            {"task_status", "Return whether a Lua task is still active."}};
}

ControlResult LuaControlService::invoke(std::string_view method, const ControlValue& params) {
    const auto* object = object_params(params);
    if (!object) return invalid("params must be an object");
    sol::state_view lua(scripting_.lua());

    if (method == "eval") {
        const std::string* code = string_field(*object, "code");
        if (!code) return invalid("lua.eval requires string param 'code'");
        sol::load_result loaded = lua.load(*code, "@control/lua.eval");
        if (!loaded.valid()) {
            const sol::error error = loaded;
            return lua_error(error.what());
        }
        return protected_results(sol::protected_function(loaded)());
    }

    if (method == "call") {
        const std::string* name = string_field(*object, "function");
        if (!name) return invalid("lua.call requires string param 'function'");
        sol::object target = resolve_lua_name(lua, *name);
        if (!target.valid() || target.get_type() != sol::type::function) {
            return lua_error("Lua function not found: " + *name);
        }
        std::vector<sol::object> arguments;
        if (const ControlValue* args = field(*object, "args")) {
            const auto* array = args->array();
            if (!array) return invalid("lua.call param 'args' must be an array");
            arguments.reserve(array->size());
            for (const ControlValue& argument : *array) {
                arguments.push_back(control_to_lua(lua, argument, 0));
            }
        }
        sol::protected_function function = target.as<sol::protected_function>();
        return protected_results(function(sol::as_args(arguments)));
    }

    if (method == "spawn") {
        const std::string* code = string_field(*object, "code");
        if (!code) return invalid("lua.spawn requires string param 'code'");
        sol::load_result loaded = lua.load(*code, "@control/lua.spawn");
        if (!loaded.valid()) {
            const sol::error error = loaded;
            return lua_error(error.what());
        }
        sol::protected_function spawn = lua["spawn"];
        const ScopeId previous_scope = scripting_.current_scope();
        scripting_.set_current_scope(scripting_.global_scope());
        const sol::protected_function_result result = spawn(sol::function(loaded));
        scripting_.set_current_scope(previous_scope);
        if (!result.valid()) {
            const sol::error error = result;
            return lua_error(error.what());
        }
        return ControlValue::Object{{"task_id", static_cast<std::int64_t>(result.get<TaskId>())}};
    }

    if (method == "task_status") {
        const auto task_id = integer_field(*object, "task_id");
        if (!task_id || *task_id <= 0) return invalid("lua.task_status requires positive 'task_id'");
        return ControlValue::Object{{"task_id", *task_id},
                                    {"active", scripting_.is_task_alive(
                                                   static_cast<TaskId>(*task_id))}};
    }

    return ControlError{-32601, "unknown lua method: " + std::string(method), std::nullopt};
}

} // namespace pac::core
