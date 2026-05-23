#pragma once

#include <string>
#include <vector>

namespace pac::pnc {

/// Room ids found directly under `host_dir` — one per `<id>.yaml` file, sorted.
/// Backs the dev "jump to room" action (#38); a missing or unreadable directory
/// yields an empty list. Host filesystem path (resolved via ResourceCache), so
/// this is dev-only and tied to the loose-files backend.
[[nodiscard]] std::vector<std::string> room_ids_in_dir(const std::string& host_dir);

} // namespace pac::pnc
