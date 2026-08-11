#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/scripting_sol.hpp"
#include "engine/pnc/case_resolution.hpp"
#include "engine/pnc/case_resolution_runtime.hpp"
#include "engine/pnc/data_error.hpp"
#include "loader_diag.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;
using pac::test::error_code;

TEST_CASE("case resolution parses terms, tag constraints, and relative background") {
    const auto bank = parse_case_terms(R"(
terms:
  - { id: tomas, name: "Tomás", tag: person }
  - { id: van, name: "una camioneta", tag: object }
  - { id: unknown, name: "algo" }
)");
    REQUIRE(bank.terms.size() == 3);
    REQUIRE(bank.find("van"));
    CHECK(bank.find("van")->tag == "object");

    const auto data = parse_case_resolution(R"(
id: route
background: board.png
slots:
  who:
    accepts: [person]
    solution: tomas
    area: [{x: 0, y: 0}, {x: 100, y: 0}, {x: 100, y: 50}, {x: 0, y: 50}]
)",
                                            {},
                                            "cases/route/template.yml");
    CHECK(data.background == "cases/route/board.png");
    REQUIRE(data.slot_at({50, 25}));
    CHECK(case_slot_accepts(data.slots[0], *bank.find("tomas")));
    CHECK_FALSE(case_slot_accepts(data.slots[0], *bank.find("van")));
    CHECK(case_slot_accepts(data.slots[0], *bank.find("unknown"))); // no tags = any
}

TEST_CASE("case term colors are stable per tag and preserve the requested alpha") {
    const sf::Color person = case_term_color("person");
    CHECK(case_term_color("person") == person);
    CHECK(case_term_color("object") != person);
    CHECK(case_term_color("person", 73).a == 73);
    CHECK(case_term_color("", 91) == sf::Color(58, 70, 80, 91));
}

TEST_CASE("assignments move terms, reject wrong tags, and check solutions") {
    const CaseTerm person{"tomas", "Tomás", "person"};
    const CaseTerm other{"malena", "Malena", "person"};
    const CaseTerm time{"1540", "15:40", "time"};
    CaseSlot who{"who", {}, {"person"}, "tomas"};
    CaseSlot witness{"witness", {}, {"person"}, ""};
    CaseResolutionData data;
    data.slots = {who, witness};
    CaseAssignments a;
    CHECK_FALSE(a.assign(who, time));
    CHECK(a.assign(who, person));
    CHECK(a.complete(data) == false);
    CHECK_FALSE(a.solved(data));
    CHECK(a.assign(witness, other));
    CHECK(a.complete(data));
    CHECK(a.solved(data));
    CHECK(a.assign(witness, person));
    CHECK(a.term_for("who") == nullptr);
    CHECK_FALSE(a.solved(data));
}

TEST_CASE("solution groups accept their terms in any slot order") {
    const CaseTerm can{"negatives_can", "lata de negativos", "object"};
    const CaseTerm notebook{"black_notebook", "libreta negra", "object"};
    const CaseTerm van{"blue_van", "camioneta azul", "object"};
    const CaseSlot first{"object_1", {}, {"object"}, ""};
    const CaseSlot second{"object_2", {}, {"object"}, ""};
    CaseResolutionData data;
    data.slots = {first, second};
    data.solution_groups = {
        {"evidence", {"object_1", "object_2"}, {"negatives_can", "black_notebook"}}};

    CaseAssignments assignments;
    CHECK(assignments.assign(first, notebook));
    CHECK(assignments.assign(second, can));
    CHECK(assignments.complete(data));
    CHECK(assignments.invalid_count(data) == 0);
    CHECK(assignments.solved(data));

    CHECK(assignments.assign(second, van));
    CHECK(assignments.invalid_count(data) == 1);
    CHECK_FALSE(assignments.solved(data));
}

TEST_CASE("case resolution parses unordered groups and optional sound hooks") {
    const CaseResolutionData data = parse_case_resolution(R"(
id: evidence
background: board.png
sounds:
  pickup: audio/pick.ogg
  place: ""
  return: audio/return.ogg
solution_groups:
  objects:
    slots: [first, second]
    terms: [can, notebook]
slots:
  first:
    accepts: [object]
    area: [{x: 0, y: 0}, {x: 10, y: 0}, {x: 10, y: 10}, {x: 0, y: 10}]
  second:
    accepts: [object]
    area: [{x: 20, y: 0}, {x: 30, y: 0}, {x: 30, y: 10}, {x: 20, y: 10}]
)",
                                                          {},
                                                          "cases/evidence/template.yaml");
    REQUIRE(data.solution_groups.size() == 1);
    CHECK(data.solution_groups[0].id == "objects");
    CHECK(data.solution_groups[0].slots == std::vector<std::string>{"first", "second"});
    CHECK(data.solution_groups[0].terms == std::vector<std::string>{"can", "notebook"});
    CHECK(data.sounds.pickup == "cases/evidence/audio/pick.ogg");
    CHECK(data.sounds.place.empty());
    CHECK(data.sounds.return_to_bank == "cases/evidence/audio/return.ogg");
}

TEST_CASE("case resolution reports stable authoring errors") {
    CHECK(error_code([] { parse_case_terms("terms: [{id: a}]"); }) == "case.term-fields-missing");
    CHECK(error_code([] { parse_case_resolution("id: x\nbackground: a.png\n"); }) ==
          "case.slots-missing");
    CHECK(error_code([] { parse_case_terms("terms: [{id: a, name: A, tags: [x, y]}]"); }) ==
          "case.term-tags-unsupported");
    CHECK(error_code([] {
              parse_case_resolution(R"(
id: x
background: a.png
solution_groups: {pair: {slots: [a], terms: [one, two]}}
slots:
  a: {area: [{x: 0, y: 0}, {x: 1, y: 0}, {x: 0, y: 1}]}
)");
          }) == "case.solution-group-size-mismatch");
    CHECK(error_code([] {
              parse_case_resolution(R"(
id: x
background: a.png
solution_groups: {pair: {slots: [missing], terms: [one]}}
slots:
  a: {area: [{x: 0, y: 0}, {x: 1, y: 0}, {x: 0, y: 1}]}
)");
          }) == "case.solution-group-slot-unknown");
}

TEST_CASE("case resolution sidecars receive check and exit status") {
    pac::core::Diagnostics log;
    pac::core::Scripting scripting(log);
    CaseResolutionRuntime runtime;
    REQUIRE(runtime.load(scripting,
                         R"(
        return {
            on_check = function(invalid) checked = invalid end,
            on_exit = function(status) exited = status end,
        }
    )",
                         "cases/test.lua",
                         log));
    runtime.run_on_check(2);
    runtime.run_on_exit("incorrect");
    CHECK(scripting.lua()["checked"].get<int>() == 2);
    CHECK(scripting.lua()["exited"].get<std::string>() == "incorrect");
}
