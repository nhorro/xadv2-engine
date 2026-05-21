#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"

#include <doctest/doctest.h>

using namespace pac::core;

namespace {
// ERROR level keeps normal output quiet; the error-reporting test still logs the
// (expected) script errors it triggers.
Diagnostics quiet() {
    return Diagnostics(LogLevel::ERROR);
}
} // namespace

TEST_CASE("run_string reports load and runtime errors") {
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string("local x = 1 + 1"));
    CHECK_FALSE(s.run_string("this is not lua )(")); // syntax error
    CHECK_FALSE(s.run_string("error('boom')"));      // runtime error
}

TEST_CASE("spawn + wait advances by timer then finishes") {
    Diagnostics log = quiet();
    Scripting s(log);
    CHECK(s.run_string("spawn(function() wait(1.0) end)"));
    CHECK(s.active_task_count() == 1);
    s.update(0.5f); // starts task, yields a 1.0s timer
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // 1.0 -> 0.4
    CHECK(s.active_task_count() == 1);
    s.update(0.6f); // 0.4 -> elapsed -> coroutine returns -> done
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("spawn inherits the current scope") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId sc = s.open_scope();
    s.set_current_scope(sc);
    s.run_string("spawn(function() wait(1.0) end)");
    s.set_current_scope(s.global_scope());
    CHECK(s.active_task_count(sc) == 1);
    CHECK(s.active_task_count(s.global_scope()) == 0);
}

TEST_CASE("wait_event resumes on emit, and events do not cross scopes") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId a = s.open_scope();
    const ScopeId b = s.open_scope();

    s.set_current_scope(a);
    CHECK(s.run_string("spawn(function() wait_event('go') end)"));
    s.set_current_scope(s.global_scope());

    s.update(0.0f); // start -> waits on event 'go'
    CHECK(s.active_task_count(a) == 1);

    s.emit(b, "go"); // wrong scope
    s.update(0.0f);
    CHECK(s.active_task_count(a) == 1);

    s.emit(a, "go"); // right scope -> wakes
    s.update(0.0f);
    CHECK(s.active_task_count(a) == 0);
}

TEST_CASE("cancel_scope removes tasks and never resumes them") {
    Diagnostics log = quiet();
    Scripting s(log);
    const ScopeId sc = s.open_scope();
    s.set_current_scope(sc);
    CHECK(s.run_string("spawn(function() wait(100.0) end)"));
    s.set_current_scope(s.global_scope());

    s.update(0.0f); // start -> long timer
    CHECK(s.active_task_count() == 1);
    CHECK(s.active_task_count(sc) == 1);

    s.cancel_scope(sc);
    CHECK(s.active_task_count(sc) == 0);
    CHECK(s.active_task_count() == 0);

    s.update(200.0f); // the timer would have fired; it must stay gone
    CHECK(s.active_task_count() == 0);
}

TEST_CASE("show_text exposes the current page and clears it when done") {
    Diagnostics log = quiet();
    Scripting s(log);
    s.run_string("spawn(function() show_text('hola', 1.0) end)");
    CHECK(s.current_text().empty());

    s.update(0.0f); // start -> page shown
    CHECK(s.current_text() == "hola");

    s.update(2.0f); // page duration elapses -> resume -> finish, page cleared
    CHECK(s.current_text().empty());
    CHECK(s.active_task_count() == 0);
}
