#include "engine/pnc/dialog.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/scripting.hpp"
#include "pnc/dialog_internal.hpp"

#include <sol/sol.hpp>

#include <set>
#include <utility>

namespace pac::pnc {

namespace {

constexpr const char* kEndSentinel = "__END__";

} // namespace

struct DialogRuntime::Impl {
    sol::state* lua = nullptr;
    pac::core::Diagnostics* log = nullptr;
    DialogHost host;
    std::string npc_id;
    sol::table tree;

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

    // (node, raw_option_index) pairs the player has used at least once.
    std::set<std::pair<std::string, int>> consumed_once;

    // The raw index of the option chosen during SPEAKING_PLAYER.
    int chosen_raw = -1;

    void log_error(const std::string& msg) {
        if (log) {
            log->error("dialog '" + npc_id + "': " + msg);
        }
    }

    void enter_node(const std::string& id) {
        if (id == kEndSentinel || id.empty()) {
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
        if (sol::optional<std::string> to = node["to"]; to) {
            enter_node(*to);
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
            if (consumed_once.count({current_node, raw}) > 0) {
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
        if (sol::optional<sol::protected_function> run = (*opt)["run"]; run) {
            const sol::protected_function_result r = (*run)();
            if (!r.valid()) {
                const sol::error e = r;
                log_error(std::string("option 'run' error: ") + e.what());
            }
        }
        if (sol::optional<bool> once = (*opt)["once"]; once && *once) {
            consumed_once.insert({current_node, chosen_raw});
        }
        if (sol::optional<std::string> to = (*opt)["to"]; to) {
            enter_node(*to);
        } else {
            end();
        }
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
    sol::state& L = scripting.lua();
    sol::load_result chunk = L.load(code, "@" + logical);
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
    return DialogInternal::from_table(scripting, log, npc_id, std::move(host), *table);
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
                                         sol::table dialog_table) {
    auto impl = std::make_unique<DialogRuntime::Impl>();
    impl->lua = &scripting.lua();
    impl->log = &log;
    impl->host = std::move(host);
    impl->npc_id = npc_id;
    impl->tree = std::move(dialog_table);

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

} // namespace pac::pnc
