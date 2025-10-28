#include "SceneController.h"

#include <algorithm>

namespace {
constexpr double kEpsilon = 1e-6;
}

SceneController::SceneController() = default;

void SceneController::setup(double nowSeconds, double fadeDurationSeconds) {
    stateEnteredAt_ = nowSeconds;
    fadeDuration_ = std::max(fadeDurationSeconds, 0.001);
    transition_ = {};
    transition_.from = currentState_;
    transition_.to = currentState_;
    transition_.startTime = nowSeconds;
    transition_.active = false;
    blend_ = 0.0f;
}

void SceneController::update(double nowSeconds) {
    if (!transition_.active) {
        blend_ = 0.0f;
        return;
    }

    const double elapsed = nowSeconds - transition_.startTime;
    if (elapsed >= fadeDuration_ - kEpsilon) {
        currentState_ = transition_.to;
        stateEnteredAt_ = nowSeconds;
        transition_.active = false;
        blend_ = 0.0f;
        return;
    }

    const double ratio = std::clamp(elapsed / fadeDuration_, 0.0, 1.0);
    blend_ = static_cast<float>(ratio);
}

bool SceneController::requestState(SceneState target, double nowSeconds) {
    if (target == currentState_) {
        // Nothing to do.
        return false;
    }

    if (transition_.active) {
        if (target == transition_.to) {
            return false;
        }
        // Prevent abrupt flips; wait for current transition to finish first.
        return false;
    }

    if (!canTransition(currentState_, target)) {
        return false;
    }

    transition_.from = currentState_;
    transition_.to = target;
    transition_.startTime = nowSeconds;
    transition_.active = true;
    blend_ = 0.0f;
    return true;
}

SceneState SceneController::currentState() const noexcept {
    return currentState_;
}

SceneState SceneController::targetState() const noexcept {
    return transition_.active ? transition_.to : currentState_;
}

bool SceneController::isTransitioning() const noexcept {
    return transition_.active;
}

float SceneController::transitionBlend() const noexcept {
    return blend_;
}

double SceneController::timeInState(double nowSeconds) const noexcept {
    return nowSeconds - stateEnteredAt_;
}

bool SceneController::canTransition(SceneState from, SceneState to) const noexcept {
    switch (from) {
        case SceneState::Idle:
            return to == SceneState::FirstPhase;
        case SceneState::FirstPhase:
            return to == SceneState::End || to == SceneState::Idle;
        case SceneState::End:
            return to == SceneState::Idle;
    }
    return false;
}
