#include "SceneTimingConfig.h"

#include "ofMain.h"

namespace {

std::filesystem::path resolveDataPath(const std::filesystem::path& relativePath) {
	std::filesystem::path dataPath(ofToDataPath(relativePath.string(), true));
	if (std::filesystem::exists(dataPath)) {
		return dataPath;
	}
	if (relativePath.is_absolute()) {
		return relativePath;
	}
	return std::filesystem::absolute(std::filesystem::current_path() / relativePath);
}

SceneTimingConfig::Stage parseStage(const ofJson& stageJson) {
	SceneTimingConfig::Stage stage;
	stage.name = stageJson.value("name", "");
	stage.startAt = stageJson.value("startAt", 0.0);
	stage.duration = stageJson.value("duration", 0.0);
	return stage;
}

std::optional<SceneState> parseSceneState(const ofJson& jsonValue) {
	if (jsonValue.is_string()) {
		return sceneStateFromString(jsonValue.get<std::string>());
	}
	return std::nullopt;
}

}  // namespace

SceneTimingConfig SceneTimingConfig::load(const std::filesystem::path& relativePath) {
	SceneTimingConfig config;
	const auto absolutePath = resolveDataPath(relativePath);

	if (!std::filesystem::exists(absolutePath)) {
		ofLogWarning("SceneTimingConfig") << "Config not found: " << absolutePath;
		return config;
	}

	ofJson json;
	try {
		json = ofLoadJson(absolutePath.string());
	} catch (const std::exception& ex) {
		ofLogError("SceneTimingConfig") << "Failed to parse config: " << ex.what();
		return config;
	}

	const auto scenesIt = json.find("scenes");
	if (scenesIt != json.end() && scenesIt->is_object()) {
		for (const auto& [key, value] : scenesIt->items()) {
			const auto stateOpt = sceneStateFromString(key);
			if (!stateOpt.has_value()) {
				ofLogWarning("SceneTimingConfig") << "Unknown scene key: " << key;
				continue;
			}

			SceneConfig scene;
			if (value.contains("autoDuration") && !value["autoDuration"].is_null()) {
				scene.autoDuration = value["autoDuration"].get<double>();
			}

			if (value.contains("stages") && value["stages"].is_array()) {
				for (const auto& stageJson : value["stages"]) {
					scene.stages.push_back(parseStage(stageJson));
				}
			}

			if (value.contains("transitionTo")) {
				scene.transitionTo = parseSceneState(value["transitionTo"]);
			}

			if (value.contains("idleReturnDelay") && !value["idleReturnDelay"].is_null()) {
				scene.idleReturnDelay = value["idleReturnDelay"].get<double>();
			}

			config.scenes_.emplace(*stateOpt, std::move(scene));
		}
	}

	if (json.contains("testMode") && json["testMode"].is_object()) {
		const auto& testMode = json["testMode"];
		config.testModeEnabled_ = testMode.value("enabled", false);
		const double scale = testMode.value("scaleFactor", 1.0);
		config.testScaleFactor_ = (scale > 0.0) ? scale : 1.0;
	}

	return config;
}

const SceneTimingConfig::SceneConfig* SceneTimingConfig::find(SceneState state) const noexcept {
	const auto it = scenes_.find(state);
	if (it == scenes_.end()) {
		return nullptr;
	}
	return &it->second;
}

const SceneTimingConfig::Stage* SceneTimingConfig::findStage(SceneState state, const std::string& name) const noexcept {
	const auto* scene = find(state);
	if (scene == nullptr) {
		return nullptr;
	}
	for (const auto& stage : scene->stages) {
		if (stage.name == name) {
			return &stage;
		}
	}
	return nullptr;
}

std::optional<double> SceneTimingConfig::effectiveDuration(SceneState state) const noexcept {
	const auto* config = find(state);
	if (config == nullptr || !config->autoDuration.has_value()) {
		return std::nullopt;
	}
	double duration = *config->autoDuration;
	if (testModeEnabled_) {
		duration *= testScaleFactor_;
	}
	return duration;
}
