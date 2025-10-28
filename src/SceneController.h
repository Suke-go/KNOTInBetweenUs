#pragma once

#include <cstdint>

enum class SceneState : std::uint8_t {
    Idle = 0,
    FirstPhase,
    End
};

class SceneController {
public:
    SceneController();

    void setup(double nowSeconds, double fadeDurationSeconds);
    void update(double nowSeconds);

    /// Attempts to start a transition toward the requested state.
    /// Returns true if the request was accepted.
    bool requestState(SceneState target, double nowSeconds);

    SceneState currentState() const noexcept;
    SceneState targetState() const noexcept;
    bool isTransitioning() const noexcept;
    float transitionBlend() const noexcept;
    double timeInState(double nowSeconds) const noexcept;

private:
    struct Transition {
        SceneState from = SceneState::Idle;
        SceneState to = SceneState::Idle;
        double startTime = 0.0;
        bool active = false;
    };

    bool canTransition(SceneState from, SceneState to) const noexcept;

    SceneState currentState_ = SceneState::Idle;
    double stateEnteredAt_ = 0.0;
    double fadeDuration_ = 1.0;
    Transition transition_;
    float blend_ = 0.0f;
};
