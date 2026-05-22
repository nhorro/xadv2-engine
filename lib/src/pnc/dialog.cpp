#include "engine/pnc/dialog.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scripting.hpp"
#include "pnc/dialog_internal.hpp"

#include <sol/sol.hpp>

#include <utility>

namespace pac::pnc {

struct DialogRuntime::Impl {
    sol::state* lua = nullptr;
    pac::core::Diagnostics* log = nullptr;
    DialogHost host;
    std::string npc_id;
    sol::table tree;
    /// The unique table injected into the dialog chunk's environment as `END`.
    /// We compare each `to` field against this by Lua-reference identity so a
    /// stray author typing `to = "__END__"` can't accidentally terminate the
    /// dialog.
    sol::table end_sentinel;

    State state = State::ENDED;
    std::string current_node;

    // Pending NPC lines for the current node (string or array of strings).
    std::vector<std::string> npc_lines;
    std::size_t npc_index = 0;

    // Options visible at the current node, in display order. The raw index is
    // the original 1-based position in `node.options`, needed to look up the
    // option table again on `choose()` and to track `once`-consumption.
    std::vector<DialogOption> visible;
    std::vector<int> visible_raw_index;

    // Note: `once`-consumption persistence is delegated to the host (see
    // `DialogHost::is_option_consumed` / `mark_option_consumed`). The host
    // typically backs this with the engine StateStore under reserved keys
    // (`__dialog.<id>.<node>.<idx>`), so flags survive across dialog sessions
    // and save/load. The test host backs it with a local set.

    // The raw index of the option chosen during SPEAKING_PLAYER.
    int chosen_raw = -1;

    // Cached `to` target for the chosen option, captured at the moment the
    // option is taken. We resolve `to` then rather than after `run` completes
    // because `run` may invalidate the option table (e.g., scripted state
    // changes that reshape the dialog) but the design fixes `to` at choose-time.
    sol::object pending_to;

    void log_error(const std::string& msg) {
        if (log) {
            log->error("dialog '" + npc_id + "': " + msg);
        }
    }

    /// True if `to` is the END sentinel (Lua-reference identity match). Used
    /// to distinguish an explicit end-of-dialog from a string node id.
    bool is_end(const sol::object& to) const {
        return to.is<sol::table>() && to.as<sol::table>() == end_sentinel;
    }

    void enter_node(const std::string& id) {
        if (id.empty()) {
            end();
            return;
        }
        sol::object node_obj = tree[id];
        if (!node_obj.is<sol::table>()) {
            log_error("missing node '" + id + "'");
            end();
            return;
        }
        sol::table node = node_obj;
        current_node = id;
        chosen_raw = -1;

        npc_lines.clear();
        npc_index = 0;
        if (sol::object npc = node["npc"]; npc.valid()) {
            if (npc.is<std::string>()) {
                npc_lines.push_back(npc.as<std::string>());
            } else if (npc.is<sol::table>()) {
                sol::table arr = npc;
                for (std::size_t i = 1; i <= arr.size(); ++i) {
                    if (sol::optional<std::string> s = arr[i]; s) {
                        npc_lines.push_back(*s);
                    }
                }
            }
        }
        if (!npc_lines.empty()) {
            state = State::SPEAKING_NPC;
            host.speak_npc(npc_lines[0]);
            return;
        }
        // No NPC line: jump straight to options / `to` / end.
        after_npc(node);
    }

    void after_npc(sol::table node) {
        compute_visible_options(node);
        if (!visible.empty()) {
            state = State::AWAITING_CHOICE;
            return;
        }
        // Validator guarantees `to` is either a string or the END sentinel
        // when present. Absent `to` means the node ends the dialog.
        sol::object to = node["to"];
        if (is_end(to) || !to.valid()) {
            end();
        } else if (to.is<std::string>()) {
            enter_node(to.as<std::string>());
        } else {
            end();
        }
    }

