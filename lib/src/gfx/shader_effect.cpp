#include "engine/gfx/shader_effect.hpp"

#include <SFML/Graphics/Glsl.hpp>
#include <SFML/Graphics/Shader.hpp>

#include <type_traits>

namespace pac::gfx {

void apply_shader_params(sf::Shader& shader, const std::vector<ShaderParam>& params) {
    for (const ShaderParam& p : params) {
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    shader.setUniform(p.name, v);
                } else if constexpr (std::is_same_v<T, int>) {
                    shader.setUniform(p.name, v);
                } else if constexpr (std::is_same_v<T, float>) {
                    shader.setUniform(p.name, v);
                } else if constexpr (std::is_same_v<T, std::array<float, 2>>) {
                    shader.setUniform(p.name, sf::Glsl::Vec2(v[0], v[1]));
                } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                    shader.setUniform(p.name, sf::Glsl::Vec3(v[0], v[1], v[2]));
                } else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                    shader.setUniform(p.name, sf::Glsl::Vec4(v[0], v[1], v[2], v[3]));
                }
            },
            p.value);
    }
}

} // namespace pac::gfx
