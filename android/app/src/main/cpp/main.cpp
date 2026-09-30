#include "engine/core/game.hpp"
#include "engine/core/resource_source.hpp"

#include <SFML/System/FileInputStream.hpp>

#include <android/log.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* kLogTag = "xadv2-android";

class AndroidAssetResourceSource final : public pac::core::ResourceSource {
public:
    bool exists(const std::string& logical) const override {
        if (!pac::core::is_valid_logical_path(logical)) {
            return false;
        }
        sf::FileInputStream stream;
        return stream.open(logical);
    }

    std::string read_text(const std::string& logical) const override {
        const std::vector<std::byte> bytes = read_bytes(logical);
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    std::vector<std::byte> read_bytes(const std::string& logical) const override {
        if (!pac::core::is_valid_logical_path(logical)) {
            throw pac::core::ResourceError("invalid Android asset path '" + logical + "'");
        }

        sf::FileInputStream stream;
        if (!stream.open(logical)) {
            throw pac::core::ResourceError("cannot read Android asset '" + logical + "'");
        }
        const std::optional<std::size_t> size = stream.getSize();
        if (!size) {
            throw pac::core::ResourceError("cannot determine Android asset size for '" + logical +
                                           "'");
        }

        std::vector<std::byte> bytes(*size);
        if (*size > 0) {
            const std::optional<std::size_t> read = stream.read(bytes.data(), *size);
            if (!read || *read != *size) {
                throw pac::core::ResourceError("short read from Android asset '" + logical + "'");
            }
        }
        return bytes;
    }
};

} // namespace

int main(int argc, char** argv) {
    (void) argc;
    (void) argv;

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "starting normal game.yaml bootstrap");
    AndroidAssetResourceSource resources;
    std::unique_ptr<pac::core::Game> game = pac::game::create();
    const int result = pac::core::run_game_from_resources(resources, "game.yaml", *game);
    __android_log_print(ANDROID_LOG_INFO,
                        kLogTag,
                        "normal game.yaml bootstrap stopped (result=%d)",
                        result);
    return result;
}
