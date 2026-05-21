#include "engine/core/scene_manager.hpp"

#include "engine/core/scene.hpp"

#include <utility>

namespace pac::core {

void SceneManager::set_builder(Builder builder) {
    builder_ = std::move(builder);
}

void SceneManager::set_settings_scene_id(std::string id) {
    settings_scene_id_ = std::move(id);
}

void SceneManager::goto_scene(const std::string& id) {
    if (id == "QUIT") {
        quit();
        return;
    }
    pending_.push_back({OpKind::GOTO, id});
}

void SceneManager::push_scene(const std::string& id) {
    if (id == "QUIT") {
        quit();
        return;
    }
    pending_.push_back({OpKind::PUSH, id});
}

void SceneManager::pop_scene() {
    pending_.push_back({OpKind::POP, {}});
}

void SceneManager::open_settings() {
    if (settings_scene_id_.empty()) {
        return;
    }
    push_scene(settings_scene_id_);
}

void SceneManager::quit() {
    pending_.push_back({OpKind::QUIT, {}});
}

Scene* SceneManager::top() const {
    return stack_.empty() ? nullptr : stack_.back().get();
}

std::unique_ptr<Scene> SceneManager::build(const std::string& id) {
    return builder_ ? builder_(id) : nullptr;
}

void SceneManager::apply_pending() {
    while (!pending_.empty()) {
        const Op op = pending_.front();
        pending_.erase(pending_.begin());
        switch (op.kind) {
        case OpKind::GOTO: {
            while (!stack_.empty()) {
                stack_.back()->leave();
                stack_.pop_back();
            }
            std::unique_ptr<Scene> scene = build(op.id);
            if (scene) {
                scene->enter();
                stack_.push_back(std::move(scene));
            } else {
                running_ = false; // cannot continue with no scene
            }
            break;
        }
        case OpKind::PUSH: {
            std::unique_ptr<Scene> scene = build(op.id);
            if (scene) {
                scene->enter();
                stack_.push_back(std::move(scene));
            }
            break;
        }
        case OpKind::POP: {
            if (!stack_.empty()) {
                stack_.back()->leave();
                stack_.pop_back();
            }
            if (stack_.empty()) {
                running_ = false;
            }
            break;
        }
        case OpKind::QUIT: {
            while (!stack_.empty()) {
                stack_.back()->leave();
                stack_.pop_back();
            }
            running_ = false;
            pending_.clear();
            return;
        }
        }
    }
}

void SceneManager::handle_event(const sf::Event& event) {
    if (Scene* t = top()) {
        t->handle_event(event);
    }
}

void SceneManager::update(float dt) {
    if (Scene* t = top()) {
        t->update(dt);
    }
}

void SceneManager::draw(sf::RenderTarget& target) const {
    if (stack_.empty()) {
        return;
    }
    // Draw from the topmost opaque scene upward; lower scenes it hides are skipped.
    std::size_t start = 0;
    for (std::size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i]->opaque()) {
            start = i;
            break;
        }
    }
    for (std::size_t i = start; i < stack_.size(); ++i) {
        stack_[i]->draw(target);
    }
}

} // namespace pac::core
