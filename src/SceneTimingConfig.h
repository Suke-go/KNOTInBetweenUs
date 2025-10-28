#pragma once

#include "SceneController.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

class SceneTimingConfig {
  public:
	struct Stage {
		std::string name;
		double startAt = 0.0;
		double duration = 0.0;
	};

	struct SceneConfig {
		std::optional<double> autoDuration;
		std::vector<Stage> stages;
		std::optional<SceneState> transitionTo;
		std::optional<double> idleReturnDelay;
	};

	static SceneTimingConfig load(const std::filesystem::path& relativePath);

	[[nodiscard]] bool testModeEnabled() const noexcept { return testModeEnabled_; }
	[[nodiscard]] double testScaleFactor() const noexcept { return testScaleFactor_; }
	[[nodiscard]] const SceneConfig* find(SceneState state) const noexcept;
	[[nodiscard]] const Stage* findStage(SceneState state, const std::string& name) const noexcept;
	[[nodiscard]] std::optional<double> effectiveDuration(SceneState state) const noexcept;

  private:
	std::map<SceneState, SceneConfig> scenes_;
	bool testModeEnabled_ = false;
	double testScaleFactor_ = 1.0;
};
