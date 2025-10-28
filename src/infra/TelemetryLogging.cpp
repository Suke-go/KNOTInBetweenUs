#include "infra/TelemetryLogging.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::filesystem::path makeAbsolute(const std::filesystem::path& path) {
	if (path.is_absolute()) {
		return path;
	}
	return std::filesystem::absolute(std::filesystem::current_path() / path);
}

std::string makeTimestampSuffix() {
	const auto now = std::chrono::system_clock::now();
	const auto tt = std::chrono::system_clock::to_time_t(now);
	std::tm tm {};
#if defined(_WIN32)
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
	return oss.str();
}

double computeMean(const std::vector<double>& data) {
	if (data.empty()) {
		return 0.0;
	}
	double sum = 0.0;
	for (double value : data) {
		sum += value;
	}
	return sum / static_cast<double>(data.size());
}

double computeStddev(const std::vector<double>& data, double mean) {
	if (data.size() < 2) {
		return 0.0;
	}
	double accum = 0.0;
	for (double value : data) {
		const double diff = value - mean;
		accum += diff * diff;
	}
	return std::sqrt(accum / static_cast<double>(data.size() - 1));
}

double computeRmssd(const std::vector<double>& data) {
	if (data.size() < 2) {
		return 0.0;
	}
	double accum = 0.0;
	for (size_t i = 1; i < data.size(); ++i) {
		const double diff = data[i] - data[i - 1];
		accum += diff * diff;
	}
	return std::sqrt(accum / static_cast<double>(data.size() - 1));
}

std::string toIso8601(const std::chrono::system_clock::time_point& tp) {
	const auto tt = std::chrono::system_clock::to_time_t(tp);
	std::tm tm {};
#if defined(_WIN32)
	gmtime_s(&tm, &tt);
#else
	gmtime_r(&tt, &tm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return oss.str();
}

}  // namespace

