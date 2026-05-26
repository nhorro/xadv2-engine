#include "engine/pnc/dev_actions.hpp"

#include "engine/core/resource_source.hpp"

#include <algorithm>
#include <filesystem>

namespace pac::pnc {

std::vector<std::string> room_ids_in_dir(const pac::core::ResourceSource& source,
                                         const std::string& rooms_dir) {
    std::vector<std::string> ids;
    for (const std::string& logical : source.list(rooms_dir, ".yaml")) {
        // `logical` is a full logical path under `rooms_dir`. The id is the
        // file stem: strip the leading `<rooms_dir>/` and the `.yaml`
        // extension, and skip anything nested in a subdirectory (current rooms
        // are flat under `rooms_dir`).
        std::string tail = logical;
        if (!rooms_dir.empty()) {
            const std::string prefix = rooms_dir + "/";
            if (tail.compare(0, prefix.size(), prefix) == 0) {
                tail = tail.substr(prefix.size());
            }
        }
        if (tail.find('/') != std::string::npos) {
            continue; // nested file — not a top-level room
        }
        if (tail.size() < 5 || tail.compare(tail.size() - 5, 5, ".yaml") != 0) {
            continue;
        }
        ids.push_back(tail.substr(0, tail.size() - 5));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace pac::pnc
