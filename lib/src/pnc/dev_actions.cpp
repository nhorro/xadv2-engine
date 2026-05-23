#include "engine/pnc/dev_actions.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace pac::pnc {

std::vector<std::string> room_ids_in_dir(const std::string& host_dir) {
    std::vector<std::string> ids;
    std::error_code ec;
    std::filesystem::directory_iterator it(host_dir, ec);
    if (ec) {
        return ids; // missing / unreadable directory: no rooms to offer
    }
    for (const std::filesystem::directory_entry& entry : it) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".yaml") {
            continue;
        }
        ids.push_back(entry.path().stem().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace pac::pnc