namespace infra {

AppConfig AppConfigLoader::load(const std::filesystem::path& configRelativePath) const {
	const auto absolutePath = std::filesystem::path(ofToDataPath(configRelativePath.string(), true));
	const ofJson json = loadOrCreateDefault(absolutePath);

	AppConfig config;
	const auto telemetryJson = json.value("telemetry", ofJson::object());
	config.telemetry.sessionCsvPath = makeAbsolute(std::filesystem::path(telemetryJson.value("sessionCsv", "../logs/proto_session.csv")));
	config.telemetry.summaryJsonPath = makeAbsolute(std::filesystem::path(telemetryJson.value("summaryJson", "../logs/proto_summary.json")));
	config.telemetry.hapticCsvPath = makeAbsolute(std::filesystem::path(telemetryJson.value("hapticCsv", "../logs/haptic_events.csv")));
	config.telemetry.writeIntervalMs = telemetryJson.value("writeIntervalMs", 250);
	config.telemetry.flushIntervalMs = telemetryJson.value("flushIntervalMs", 1000);

	config.calibrationPath = makeAbsolute(std::filesystem::path(json.value("calibrationPath", "../calibration/channel_separator.json")));
	config.calibrationReportCsvPath =
		makeAbsolute(std::filesystem::path(json.value("calibrationReportCsv", "../logs/calibration_report.csv")));
	config.sessionSeedPath = makeAbsolute(std::filesystem::path(json.value("sessionSeed", "config/session_seed.json")));
	config.enableSyntheticTelemetry = json.value("enableSyntheticTelemetry", false);
	config.defaultScene = json.value("defaultScene", "Idle");
	return config;
}

ofJson AppConfigLoader::loadOrCreateDefault(const std::filesystem::path& absolutePath) {
	std::error_code ec;
	std::filesystem::create_directories(absolutePath.parent_path(), ec);
	if (std::filesystem::exists(absolutePath)) {
		try {
			return ofLoadJson(absolutePath.string());
		} catch (const std::exception& ex) {
			ofLogError("AppConfigLoader") << "Failed to parse config: " << absolutePath << " reason: " << ex.what();
		}
	}
	const ofJson def = makeDefaultConfig(absolutePath);
	ofSavePrettyJson(absolutePath.string(), def);
	return def;
}

ofJson AppConfigLoader::makeDefaultConfig(const std::filesystem::path& absolutePath) {
	ofLogWarning("AppConfigLoader") << "Creating default config at " << absolutePath;
		return ofJson{
			{"telemetry",
			 {
				 {"sessionCsv", "../logs/proto_session.csv"},
				 {"summaryJson", "../logs/proto_summary.json"},
				 {"hapticCsv", "../logs/haptic_events.csv"},
				 {"writeIntervalMs", 250},
				 {"flushIntervalMs", 1000},
			 }},
			{"calibrationPath", "../calibration/channel_separator.json"},
			{"calibrationReportCsv", "../logs/calibration_report.csv"},
			{"sessionSeed", "config/session_seed.json"},
			{"enableSyntheticTelemetry", false},
			{"defaultScene", "Idle"},
		};
}

void SummaryAggregator::reset() {
	bpmSamples_.clear();
	rrIntervalsMs_.clear();
	firstTimestampMicros_ = 0;
	lastTimestampMicros_ = 0;
	wallClockStart_.reset();
	wallClockEnd_.reset();
}

void SummaryAggregator::ingest(const TelemetryFrame& frame) {
	if (firstTimestampMicros_ == 0) {
		firstTimestampMicros_ = frame.timestampMicros;
		wallClockStart_ = std::chrono::system_clock::now();
	}
	lastTimestampMicros_ = frame.timestampMicros;
	wallClockEnd_ = std::chrono::system_clock::now();

	if (frame.bpm > 0.1f && frame.bpm < 260.0f) {
		bpmSamples_.push_back(static_cast<double>(frame.bpm));
		const double rrMs = 60000.0 / std::max(1.0f, frame.bpm);
		rrIntervalsMs_.push_back(rrMs);
	}
}

ofJson SummaryAggregator::buildSummaryJson() const {
	const double avgBpm = computeMean(bpmSamples_);
	const double sdnn = computeStddev(rrIntervalsMs_, computeMean(rrIntervalsMs_));
	const double rmssd = computeRmssd(rrIntervalsMs_);
	const double durationSec =
		(lastTimestampMicros_ > firstTimestampMicros_) ? static_cast<double>(lastTimestampMicros_ - firstTimestampMicros_) / 1'000'000.0 : 0.0;

	ofJson summary = {
		{"sampleCount", bpmSamples_.size()},
		{"avgBpm", avgBpm},
		{"sdnnMs", sdnn},
		{"rmssdMs", rmssd},
		{"durationSec", durationSec},
		{"timestampMicros",
		 {
			 {"start", firstTimestampMicros_},
			 {"end", lastTimestampMicros_},
		 }},
	};

	if (wallClockStart_.has_value() && wallClockEnd_.has_value()) {
		summary["wallClockUtc"] = {
			{"start", toIso8601(*wallClockStart_)},
			{"end", toIso8601(*wallClockEnd_)},
		};
	}

	return summary;
}

SessionLogger::SessionLogger(const TelemetryConfig& config, bool consoleEcho)
	: consoleEcho_(consoleEcho)
	, config_(config) {
	rotateIfNeeded();
	openCsv();
	ensureHeader();
	aggregator_.reset();
}

SessionLogger::~SessionLogger() {
	try {
		writeSummary();
		flush();
	} catch (const std::exception& ex) {
		ofLogError("SessionLogger") << "Destructor caught exception: " << ex.what();
	}
	if (csv_.is_open()) {
		csv_.close();
	}
}

void SessionLogger::append(const TelemetryFrame& frame) {
	if (!csv_.is_open()) {
		return;
	}
	ensureHeader();
	csv_ << frame.timestampMicros << "," << frame.bpm << "," << frame.envelopePeak << "," << frame.sceneId << "\n";
	if (consoleEcho_) {
		ofLogNotice("SessionLogger") << "Telemetry " << frame.sceneId << " bpm=" << frame.bpm << " env=" << frame.envelopePeak;
	}
	aggregator_.ingest(frame);
}

void SessionLogger::flushIfDue(uint64_t nowMicros) {
	const uint64_t flushIntervalMicros = static_cast<uint64_t>(config_.flushIntervalMs) * 1000ULL;
	if (flushIntervalMicros == 0) {
		flush();
		return;
	}

	if (nowMicros - lastFlushMicros_ >= flushIntervalMicros) {
		flush();
		lastFlushMicros_ = nowMicros;
	}
}

void SessionLogger::writeSummary() {
	const ofJson json = aggregator_.buildSummaryJson();
	const auto parent = config_.summaryJsonPath.parent_path();
	std::error_code ec;
	std::filesystem::create_directories(parent, ec);
	ofSavePrettyJson(config_.summaryJsonPath.string(), json);
}

void SessionLogger::rotateIfNeeded() {
	std::error_code ec;
	const auto& target = config_.sessionCsvPath;
	if (!std::filesystem::exists(target)) {
		return;
	}
	const auto size = std::filesystem::file_size(target, ec);
	if (ec || size == 0) {
		return;
	}
	const auto backup = target.parent_path() / (target.stem().string() + "_" + makeTimestampSuffix() + target.extension().string());
	std::filesystem::rename(target, backup, ec);
	if (ec) {
		ofLogError("SessionLogger") << "Failed to rotate log " << target << " -> " << backup << " reason: " << ec.message();
	}
}

void SessionLogger::openCsv() {
	const auto parent = config_.sessionCsvPath.parent_path();
	std::error_code ec;
	std::filesystem::create_directories(parent, ec);
	csv_.open(config_.sessionCsvPath, std::ios::out | std::ios::app);
	if (!csv_.is_open()) {
		ofLogError("SessionLogger") << "Failed to open CSV " << config_.sessionCsvPath;
		return;
	}
	headerWritten_ = std::filesystem::exists(config_.sessionCsvPath) && std::filesystem::file_size(config_.sessionCsvPath) > 0;
}

void SessionLogger::ensureHeader() {
	if (headerWritten_ || !csv_.is_open()) {
		return;
	}
	csv_ << "timestampMicros,bpm,envelopePeak,sceneId\n";
	headerWritten_ = true;
}

void SessionLogger::flush() {
	if (csv_.is_open()) {
		csv_.flush();
	}
}

HapticEventLogger::HapticEventLogger(const std::filesystem::path& csvPath)
	: csvPath_(makeAbsolute(csvPath)) {
	rotateIfNeeded();
	openCsv();
	ensureHeader();
}

HapticEventLogger::~HapticEventLogger() {
	if (csv_.is_open()) {
		csv_.close();
	}
}

void HapticEventLogger::append(const HapticEventFrame& frame) {
	if (!csv_.is_open()) {
		return;
	}
	ensureHeader();
	csv_ << frame.timestampMicros << "," << frame.label << "," << frame.intensity << "\n";
}

void HapticEventLogger::rotateIfNeeded() {
	std::error_code ec;
	if (!std::filesystem::exists(csvPath_)) {
		return;
	}
	const auto size = std::filesystem::file_size(csvPath_, ec);
	if (ec || size == 0) {
		return;
	}
	const auto backup = csvPath_.parent_path() / (csvPath_.stem().string() + "_" + makeTimestampSuffix() + csvPath_.extension().string());
	std::filesystem::rename(csvPath_, backup, ec);
	if (ec) {
		ofLogError("HapticEventLogger") << "Failed to rotate log " << csvPath_ << " -> " << backup << " reason: " << ec.message();
	}
}

void HapticEventLogger::openCsv() {
	std::error_code ec;
	std::filesystem::create_directories(csvPath_.parent_path(), ec);
	csv_.open(csvPath_, std::ios::out | std::ios::app);
	if (!csv_.is_open()) {
		ofLogError("HapticEventLogger") << "Failed to open log file " << csvPath_;
		return;
	}
	headerWritten_ = std::filesystem::exists(csvPath_) && std::filesystem::file_size(csvPath_) > 0;
}

void HapticEventLogger::ensureHeader() {
	if (headerWritten_ || !csv_.is_open()) {
		return;
	}
	csv_ << "timestampMicros,label,intensity\n";
	headerWritten_ = true;
}

}  // namespace infra
