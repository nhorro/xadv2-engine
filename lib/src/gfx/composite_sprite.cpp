#include "engine/gfx/composite_sprite.hpp"

#include "core/load_error_yaml.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/resource_source.hpp"
#include "engine/gfx/asset_error.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace pac::gfx {

namespace {

constexpr const char* kSource = "composite-loader";

[[noreturn]] void composite_fail(const std::string& code,
                                 const std::string& message,
                                 const YAML::Node& at = YAML::Node()) {
    pac::core::fail_at<AssetError>(kSource, code, message, at);
}

sf::Vector2f vector_from(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsMap() || !node["x"] || !node["y"]) {
        composite_fail("composite.vector-invalid", field + " must be {x, y}", node);
    }
    return {node["x"].as<float>(), node["y"].as<float>()};
}

CompositeNodeDefinition parse_node(const YAML::Node& node, bool root) {
    if (!node || !node.IsMap()) {
        composite_fail("composite.node-invalid", "composite nodes must be mappings", node);
    }
    CompositeNodeDefinition out;
    if (!node["id"] || !node["animation"]) {
        composite_fail("composite.node-incomplete", "a node needs id and animation", node);
    }
    out.id = node["id"].as<std::string>();
    out.animation = node["animation"].as<std::string>();
    if (out.id.empty() || out.animation.empty()) {
        composite_fail("composite.node-incomplete", "node id/animation cannot be empty", node);
    }
    if (!root) {
        if (!node["parent"] || !node["parent_anchor"] || !node["child_anchor"]) {
            composite_fail("composite.attachment-incomplete",
                           "a child needs parent, parent_anchor and child_anchor",
                           node);
        }
        out.parent = node["parent"].as<std::string>();
        out.parent_anchor = node["parent_anchor"].as<std::string>();
        out.child_anchor = node["child_anchor"].as<std::string>();
    }
    if (node["offset"]) {
        out.offset = vector_from(node["offset"], "node.offset");
    }
    out.scale = node["scale"] ? node["scale"].as<float>() : 1.0f;
    if (!(out.scale > 0.0f) || !std::isfinite(out.scale)) {
        composite_fail("composite.scale-invalid", "node scale must be finite and > 0", node);
    }
    out.rotation = node["rotation"] ? node["rotation"].as<float>() : 0.0f;
    out.z = node["z"] ? node["z"].as<int>() : 0;
    return out;
}

CompositePartSequence parse_part(const YAML::Node& node) {
    if (!node || !node.IsMap()) {
        composite_fail("composite.part-invalid", "sequence parts must be mappings", node);
    }
    CompositePartSequence out;
    if (node["sequence"]) {
        out.sequence = node["sequence"].as<std::string>();
    }
    if (const YAML::Node rotation = node["rotation"]) {
        if (!rotation.IsMap() || !rotation["to"] || !rotation["duration"]) {
            composite_fail("composite.rotation-invalid",
                           "rotation needs to and duration (from/loop are optional)",
                           rotation);
        }
        CompositeRotationTrack track;
        track.from = rotation["from"] ? rotation["from"].as<float>() : 0.0f;
        track.to = rotation["to"].as<float>();
        track.duration = rotation["duration"].as<float>();
        track.loop = rotation["loop"] ? rotation["loop"].as<bool>() : false;
        if (!(track.duration > 0.0f) || !std::isfinite(track.duration)) {
            composite_fail("composite.rotation-duration-invalid",
                           "rotation duration must be finite and > 0",
                           rotation["duration"]);
        }
        out.rotation = track;
    }
    if (out.sequence.empty() && !out.rotation) {
        composite_fail("composite.part-empty",
                       "a sequence part needs a sprite sequence or rotation",
                       node);
    }
    return out;
}

} // namespace

