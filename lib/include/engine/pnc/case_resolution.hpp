#pragma once

#include "engine/geom/geometry.hpp"

#include <SFML/Graphics/Color.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pac::pnc {

struct CaseTerm {
    std::string id;
    std::string name;
    std::string tag; // empty means any
};

struct CaseTermBank {
    int version = 1;
    std::vector<CaseTerm> terms;
    [[nodiscard]] const CaseTerm* find(const std::string& id) const;
};

struct CaseSlot {
    std::string id;
    geom::Polygon area;
    std::vector<std::string> accepts; // empty accepts any term
    std::string solution;             // optional term id
};

struct CaseResolutionData {
    int version = 1;
    std::string id;
    std::string background;
    sf::Color background_color = sf::Color(21, 22, 23);
    float canvas_height = 592.0f;
    std::vector<CaseSlot> slots;

    [[nodiscard]] const CaseSlot* slot_at(geom::Point p) const;
};

CaseTermBank parse_case_terms(const std::string& yaml_text);
CaseResolutionData parse_case_resolution(const std::string& yaml_text,
                                         const std::string& expected_id = {},
                                         const std::string& logical_path = {});

[[nodiscard]] bool case_slot_accepts(const CaseSlot& slot, const CaseTerm& term);

/// Headless assignment rules shared by input handling and tests. A term can occupy
/// only one slot; assigning it elsewhere moves it. Invalid assignments are ignored.
class CaseAssignments {
public:
    bool assign(const CaseSlot& slot, const CaseTerm& term);
    void clear(const std::string& slot_id);
    [[nodiscard]] const std::string* term_for(const std::string& slot_id) const;
    [[nodiscard]] bool complete(const CaseResolutionData& data) const;
    [[nodiscard]] std::size_t invalid_count(const CaseResolutionData& data) const;
    [[nodiscard]] bool solved(const CaseResolutionData& data) const;

private:
    std::map<std::string, std::string> by_slot_;
};

} // namespace pac::pnc
