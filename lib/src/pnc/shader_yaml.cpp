#include "shader_yaml.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/pnc/data_error.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <utility>

namespace pac::pnc::detail {

namespace {

[[noreturn]] void fail(const char* source,
                       const std::string& code,
                       const std::string& msg,
                       const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<DataError>(source, code, msg, at);
}

gfx::ShaderValue
parse_vec(const YAML::Node& seq, const char* source, const std::string& code_prefix) {
    switch (seq.size()) {
    case 2:
        return std::array<float, 2>{seq[0].as<float>(), seq[1].as<float>()};
    case 3:
        return std::array<float, 3>{seq[0].as<float>(), seq[1].as<float>(), seq[2].as<float>()};
    case 4:
        return std::array<float, 4>{seq[0].as<float>(),
                                    seq[1].as<float>(),
                                    seq[2].as<float>(),
                                    seq[3].as<float>()};
    default:
        fail(source,
             code_prefix + ".shader-param-vec-size",
             "a shader vector param must have 2, 3, or 4 numbers",
             seq);
    }
}

gfx::ShaderValue
parse_value(const YAML::Node& node, const char* source, const std::string& code_prefix) {
    if (node.IsSequence()) {
        return parse_vec(node, source, code_prefix);
    }
    if (node.IsMap()) {
        if (!node["type"] || !node["value"]) {
            fail(source,
                 code_prefix + ".shader-param-form",
                 "a shader param mapping needs 'type' and 'value'",
                 node);
        }
        const std::string type = node["type"].as<std::string>();
        const YAML::Node value = node["value"];
        if (type == "bool") {
            return value.as<bool>();
        }
        if (type == "int") {
            return value.as<int>();
        }
        if (type == "float") {
            return value.as<float>();
        }
        if (type == "vec2" || type == "vec3" || type == "vec4") {
            return parse_vec(value, source, code_prefix);
        }
        fail(source,
             code_prefix + ".shader-param-type",
             "unknown shader param type '" + type + "' (want bool/int/float/vec2/vec3/vec4)",
             node["type"]);
    }
    const std::string scalar = node.Scalar();
    if (scalar == "true") {
        return true;
    }
    if (scalar == "false") {
        return false;
    }
    return node.as<float>();
}

gfx::ShaderEffect
parse_one(const YAML::Node& node, const char* source, const std::string& code_prefix) {
    gfx::ShaderEffect fx;
    if (node.IsScalar()) {
        fx.source = node.as<std::string>();
        return fx;
    }
    if (!node["source"]) {
        fail(source, code_prefix + ".shader-source-missing", "a shader needs a 'source'", node);
    }
    fx.source = node["source"].as<std::string>();
    if (node["controller"]) {
        fx.controller = node["controller"].as<std::string>();
    }
    if (node["enabled"]) {
        fx.enabled = node["enabled"].as<bool>();
    }
    if (const YAML::Node params = node["params"]) {
        for (const auto& kv : params) {
            gfx::ShaderParam p;
            p.name = kv.first.as<std::string>();
            p.value = parse_value(kv.second, source, code_prefix);
            fx.params.push_back(std::move(p));
        }
    }
    return fx;
}

} // namespace

std::vector<gfx::ShaderEffect>
parse_shaders(const YAML::Node& owner, const char* source, const std::string& code_prefix) {
    std::vector<gfx::ShaderEffect> out;
    if (const YAML::Node one = owner["shader"]) {
        out.push_back(parse_one(one, source, code_prefix));
    }
    if (const YAML::Node list = owner["shaders"]) {
        for (const YAML::Node& e : list) {
            out.push_back(parse_one(e, source, code_prefix));
        }
    }
    return out;
}

} // namespace pac::pnc::detail
