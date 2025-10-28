#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class SceneState : std::uint8_t {
	Idle = 0,
	Start,
	FirstPhase,
	Exchange,
	Mixed,
	End
};

std::string sceneStateToString(SceneState state);
std::optional<SceneState> sceneStateFromString(const std::string& value);

class SceneTimingConfig;

class SceneController {
public:
    SceneController();

    void setup(double nowSeconds, double fadeDurationSeconds);
    void update(double nowSeconds);

    /// Attempts to start a transition toward the requested state.
    /// Returns true if the request was accepted.
    bool requestState(SceneState target, double nowSeconds, bool manualRequest = true,
                      const std::string& triggerReason = "manual");

    void setTimingConfig(std::shared_ptr<const SceneTimingConfig> timingConfig);

	struct TransitionEvent {
		SceneState from = SceneState::Idle;
		SceneState to = SceneState::Idle;
		bool manual = true;
		bool completed = false;
		std::string triggerReason;
		double timestamp = 0.0;
		double timeInState = 0.0;
		double blendDuration = 0.0;
	};

    std::optional<TransitionEvent> popTransitionEvent();

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

    bool canTransition(SceneState from, SceneState to, bool manualRequest) const noexcept;
    bool startTransition(SceneState target, double nowSeconds, bool manualRequest,
                         const std::string& triggerReason);
    void pollAutoTransition(double nowSeconds);

    SceneState currentState_ = SceneState::Idle;
    double stateEnteredAt_ = 0.0;
    double fadeDuration_ = 1.0;
    Transition transition_;
    float blend_ = 0.0f;
    std::shared_ptr<const SceneTimingConfig> timingConfig_;
    std::vector<TransitionEvent> transitionEvents_;
    struct TransitionMeta {
        bool manual = true;
        std::string triggerReason = "manual";
        double requestedAt = 0.0;
        double timeInState = 0.0;
    } transitionMeta_;
};
