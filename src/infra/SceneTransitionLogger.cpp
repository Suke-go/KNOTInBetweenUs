#include "infra/SceneTransitionLogger.h"

#include "ofMain.h"

#include <iomanip>
#include <sstream>

namespace infra {

namespace {

std::string optionalToString(const std::optional<double>& value) {
	if (!value.has_value()) {
		return "null";
	}
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << *value;
	return oss.str();
}

std::filesystem::path makeAbsolute(const std::filesystem::path& path) {
	if (path.is_absolute()) {
		return path;
	}
	return std::filesystem::absolute(std::filesystem::current_path() / path);
}

}  // namespace

void SceneTransitionLogger::setup(const std::filesystem::path& csvPath) {
	csvPath_ = makeAbsolute(csvPath);
	std::error_code ec;
	std::filesystem::create_directories(csvPath_.parent_path(), ec);
	if (ec) {
		ofLogWarning("SceneTransitionLogger") << "Failed to create directory: " << ec.message();
	}
	openIfNeeded();
	ensureHeader();
}

void SceneTransitionLogger::recordTransition(const TransitionRecord& record) {
	buffer_.push_back(record);
	openIfNeeded();
	ensureHeader();
	if (!stream_.is_open()) {
		return;
	}
	for (const auto& entry : buffer_) {
		stream_ << entry.timestampMicros << ','
		        << sceneStateToString(entry.sceneFrom) << ','
		        << sceneStateToString(entry.sceneTo) << ','
		        << entry.transitionType << ','
		        << entry.triggerReason << ','
		        << std::fixed << std::setprecision(3) << entry.timeInStateSec << ','
		        << optionalToString(entry.expectedDurationSec) << ','
		        << optionalToString(entry.deviationSec) << ','
		        << std::fixed << std::setprecision(3) << entry.blendDurationSec << ','
		        << (entry.completed ? "1" : "0") << '\n';
	}
	buffer_.clear();
	stream_.flush();
}

void SceneTransitionLogger::flush() {
	if (stream_.is_open()) {
		stream_.flush();
	}
	buffer_.clear();
}

void SceneTransitionLogger::openIfNeeded() {
	if (stream_.is_open()) {
		return;
	}
	const bool existed = std::filesystem::exists(csvPath_);
	const auto initialSize = existed ? std::filesystem::file_size(csvPath_) : 0;
	stream_.open(csvPath_, std::ios::app);
	if (!stream_) {
		ofLogError("SceneTransitionLogger") << "Failed to open csv: " << csvPath_;
		return;
	}
	headerWritten_ = (initialSize > 0);
}

void SceneTransitionLogger::ensureHeader() {
	if (!stream_.is_open() || headerWritten_) {
		return;
	}
	stream_ << "timestampMicros,sceneFrom,sceneTo,transitionType,triggerReason,timeInStateSec,"
	           "expectedDurationSec,deviationSec,blendDurationSec,completed\n";
	headerWritten_ = true;
}

}  // namespace infra
