#include "engine/core/scene_manager.hpp"

#include "engine/core/scene.hpp"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/View.hpp>

#include <utility>

namespace pac::core {

void SceneManager::set_builder(Builder builder) {
    builder_ = std::move(builder);
}

void SceneManager::set_scene_entered_callback(SceneEntered callback) {
    scene_entered_ = std::move(callback);
}

void SceneManager::set_settings_scene_id(std::string id) {
    settings_scene_id_ = std::move(id);
}

void SceneManager::set_confirmation_scene_id(std::string id) {
    confirmation_scene_id_ = std::move(id);
}

void SceneManager::set_save_scene_id(std::string id) {
    save_scene_id_ = std::move(id);
}

void SceneManager::set_load_scene_id(std::string id) {
    load_scene_id_ = std::move(id);
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

void SceneManager::open_save() {
    if (save_scene_id_.empty()) {
        return;
    }
    push_scene(save_scene_id_);
}

void SceneManager::open_load() {
    if (load_scene_id_.empty()) {
        return;
    }
    push_scene(load_scene_id_);
}

void SceneManager::request_quit() {
    if (confirmation_scene_id_.empty()) {
        quit();
        return;
    }
    if (confirmation_action_ != ConfirmationAction::NONE) {
        return;
    }
    confirmation_action_ = ConfirmationAction::QUIT_APPLICATION;
    confirmation_target_.clear();
    push_scene(confirmation_scene_id_);
}

void SceneManager::request_goto_scene(const std::string& id) {
    if (id == "QUIT") {
        request_quit();
        return;
    }
    if (confirmation_scene_id_.empty()) {
        goto_scene(id);
        return;
    }
    if (confirmation_action_ != ConfirmationAction::NONE) {
        return;
    }
    confirmation_action_ = ConfirmationAction::GOTO_SCENE;
    confirmation_target_ = id;
    push_scene(confirmation_scene_id_);
}

void SceneManager::accept_confirmation() {
    const ConfirmationAction action = confirmation_action_;
    const std::string target = confirmation_target_;
    confirmation_action_ = ConfirmationAction::NONE;
    confirmation_target_.clear();

    if (action == ConfirmationAction::QUIT_APPLICATION) {
        quit();
    } else if (action == ConfirmationAction::GOTO_SCENE) {
        goto_scene(target);
    }
}

void SceneManager::cancel_confirmation() {
    if (confirmation_action_ == ConfirmationAction::NONE) {
        return;
    }
    confirmation_action_ = ConfirmationAction::NONE;
    confirmation_target_.clear();
    pop_scene();
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

void SceneManager::do_goto(const std::string& id) {
    while (!stack_.empty()) {
        stack_.back()->leave();
        stack_.pop_back();
    }
    std::unique_ptr<Scene> scene = build(id);
    if (scene) {
        if (scene_entered_) {
            scene_entered_(id);
        }
        scene->enter();
        stack_.push_back(std::move(scene));
        current_scene_id_ = id;
    } else {
        running_ = false; // cannot continue with no scene
    }
}

void SceneManager::apply_pending() {
    // A faded GOTO holds the swap until the screen is fully black, then swaps and
    // fades back in. While waiting, leave any further ops queued.
    if (transition_pending_) {
        if (!fade_.opaque()) {
            return;
        }
        do_goto(transition_target_);
        transition_pending_ = false;
        fade_.fade_in(transition_duration_);
    }

    while (!pending_.empty()) {
        const Op op = pending_.front();
        pending_.erase(pending_.begin());

        // Full-screen scene replacement fades to black first (overlays don't).
        if (op.kind == OpKind::GOTO && transition_duration_ > 0.0f) {
            transition_pending_ = true;
            transition_target_ = op.id;
            fade_.fade_out(transition_duration_);
            return; // wait for black; remaining ops stay queued
        }

        switch (op.kind) {
        case OpKind::GOTO: {
            do_goto(op.id);
            break;
        }
        case OpKind::PUSH: {
            std::unique_ptr<Scene> scene = build(op.id);
            if (scene) {
                if (scene_entered_) {
                    scene_entered_(op.id);
                }
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
            for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
                (*it)->prepare_for_application_exit();
            }
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
    if (transition_pending_) {
        return; // a scene swap is committed; ignore input until it completes
    }
    if (Scene* t = top()) {
        t->handle_event(event);
    }
}

bool SceneManager::enter_pause_menu() {
    if (Scene* t = top()) {
        return t->enter_pause_menu();
    }
    return false;
}

void SceneManager::leave_pause_menu() {
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        if ((*it)->pause_menu_active()) {
            (*it)->leave_pause_menu();
            return;
        }
    }
}

bool SceneManager::pause_menu_active() const {
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        if ((*it)->pause_menu_active()) {
            return true;
        }
    }
    return false;
}

void SceneManager::update_transition(float dt) {
    fade_.update(dt);
}

void SceneManager::update(float dt) {
    update_transition(dt);
    if (Scene* t = top()) {
        t->update(dt);
    }
}

void SceneManager::draw(sf::RenderTarget& target) const {
    if (!stack_.empty()) {
        // Draw from the topmost opaque scene upward; hidden lower scenes are skipped.
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

    // Fade overlay: a black quad over the whole window (bars included), drawn in
    // window pixels so it is independent of whatever view a scene left set.
    const sf::Uint8 a = fade_.alpha255();
    if (a > 0) {
        const sf::View prev = target.getView();
        const sf::Vector2f size(static_cast<float>(target.getSize().x),
                                static_cast<float>(target.getSize().y));
        target.setView(sf::View(sf::FloatRect(0.0f, 0.0f, size.x, size.y)));
        sf::RectangleShape quad(size);
        quad.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(quad);
        target.setView(prev);
    }
}

} // namespace pac::core
