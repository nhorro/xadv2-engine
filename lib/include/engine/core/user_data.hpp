#pragma once

#include <filesystem>
#include <string>

namespace pac::core {

/// Per-user writable directory for save files and other persistent app data.
/// Resolves to:
///
/// - `$XDG_DATA_HOME/<app_name>` if set (Linux/XDG hosts);
/// - `$HOME/.local/share/<app_name>` on Linux/macOS with HOME but no XDG_DATA_HOME;
/// - `$HOME/Library/Application Support/<app_name>` on macOS;
/// - `%APPDATA%/<app_name>` on Windows;
/// - `./<app_name>` as a last-resort fallback when none of the above resolve.
///
/// Creates the directory (with parents) if it does not exist. `app_name` must
/// be a stable, filesystem-friendly id: no slashes, no `..`, no leading `.`.
std::filesystem::path user_data_dir(const std::string& app_name);

/// Save directory for this launch. A packaged portable build places a
/// `portable.flag` marker beside the executable; in that case saves live in the
/// sibling `saves/` directory. Other builds retain the platform user-data path.
/// `executable_dir` is supplied by the application so the policy stays easy to
/// test and does not depend on process-global path discovery here.
std::filesystem::path save_data_dir(const std::string& app_name,
                                    const std::filesystem::path& executable_dir);

/// Per-user writable directory for *configuration* (player settings). Distinct
/// from `user_data_dir` on Linux, where config conventionally lives apart from
/// data. Resolves to:
///
/// - `$XDG_CONFIG_HOME/<app_name>` if set (Linux/XDG hosts);
/// - `$HOME/.config/<app_name>` on Linux/macOS with HOME but no XDG_CONFIG_HOME;
/// - `$HOME/Library/Application Support/<app_name>` on macOS;
/// - `%APPDATA%/<app_name>` on Windows;
/// - `./<app_name>` as a last-resort fallback when none of the above resolve.
///
/// Creates the directory (with parents) if it does not exist. Same `app_name`
/// constraints as `user_data_dir`.
std::filesystem::path user_config_dir(const std::string& app_name);

} // namespace pac::core