    void compute_visible_options(sol::table node) {
        visible.clear();
        visible_raw_index.clear();
        sol::optional<sol::table> opts = node["options"];
        if (!opts) {
            return;
        }
        for (std::size_t i = 1; i <= opts->size(); ++i) {
            sol::optional<sol::table> opt = (*opts)[i];
            if (!opt) {
                continue;
            }
            const int raw = static_cast<int>(i);
            if (host.is_option_consumed && host.is_option_consumed(current_node, raw)) {
                continue;
            }
            if (sol::optional<sol::protected_function> when = (*opt)["when"]; when) {
                const sol::protected_function_result r = (*when)();
                if (!r.valid()) {
                    const sol::error e = r;
                    log_error(std::string("option 'when' error: ") + e.what());
                    continue;
                }
                sol::optional<bool> b = r;
                if (!b || !*b) {
                    continue;
                }
            }
            sol::optional<std::string> text = (*opt)[1];
            if (!text) {
                continue;
            }
            DialogOption view;
            view.index = static_cast<int>(visible.size());
            view.text = *text;
            visible.push_back(std::move(view));
            visible_raw_index.push_back(raw);
        }
    }

    /// Kick off the chosen option's `run` callback. Returns true if the host
    /// reports a run task is still alive (so the caller should leave the state
    /// machine in RUNNING_CALLBACK); false if `run` completed synchronously or
    /// the option has no `run`. Errors are caught + logged either way per
    /// design 04: a failing `run` does not abort the dialog.
    bool kick_off_run(sol::table opt) {
        sol::optional<sol::protected_function> run = opt["run"];
        if (!run) {
            return false;
        }
        if (host.spawn_run) {
            // Production path: the host wraps `run` in a coroutine task, which
            // lets blocking script APIs inside `run` (`wait`, `talk`, ...) yield
            // properly. The host is also responsible for placing the task in
            // the dialog's scope so it dies with the dialog.
            DialogRunFn carrier{sol::function(*run)};
            host.spawn_run(carrier);
            return host.is_run_running && host.is_run_running();
        }
        // Test/fallback path: invoke synchronously. `wait`/`talk` from here
        // won't yield, but tests don't need them to.
        const sol::protected_function_result r = (*run)();
        if (!r.valid()) {
            const sol::error e = r;
            log_error(std::string("option 'run' error: ") + e.what());
        }
        return false;
    }

    /// Finalize the chosen option after its `run` (if any) is done: consume
    /// `once`, then either end (queued change_room) or follow `to`.
    void after_run() {
        sol::optional<sol::table> node = tree[current_node];
        sol::optional<sol::table> opts = node ? (*node)["options"] : sol::optional<sol::table>{};
        sol::optional<sol::table> opt = opts ? (*opts)[chosen_raw] : sol::optional<sol::table>{};
        if (opt) {
            if (sol::optional<bool> once = (*opt)["once"]; once && *once) {
                if (host.mark_option_consumed) {
                    host.mark_option_consumed(current_node, chosen_raw);
                }
            }
        }
        // The design's "if run calls change_room, the dialog ends first" rule:
        // when the host signals an external termination request after `run`,
        // skip the `to` follow.
        if (host.should_end && host.should_end()) {
            end();
            return;
        }
        const sol::object to = pending_to;
        pending_to = sol::object();
        if (is_end(to) || !to.valid()) {
            end();
        } else if (to.is<std::string>()) {
            enter_node(to.as<std::string>());
        } else {
            end();
        }
    }

    void on_player_line_finished() {
        sol::optional<sol::table> node = tree[current_node];
        if (!node) {
            end();
            return;
        }
        sol::optional<sol::table> opts = (*node)["options"];
        sol::optional<sol::table> opt = opts ? (*opts)[chosen_raw] : sol::optional<sol::table>{};
        if (!opt) {
            end();
            return;
        }
        if (kick_off_run(*opt)) {
            state = State::RUNNING_CALLBACK;
            return;
        }
        after_run();
    }

