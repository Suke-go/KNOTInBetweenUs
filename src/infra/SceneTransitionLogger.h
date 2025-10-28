#pragma once

#include "SceneController.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace infra {

class SceneTransitionLogger {
  public:
	struct TransitionRecord {
		uint64_t timestampMicros = 0;
		SceneState sceneFrom = SceneState::Idle;
		SceneState sceneTo = SceneState::Idle;
		std::string transitionType;   // manual / auto / error
		std::string triggerReason;    // button_press / timeout / ...
		double timeInStateSec = 0.0;
		std::optional<double> expectedDurationSec;
		std::optional<double> deviationSec;
		double blendDurationSec = 0.0;
		bool completed = false;
	};

	void setup(const std::filesystem::path& csvPath);
	void recordTransition(const TransitionRecord& record);
	void flush();

  private:
	void openIfNeeded();
	void ensureHeader();

	std::filesystem::path csvPath_;
	std::ofstream stream_;
	bool headerWritten_ = false;
	std::vector<TransitionRecord> buffer_;
};

}  // namespace infra
