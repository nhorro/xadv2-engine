#pragma once

#include <string>
#include <vector>

namespace pac::core {
class ResourceSource;
}

namespace pac::pnc {

/// Room ids under the rooms logical directory `rooms_dir` (e.g. `rooms`) —
/// one per `<id>.yaml` file, sorted. Backs the dev "jump to room" action
/// (#38). Routes through `ResourceSource::list` so it works for both the
/// loose-files and packed backends (#109); a backend that can't enumerate
/// yields an empty list.
[[nodiscard]] std::vector<std::string> room_ids_in_dir(const pac::core::ResourceSource& source,
                                                       const std::string& rooms_dir);

} // namespace pac::pnc