CompositeDefinition parse_composite(const std::string& yaml_text) {
    YAML::Node root;
    try {
        root = YAML::Load(yaml_text);
    } catch (const YAML::Exception& e) {
        composite_fail("composite.invalid-yaml", std::string("invalid YAML: ") + e.what());
    }
    if (!root || !root.IsMap() || !root["root"]) {
        composite_fail("composite.root-missing", "composite needs a root node", root);
    }

    std::map<std::string, CompositeNodeDefinition> by_id;
    std::vector<std::string> declaration_order;
    const auto add_node = [&](CompositeNodeDefinition node, const YAML::Node& at) {
        if (by_id.count(node.id) > 0) {
            composite_fail("composite.node-duplicate", "duplicate node id '" + node.id + "'", at);
        }
        declaration_order.push_back(node.id);
        by_id.emplace(node.id, std::move(node));
    };

    const CompositeNodeDefinition root_node = parse_node(root["root"], true);
    const std::string root_id = root_node.id;
    add_node(root_node, root["root"]);
    if (const YAML::Node children = root["children"]) {
        if (!children.IsSequence()) {
            composite_fail("composite.children-invalid", "children must be a sequence", children);
        }
        for (const YAML::Node& child : children) {
            add_node(parse_node(child, false), child);
        }
    }

    for (const auto& [id, node] : by_id) {
        if (id != root_id && by_id.count(node.parent) == 0) {
            composite_fail("composite.parent-unknown",
                           "node '" + id + "' references unknown parent '" + node.parent + "'");
        }
    }

    CompositeDefinition out;
    std::map<std::string, int> marks;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (marks[id] == 2) {
            return;
        }
        if (marks[id] == 1) {
            composite_fail("composite.parent-cycle", "attachment hierarchy contains a cycle");
        }
        marks[id] = 1;
        const CompositeNodeDefinition& node = by_id.at(id);
        if (!node.parent.empty()) {
            visit(node.parent);
        }
        marks[id] = 2;
        if (std::none_of(out.nodes.begin(), out.nodes.end(), [&](const auto& n) {
                return n.id == id;
            })) {
            out.nodes.push_back(node);
        }
    };
    for (const std::string& id : declaration_order) {
        visit(id);
    }
    if (out.nodes.empty() || out.nodes.front().id != root_id) {
        composite_fail("composite.root-invalid", "root must head the attachment hierarchy");
    }

    const YAML::Node sequences = root["sequences"];
    if (!sequences || !sequences.IsMap() || sequences.size() == 0) {
        composite_fail("composite.sequences-invalid",
                       "sequences must be a non-empty mapping",
                       root);
    }
    for (const auto& kv : sequences) {
        const std::string sequence_id = kv.first.as<std::string>();
        const YAML::Node seq_node = kv.second;
        const YAML::Node parts = seq_node && seq_node.IsMap() ? seq_node["parts"] : YAML::Node();
        if (!parts || !parts.IsMap()) {
            composite_fail("composite.parts-invalid",
                           "sequence '" + sequence_id + "' needs a parts mapping",
                           seq_node);
        }
        CompositeSequence sequence;
        for (const auto& part : parts) {
            const std::string node_id = part.first.as<std::string>();
            if (by_id.count(node_id) == 0) {
                composite_fail("composite.sequence-node-unknown",
                               "sequence '" + sequence_id + "' references unknown node '" +
                                   node_id + "'",
                               part.first);
            }
            sequence.parts.emplace(node_id, parse_part(part.second));
        }
        out.sequences.emplace(sequence_id, std::move(sequence));
    }
    return out;
}

CompositeSprite::CompositeSprite(CompositeDefinition definition,
                                 std::vector<AnimatedSprite> sprites)
    : sequences_(std::move(definition.sequences)) {
    if (definition.nodes.size() != sprites.size() || definition.nodes.empty()) {
        throw AssetError("composite sprite node/animation count mismatch");
    }
    std::map<std::string, std::size_t> indices;
    nodes_.reserve(definition.nodes.size());
    for (std::size_t i = 0; i < definition.nodes.size(); ++i) {
        const CompositeNodeDefinition& def = definition.nodes[i];
        std::size_t parent = 0;
        if (i > 0) {
            const auto it = indices.find(def.parent);
            if (it == indices.end()) {
                throw AssetError("composite nodes are not topologically ordered");
            }
            parent = it->second;
        }
        indices.emplace(def.id, i);
        nodes_.push_back(
            Node{def, parent, std::move(sprites[i]), def.rotation, false, std::nullopt});
    }
    for (const auto& [sequence_id, sequence] : sequences_) {
        for (const auto& [node_id, part] : sequence.parts) {
            const auto found = node_index(node_id);
            if (!found) {
                throw AssetError("composite sequence '" + sequence_id +
                                 "' references unknown node '" + node_id + "'");
            }
            const std::size_t index = *found;
            if (!part.sequence.empty() && !nodes_[index].sprite.has(part.sequence)) {
                throw AssetError("composite sequence '" + sequence_id + "' asks node '" + node_id +
                                 "' for missing animation sequence '" + part.sequence + "'");
            }
        }
    }
}

std::optional<std::size_t> CompositeSprite::node_index(const std::string& id) const {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].definition.id == id) {
            return i;
        }
    }
    return std::nullopt;
}

bool CompositeSprite::has(const std::string& sequence) const {
    return sequences_.count(sequence) > 0;
}

void CompositeSprite::play(const std::string& sequence, bool restart) {
    const auto it = sequences_.find(sequence);
    if (it == sequences_.end() || (!restart && current_sequence_ == sequence)) {
        return;
    }
    current_sequence_ = sequence;
    for (Node& node : nodes_) {
        node.rotation = node.definition.rotation;
        node.sequence_tracked = false;
        node.rotation_track.reset();
    }
    for (const auto& [id, part] : it->second.parts) {
        const std::size_t index = *node_index(id);
        Node& node = nodes_[index];
        if (!part.sequence.empty()) {
            node.sprite.play(part.sequence, true);
            node.sequence_tracked = true;
        }
        if (part.rotation) {
            node.rotation = part.rotation->from;
            node.rotation_track = RotationPlayback{*part.rotation, 0.0f, false};
        }
    }
    finished_ = false;
    refresh_finished();
}