    void end() {
        if (sol::optional<sol::protected_function> on_exit = tree["on_exit"]; on_exit) {
            const sol::protected_function_result r = (*on_exit)();
            if (!r.valid()) {
                const sol::error e = r;
                log_error(std::string("on_exit error: ") + e.what());
            }
        }
        state = State::ENDED;
        visible.clear();
        visible_raw_index.clear();
        npc_lines.clear();
    }
};

DialogRuntime::DialogRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DialogRuntime::DialogRuntime(DialogRuntime&&) noexcept = default;
DialogRuntime& DialogRuntime::operator=(DialogRuntime&&) noexcept = default;
DialogRuntime::~DialogRuntime() = default;

std::optional<DialogRuntime> DialogRuntime::start(pac::core::Scripting& scripting,
                                                  pac::core::ResourceCache& resources,
                                                  pac::core::Diagnostics& log,
                                                  const std::string& npc_id,
                                                  DialogHost host) {
    const std::string logical = "dialogs/" + npc_id + ".lua";
    std::string code;
    try {
        code = resources.read_text(logical);
    } catch (const std::exception& e) {
        log.error(std::string("dialog '" + npc_id + "': ") + e.what());
        return std::nullopt;
    }
    sol::state& lua = scripting.lua();

    // Inject a unique sentinel table as the `END` global, scoped to this
    // load. A fresh table per dialog means a stray `to = "__END__"` can't be
    // mistaken for END (table identity comparison wins). The previous global
    // is restored after load so the engine doesn't leak `END` between dialogs.
    sol::table end_sentinel = lua.create_table();
    const sol::object prev_end = lua["END"];
    lua["END"] = end_sentinel;
    struct EndGuard {
        sol::state& lua;
        sol::object prev;
        ~EndGuard() { lua["END"] = prev; }
    } guard{lua, prev_end};

    sol::load_result chunk = lua.load(code, "@" + logical);
    if (!chunk.valid()) {
        const sol::error e = chunk;
        log.error(std::string("dialog '" + npc_id + "' load: ") + e.what());
        return std::nullopt;
    }
    const sol::protected_function_result r = sol::protected_function(chunk)();
    if (!r.valid()) {
        const sol::error e = r;
        log.error(std::string("dialog '" + npc_id + "' eval: ") + e.what());
        return std::nullopt;
    }
    sol::optional<sol::table> table = r;
    if (!table) {
        log.error("dialog '" + npc_id + "' did not return a table");
        return std::nullopt;
    }
    if (const std::string err = DialogInternal::validate(*table, end_sentinel); !err.empty()) {
        log.error("dialog '" + npc_id + "': " + err);
        return std::nullopt;
    }
    return DialogInternal::from_table(scripting,
                                      log,
                                      npc_id,
                                      std::move(host),
                                      *table,
                                      end_sentinel);
}

const std::string& DialogRuntime::npc_id() const {
    return impl_->npc_id;
}

DialogRuntime::State DialogRuntime::state() const {
    return impl_->state;
}

bool DialogRuntime::ended() const {
    return impl_->state == State::ENDED;
}

const std::string& DialogRuntime::current_node() const {
    return impl_->current_node;
}

std::vector<DialogOption> DialogRuntime::options() const {
    return impl_->visible;
}

void DialogRuntime::update() {
    Impl& s = *impl_;
    if (s.state == State::ENDED) {
        return;
    }
    if (s.state == State::RUNNING_CALLBACK) {
        // Wait until the host reports the spawned `run` task is no longer
        // alive. `is_run_running` is only consulted in this state — if the
        // host left it unset, kick_off_run already returned false and we
        // never entered RUNNING_CALLBACK.
        if (s.host.is_run_running && s.host.is_run_running()) {
            return;
        }
        s.after_run();
        return;
    }
    if (s.host.is_speaking && s.host.is_speaking()) {
        return;
    }
    if (s.state == State::SPEAKING_NPC) {
        if (s.npc_index + 1 < s.npc_lines.size()) {
            ++s.npc_index;
            s.host.speak_npc(s.npc_lines[s.npc_index]);
            return;
        }
        sol::optional<sol::table> node = s.tree[s.current_node];
        if (!node) {
            s.end();
            return;
        }
        s.after_npc(*node);
        return;
    }
    if (s.state == State::SPEAKING_PLAYER) {
        s.on_player_line_finished();
        return;
    }
}

void DialogRuntime::choose(int index) {
    Impl& s = *impl_;
    if (s.state != State::AWAITING_CHOICE) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(s.visible.size())) {
        return;
    }
    s.chosen_raw = s.visible_raw_index[index];
    sol::optional<sol::table> node = s.tree[s.current_node];
    sol::optional<sol::table> opts = node ? (*node)["options"] : sol::optional<sol::table>{};
    sol::optional<sol::table> opt = opts ? (*opts)[s.chosen_raw] : sol::optional<sol::table>{};
    if (!opt) {
        s.end();
        return;
    }
    // Capture `to` now so `run` is free to mutate dialog state without the
    // runtime losing track of where the option leads.
    s.pending_to = (*opt)["to"];
    bool silent = false;
    if (sol::optional<bool> v = (*opt)["silent"]; v) {
        silent = *v;
    }
    sol::optional<std::string> text = (*opt)[1];
    s.state = State::SPEAKING_PLAYER;
    if (!silent && text) {
        s.host.speak_player(*text);
    }
    // If silent (or no text), the next update sees is_speaking()==false and
    // advances to on_player_line_finished() immediately.
}

