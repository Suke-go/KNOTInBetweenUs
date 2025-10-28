#include "SceneController.h"

#include "SceneTimingConfig.h"

#include "ofMain.h"

#include <algorithm>

namespace {
constexpr double kEpsilon = 1e-6;
}  // namespace

std::string sceneStateToString(SceneState state) {
	switch (state) {
		case SceneState::Idle:
			return "Idle";
		case SceneState::Start:
			return "Start";
		case SceneState::FirstPhase:
			return "FirstPhase";
		case SceneState::Exchange:
			return "Exchange";
		case SceneState::Mixed:
			return "Mixed";
		case SceneState::End:
			return "End";
	}
	return "Unknown";
}

std::optional<SceneState> sceneStateFromString(const std::string& value) {
	const std::string lower = ofToLower(value);
	if (lower == "idle") {
		return SceneState::Idle;
	}
	if (lower == "start") {
		return SceneState::Start;
	}
	if (lower == "firstphase" || lower == "first_phase") {
		return SceneState::FirstPhase;
	}
	if (lower == "exchange") {
		return SceneState::Exchange;
	}
	if (lower == "mixed") {
		return SceneState::Mixed;
	}
	if (lower == "end") {
		return SceneState::End;
	}
	return std::nullopt;
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
	transitionEvents_.clear();
	transitionMeta_ = {};
}

void SceneController::update(double nowSeconds) {
	pollAutoTransition(nowSeconds);

	if (!transition_.active) {
		blend_ = 0.0f;
		return;
	}

	const double elapsed = nowSeconds - transition_.startTime;
	if (elapsed >= fadeDuration_ - kEpsilon) {
		const SceneState previous = transition_.from;
		const bool manual = transitionMeta_.manual;
		const std::string reason = transitionMeta_.triggerReason;

		currentState_ = transition_.to;
		stateEnteredAt_ = nowSeconds;
		transition_.active = false;
		blend_ = 0.0f;

		TransitionEvent event;
		event.from = previous;
		event.to = currentState_;
		event.manual = manual;
		event.completed = true;
		event.triggerReason = reason;
		event.timestamp = nowSeconds;
		event.timeInState = elapsed;
		event.blendDuration = fadeDuration_;
		transitionEvents_.push_back(event);

		transitionMeta_ = {};
		return;
	}

	const double ratio = std::clamp(elapsed / fadeDuration_, 0.0, 1.0);
	blend_ = static_cast<float>(ratio);
}

bool SceneController::requestState(SceneState target, double nowSeconds, bool manualRequest,
                                   const std::string& triggerReason) {
	return startTransition(target, nowSeconds, manualRequest, triggerReason);
}

void SceneController::setTimingConfig(std::shared_ptr<const SceneTimingConfig> timingConfig) {
	timingConfig_ = std::move(timingConfig);
}

std::optional<SceneController::TransitionEvent> SceneController::popTransitionEvent() {
	if (transitionEvents_.empty()) {
		return std::nullopt;
	}
	TransitionEvent event = transitionEvents_.front();
	transitionEvents_.erase(transitionEvents_.begin());
	return event;
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

bool SceneController::canTransition(SceneState from, SceneState to, bool manualRequest) const noexcept {
	if (!manualRequest) {
		return true;
	}

	switch (from) {
		case SceneState::Idle:
			return to == SceneState::Start;
		case SceneState::Start:
			return false;
		case SceneState::FirstPhase:
			return to == SceneState::Exchange || to == SceneState::End;
		case SceneState::Exchange:
			return to == SceneState::Mixed || to == SceneState::End;
		case SceneState::Mixed:
			return to == SceneState::End;
		case SceneState::End:
			return false;
	}
	return false;
}

bool SceneController::startTransition(SceneState target, double nowSeconds, bool manualRequest,
                                      const std::string& triggerReason) {
	if (target == currentState_) {
		return false;
	}

	if (transition_.active) {
		if (target == transition_.to) {
			return false;
		}
		return false;
	}

	if (!canTransition(currentState_, target, manualRequest)) {
		return false;
	}

	transition_.from = currentState_;
	transition_.to = target;
	transition_.startTime = nowSeconds;
	transition_.active = true;
	blend_ = 0.0f;

	transitionMeta_.manual = manualRequest;
	transitionMeta_.triggerReason = triggerReason;
	transitionMeta_.requestedAt = nowSeconds;
	transitionMeta_.timeInState = nowSeconds - stateEnteredAt_;

	TransitionEvent event;
	event.from = transition_.from;
	event.to = target;
	event.manual = manualRequest;
	event.completed = false;
	event.triggerReason = triggerReason;
	event.timestamp = nowSeconds;
	event.timeInState = transitionMeta_.timeInState;
	event.blendDuration = fadeDuration_;
	transitionEvents_.push_back(event);

	return true;
}

void SceneController::pollAutoTransition(double nowSeconds) {
	if (!timingConfig_ || transition_.active) {
		return;
	}
	const auto durationOpt = timingConfig_->effectiveDuration(currentState_);
	if (!durationOpt.has_value()) {
		return;
	}
	const double elapsed = nowSeconds - stateEnteredAt_;
	if (elapsed + kEpsilon < *durationOpt) {
		return;
	}
	const auto* sceneCfg = timingConfig_->find(currentState_);
	if (sceneCfg == nullptr || !sceneCfg->transitionTo.has_value()) {
		return;
	}
	startTransition(*sceneCfg->transitionTo, nowSeconds, false, "timeout");
}