void CompositeSprite::update(float dt) {
    for (Node& node : nodes_) {
        node.sprite.update(dt);
        if (!node.rotation_track) {
            continue;
        }
        RotationPlayback& playback = *node.rotation_track;
        playback.elapsed += std::max(0.0f, dt);
        if (playback.track.loop) {
            const float phase =
                std::fmod(playback.elapsed, playback.track.duration) / playback.track.duration;
            node.rotation = playback.track.from + (playback.track.to - playback.track.from) * phase;
        } else {
            const float phase = std::min(1.0f, playback.elapsed / playback.track.duration);
            node.rotation = playback.track.from + (playback.track.to - playback.track.from) * phase;
            playback.finished = phase >= 1.0f;
        }
    }
    refresh_finished();
}

void CompositeSprite::refresh_finished() {
    if (current_sequence_.empty()) {
        finished_ = true;
        return;
    }
    bool waiting = false;
    for (const Node& node : nodes_) {
        waiting = waiting || (node.sequence_tracked && !node.sprite.finished());
        waiting = waiting || (node.rotation_track &&
                              (node.rotation_track->track.loop || !node.rotation_track->finished));
    }
    finished_ = !waiting;
}

std::vector<sf::Transform> CompositeSprite::node_transforms() const {
    std::vector<sf::Transform> transforms(nodes_.size());
    if (nodes_.empty()) {
        return transforms;
    }
    sf::Transform root_local;
    root_local.translate(nodes_[0].definition.offset);
    root_local.rotate(nodes_[0].rotation);
    root_local.scale(nodes_[0].definition.scale, nodes_[0].definition.scale);
    transforms[0] = getTransform() * root_local;

    for (std::size_t i = 1; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        const Node& parent = nodes_[node.parent];
        const sf::Vector2f parent_anchor =
            parent.sprite.anchor_local(node.definition.parent_anchor).value_or(sf::Vector2f());
        const sf::Vector2f child_anchor =
            node.sprite.anchor_local(node.definition.child_anchor).value_or(sf::Vector2f());
        sf::Transform local;
        local.translate(parent_anchor + node.definition.offset);
        local.rotate(node.rotation);
        local.scale(node.definition.scale, node.definition.scale);
        local.translate(-child_anchor);
        transforms[i] = transforms[node.parent] * local;
    }
    return transforms;
}

sf::FloatRect CompositeSprite::global_bounds() const {
    const std::vector<sf::Transform> transforms = node_transforms();
    bool any = false;
    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const sf::FloatRect local = nodes_[i].sprite.local_bounds();
        if (local.width <= 0.0f || local.height <= 0.0f) {
            continue;
        }
        const sf::FloatRect bounds = transforms[i].transformRect(local);
        any = true;
        left = std::min(left, bounds.left);
        top = std::min(top, bounds.top);
        right = std::max(right, bounds.left + bounds.width);
        bottom = std::max(bottom, bounds.top + bounds.height);
    }
    return any ? sf::FloatRect(left, top, right - left, bottom - top)
               : sf::FloatRect(getPosition().x, getPosition().y, 0.0f, 0.0f);
}

std::optional<sf::Vector2f> CompositeSprite::anchor_world(const std::string& name) const {
    if (nodes_.empty()) {
        return std::nullopt;
    }
    std::size_t index = 0;
    std::string anchor = name;
    if (const std::size_t dot = name.find('.'); dot != std::string::npos) {
        const auto found = node_index(name.substr(0, dot));
        if (!found) {
            return std::nullopt;
        }
        index = *found;
        anchor = name.substr(dot + 1);
    }
    const auto local = nodes_[index].sprite.anchor_local(anchor);
    if (!local) {
        return std::nullopt;
    }
    return node_transforms()[index].transformPoint(*local);
}

void CompositeSprite::set_shaders(std::vector<ShaderEffect> shaders) {
    for (Node& node : nodes_) {
        node.sprite.set_shaders(shaders);
    }
}

void CompositeSprite::draw(sf::RenderTarget& target,
                           pac::core::ResourceCache& resources,
                           float time,
                           ShaderChain* chain) const {
    const std::vector<sf::Transform> transforms = node_transforms();
    std::vector<std::size_t> order(nodes_.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return nodes_[a].definition.z < nodes_[b].definition.z;
    });
    for (const std::size_t i : order) {
        nodes_[i].sprite.draw_transformed(target, transforms[i], resources, time, chain);
    }
}

CompositeSprite load_composite_sprite(pac::core::ResourceCache& resources,
                                      const std::string& logical) {
    CompositeDefinition definition = parse_composite(resources.read_text(logical));
    std::vector<AnimatedSprite> sprites;
    sprites.reserve(definition.nodes.size());
    const std::string directory = pac::core::logical_dir(logical);
    for (const CompositeNodeDefinition& node : definition.nodes) {
        sprites.push_back(
            load_animated_sprite(resources, pac::core::logical_join(directory, node.animation)));
    }
    return CompositeSprite(std::move(definition), std::move(sprites));
}

} // namespace pac::gfx
