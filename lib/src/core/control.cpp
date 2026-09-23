#include "engine/core/control.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pac::core {

namespace {

ControlError error(int code, std::string message) {
    return {code, std::move(message), std::nullopt};
}

bool valid_service_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    ControlResult parse() {
        skip_space();
        ControlResult result = parse_value(0);
        if (std::holds_alternative<ControlError>(result)) {
            return result;
        }
        skip_space();
        if (position_ != input_.size()) {
            return error(-32700, "unexpected characters after JSON value");
        }
        return result;
    }

private:
    ControlResult parse_value(int depth) {
        if (depth > 64) {
            return error(-32700, "JSON nesting is too deep");
        }
        skip_space();
        if (position_ >= input_.size()) {
            return error(-32700, "unexpected end of JSON");
        }
        const char c = input_[position_];
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"') return parse_string();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        if (take("true")) return ControlValue(true);
        if (take("false")) return ControlValue(false);
        if (take("null")) return ControlValue(nullptr);
        return error(-32700, "invalid JSON value");
    }

    ControlResult parse_object(int depth) {
        ++position_;
        ControlValue::Object object;
        skip_space();
        if (consume('}')) return ControlValue(std::move(object));
        while (true) {
            skip_space();
            ControlResult key_result = parse_string();
            if (auto* parse_error = std::get_if<ControlError>(&key_result)) return *parse_error;
            const auto* key = std::get_if<std::string>(&std::get<ControlValue>(key_result).value);
            skip_space();
            if (!consume(':')) return error(-32700, "expected ':' after object key");
            ControlResult item = parse_value(depth);
            if (auto* parse_error = std::get_if<ControlError>(&item)) return *parse_error;
            object[*key] = std::move(std::get<ControlValue>(item));
            skip_space();
            if (consume('}')) break;
            if (!consume(',')) return error(-32700, "expected ',' or '}' in object");
        }
        return ControlValue(std::move(object));
    }

    ControlResult parse_array(int depth) {
        ++position_;
        ControlValue::Array array;
        skip_space();
        if (consume(']')) return ControlValue(std::move(array));
        while (true) {
            ControlResult item = parse_value(depth);
            if (auto* parse_error = std::get_if<ControlError>(&item)) return *parse_error;
            array.push_back(std::move(std::get<ControlValue>(item)));
            skip_space();
            if (consume(']')) break;
            if (!consume(',')) return error(-32700, "expected ',' or ']' in array");
        }
        return ControlValue(std::move(array));
    }