DialogRuntime DialogInternal::from_table(pac::core::Scripting& scripting,
                                         pac::core::Diagnostics& log,
                                         const std::string& npc_id,
                                         DialogHost host,
                                         sol::table dialog_table,
                                         sol::table end_sentinel) {
    auto impl = std::make_unique<DialogRuntime::Impl>();
    impl->lua = &scripting.lua();
    impl->log = &log;
    impl->host = std::move(host);
    impl->npc_id = npc_id;
    impl->tree = std::move(dialog_table);
    impl->end_sentinel = std::move(end_sentinel);

    if (sol::optional<sol::protected_function> on_enter = impl->tree["on_enter"]; on_enter) {
        const sol::protected_function_result r = (*on_enter)();
        if (!r.valid()) {
            const sol::error e = r;
            log.error(std::string("dialog '" + npc_id + "' on_enter error: ") + e.what());
        }
    }
    sol::optional<std::string> start = impl->tree["start"];
    if (!start) {
        log.error("dialog '" + npc_id + "' has no 'start'");
        impl->state = DialogRuntime::State::ENDED;
        return DialogRuntime(std::move(impl));
    }
    DialogRuntime runtime(std::move(impl));
    runtime.impl_->enter_node(*start);
    return runtime;
}

std::string DialogInternal::validate(sol::table dialog_table, sol::table end_sentinel) {
    // Helper: is `to` a valid target — string node id or the END sentinel?
    auto is_end = [&](const sol::object& to) {
        return to.is<sol::table>() && to.as<sol::table>() == end_sentinel;
    };
    auto check_to = [&](const sol::object& to, const std::string& where) -> std::string {
        if (!to.valid() || to.is<sol::lua_nil_t>()) {
            return where + ": `to` is nil (typo of `END`?)";
        }
        if (!to.is<std::string>() && !is_end(to)) {
            return where + ": `to` must be a node id or `END`";
        }
        if (to.is<std::string>()) {
            const std::string target = to.as<std::string>();
            const sol::object node = dialog_table[target];
            if (!node.is<sol::table>()) {
                return where + ": `to = \"" + target + "\"` references missing node";
            }
        }
        return {};
    };

    // Top-level shape: must have a `start` string field.
    sol::optional<std::string> start = dialog_table["start"];
    if (!start) {
        return "missing required `start` field (must be a node id)";
    }
    const sol::object start_node = dialog_table[*start];
    if (!start_node.is<sol::table>()) {
        return "`start = \"" + *start + "\"` references missing node";
    }

    // Walk each node (top-level table fields whose value is a table).
    for (auto kv : dialog_table) {
        if (!kv.first.is<std::string>() || !kv.second.is<sol::table>()) {
            continue;
        }
        const std::string node_id = kv.first.as<std::string>();
        if (node_id == "on_enter" || node_id == "on_exit") {
            continue;
        }
        sol::table node = kv.second;
        const sol::object options_obj = node["options"];
        const sol::object to_obj = node["to"];
        const bool has_options = options_obj.is<sol::table>();
        const bool has_to = to_obj.valid() && !to_obj.is<sol::lua_nil_t>();
        if (has_options && has_to) {
            return "node '" + node_id + "': has both `options` and `to` (pick one)";
        }
        if (has_to) {
            if (auto err = check_to(to_obj, "node '" + node_id + "'"); !err.empty()) {
                return err;
            }
        }
        if (has_options) {
            sol::table opts = options_obj.as<sol::table>();
            for (std::size_t i = 1; i <= opts.size(); ++i) {
                sol::optional<sol::table> opt = opts[i];
                if (!opt) {
                    continue;
                }
                if (auto err = check_to((*opt)["to"],
                                        "node '" + node_id + "' option " + std::to_string(i));
                    !err.empty()) {
                    return err;
                }
            }
        }
    }
    return {};
}

} // namespace pac::pnc