    static void append_utf8(std::string& out, unsigned codepoint) {
        if (codepoint <= 0x7f) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    std::optional<unsigned> hex4() {
        if (position_ + 4 > input_.size()) return std::nullopt;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value += static_cast<unsigned>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    ControlResult parse_string() {
        if (!consume('"')) return error(-32700, "expected JSON string");
        std::string out;
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return ControlValue(std::move(out));
            if (c < 0x20) return error(-32700, "control character in JSON string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (position_ >= input_.size()) return error(-32700, "unfinished JSON escape");
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                const auto first = hex4();
                if (!first) return error(-32700, "invalid Unicode escape");
                unsigned codepoint = *first;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1] != 'u') {
                        return error(-32700, "missing low Unicode surrogate");
                    }
                    position_ += 2;
                    const auto second = hex4();
                    if (!second || *second < 0xdc00 || *second > 0xdfff) {
                        return error(-32700, "invalid low Unicode surrogate");
                    }
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*second - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return error(-32700, "unexpected low Unicode surrogate");
                }
                append_utf8(out, codepoint);
                break;
            }
            default: return error(-32700, "invalid JSON escape");
            }
        }
        return error(-32700, "unterminated JSON string");
    }

    ControlResult parse_number() {
        const std::size_t start = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return error(-32700, "invalid JSON number");
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (input_[position_] < '1' || input_[position_] > '9')
                return error(-32700, "invalid JSON number");
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
        }
        bool floating = false;
        if (position_ < input_.size() && input_[position_] == '.') {
            floating = true;
            ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (digits == position_) return error(-32700, "invalid JSON fraction");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') ++position_;
            if (digits == position_) return error(-32700, "invalid JSON exponent");
        }
        const std::string text(input_.substr(start, position_ - start));
        if (!floating) {
            std::int64_t integer = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), integer);
            if (result.ec == std::errc{}) return ControlValue(integer);
        }
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(number)) {
            return error(-32700, "JSON number is out of range");
        }
        return ControlValue(number);
    }

    void skip_space() {
        while (position_ < input_.size()) {
            const char c = input_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool take(std::string_view text) {
        if (input_.substr(position_, text.size()) != text) return false;
        position_ += text.size();
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

void write_json_string(std::string_view input, std::string& out) {
    out.push_back('"');
    for (const unsigned char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

void write_json(const ControlValue& input, std::string& out) {
    std::visit(
        [&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                out += std::to_string(item);
            } else if constexpr (std::is_same_v<T, double>) {
                if (!std::isfinite(item)) {
                    out += "null";
                } else {
                    char buffer[64];
                    const auto result = std::to_chars(buffer,
                                                      buffer + sizeof(buffer),
                                                      item,
                                                      std::chars_format::general,
                                                      std::numeric_limits<double>::max_digits10);
                    out.append(buffer, result.ptr);
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                write_json_string(item, out);
            } else if constexpr (std::is_same_v<T, ControlValue::Array>) {
                out.push_back('[');
                bool first = true;
                for (const auto& child : item) {
                    if (!first) out.push_back(',');
                    first = false;
                    write_json(child, out);
                }
                out.push_back(']');
            } else {
                out.push_back('{');
                bool first = true;
                for (const auto& [key, child] : item) {
                    if (!first) out.push_back(',');
                    first = false;
                    write_json_string(key, out);
                    out.push_back(':');
                    write_json(child, out);
                }
                out.push_back('}');
            }
        },
        input.value);
}

ControlValue rpc_error_value(const ControlValue& id, const ControlError& item) {
    ControlValue::Object body{{"code", item.code}, {"message", item.message}};
    if (item.data) body["data"] = *item.data;
    return ControlValue::Object{{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(body)}};
}

} // namespace

ControlRegistration::ControlRegistration(ControlRouter& router,
                                         std::string service,
                                         std::uint64_t generation)
    : router_(&router), service_(std::move(service)), generation_(generation) {}

ControlRegistration::~ControlRegistration() { reset(); }

ControlRegistration::ControlRegistration(ControlRegistration&& other) noexcept
    : router_(std::exchange(other.router_, nullptr)),
      service_(std::move(other.service_)),
      generation_(std::exchange(other.generation_, 0)) {}

ControlRegistration& ControlRegistration::operator=(ControlRegistration&& other) noexcept {
    if (this != &other) {
        reset();
        router_ = std::exchange(other.router_, nullptr);
        service_ = std::move(other.service_);
        generation_ = std::exchange(other.generation_, 0);
    }
    return *this;
}

void ControlRegistration::reset() {
    if (router_) router_->unregister_service(service_, generation_);
    router_ = nullptr;
    service_.clear();
    generation_ = 0;
}

ControlRegistration ControlRouter::register_service(std::string name, ControlService& service) {
    if (!valid_service_name(name)) {
        throw std::invalid_argument("invalid control service name: '" + name + "'");
    }
    if (services_.count(name) != 0) {
        throw std::invalid_argument("control service already registered: '" + name + "'");
    }
    const std::uint64_t generation = next_generation_++;
    services_[name] = {&service, generation};
    return ControlRegistration(*this, std::move(name), generation);
}

ControlResult ControlRouter::dispatch(std::string_view method, const ControlValue& params) {
    const std::size_t separator = method.find('.');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= method.size()) {
        return error(-32601, "method not found: " + std::string(method));
    }
    const auto found = services_.find(std::string(method.substr(0, separator)));
    if (found == services_.end() || !found->second.service) {
        return error(-32601, "method not found: " + std::string(method));
    }
    try {
        return found->second.service->invoke(method.substr(separator + 1), params);
    } catch (const std::exception& exception) {
        return error(-32603, exception.what());
    } catch (...) {
        return error(-32603, "internal control service error");
    }
}

ControlValue ControlRouter::describe() const {
    ControlValue::Object services;
    for (const auto& [name, entry] : services_) {
        ControlValue::Array methods;
        if (entry.service) {
            for (const ControlMethod& method : entry.service->methods()) {
                methods.emplace_back(ControlValue::Object{{"name", method.name},
                                                          {"summary", method.summary}});
            }
        }
        services[name] = std::move(methods);
    }
    return ControlValue::Object{{"services", std::move(services)}};
}

void ControlRouter::unregister_service(const std::string& name, std::uint64_t generation) {
    const auto found = services_.find(name);
    if (found != services_.end() && found->second.generation == generation) services_.erase(found);
}

ControlResult parse_control_json(std::string_view json) { return JsonParser(json).parse(); }

std::string write_control_json(const ControlValue& value) {
    std::string out;
    write_json(value, out);
    return out;
}

std::optional<std::string> dispatch_control_json_rpc(ControlRouter& router,
                                                     std::string_view request) {
    ControlResult parsed = parse_control_json(request);
    if (auto* parse_error = std::get_if<ControlError>(&parsed)) {
        return write_control_json(rpc_error_value(nullptr, *parse_error));
    }
    const ControlValue& root = std::get<ControlValue>(parsed);
    const auto* object = root.object();
    if (!object) {
        return write_control_json(rpc_error_value(nullptr, error(-32600, "invalid request")));
    }
    const auto version = object->find("jsonrpc");
    const auto method = object->find("method");
    const bool valid_version =
        version != object->end() && std::get_if<std::string>(&version->second.value) &&
        *std::get_if<std::string>(&version->second.value) == "2.0";
    const auto* method_name =
        method == object->end() ? nullptr : std::get_if<std::string>(&method->second.value);
    const auto id = object->find("id");
    const bool notification = id == object->end();
    const ControlValue response_id = notification ? ControlValue(nullptr) : id->second;
    const bool valid_id = notification || std::holds_alternative<std::nullptr_t>(id->second.value) ||
                          std::holds_alternative<std::string>(id->second.value) ||
                          std::holds_alternative<std::int64_t>(id->second.value) ||
                          std::holds_alternative<double>(id->second.value);
    if (!valid_version || !method_name || !valid_id) {
        if (notification) return std::nullopt;
        return write_control_json(rpc_error_value(response_id, error(-32600, "invalid request")));
    }
    ControlValue params(ControlValue::Object{});
    if (const auto found = object->find("params"); found != object->end()) {
        if (!found->second.is_object() && !found->second.is_array()) {
            if (notification) return std::nullopt;
            return write_control_json(
                rpc_error_value(response_id, error(-32602, "params must be an object or array")));
        }
        params = found->second;
    }
    ControlResult result = router.dispatch(*method_name, params);
    if (notification) return std::nullopt;
    if (auto* dispatch_error = std::get_if<ControlError>(&result)) {
        return write_control_json(rpc_error_value(response_id, *dispatch_error));
    }
    return write_control_json(ControlValue::Object{{"jsonrpc", "2.0"},
                                                   {"id", response_id},
                                                   {"result", std::get<ControlValue>(result)}});
}

} // namespace pac::core
