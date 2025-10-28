#pragma once

#include "ofMain.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace infra {

struct TelemetryFrame {
	uint64_t timestampMicros = 0;
	float bpm = 0.0f;
	float envelopePeak = 0.0f;
	std::string sceneId;
};

struct HapticEventFrame {
	uint64_t timestampMicros = 0;
	std::string label;
	float intensity = 0.0f;
};

struct TelemetryConfig {
	std::filesystem::path sessionCsvPath;
	std::filesystem::path summaryJsonPath;
	std::filesystem::path hapticCsvPath;
	uint32_t writeIntervalMs = 250;
	uint32_t flushIntervalMs = 1000;
};

struct GuiConfig {
	bool showControlPanel = true;
	bool showStatusPanel = true;
	bool allowKeyboardToggle = true;
	std::string keyboardToggleKey = "g";
	double keyboardToggleHoldTime = 0.0;
	bool allowCornerUnlock = false;
};

struct AppConfig {
	TelemetryConfig telemetry;
	std::filesystem::path calibrationPath;
	std::filesystem::path calibrationReportCsvPath;
	std::filesystem::path sessionSeedPath;
	bool enableSyntheticTelemetry = false;
	std::string defaultScene = "Idle";
	std::string operationMode = "debug";
	float inputGainDb = 0.0f;
	GuiConfig gui;
	std::filesystem::path sceneTimingConfigPath;
	std::filesystem::path sceneTransitionCsvPath;
};

class AppConfigLoader {
  public:
	AppConfig load(const std::filesystem::path& configRelativePath) const;

  private:
	static ofJson loadOrCreateDefault(const std::filesystem::path& absolutePath);
	static ofJson makeDefaultConfig(const std::filesystem::path& absolutePath);
};

class SummaryAggregator {
  public:
	void reset();
	void ingest(const TelemetryFrame& frame);
	[[nodiscard]] ofJson buildSummaryJson() const;

  private:
	std::vector<double> bpmSamples_;
	std::vector<double> rrIntervalsMs_;
	uint64_t firstTimestampMicros_ = 0;
	uint64_t lastTimestampMicros_ = 0;
	std::optional<std::chrono::system_clock::time_point> wallClockStart_;
	std::optional<std::chrono::system_clock::time_point> wallClockEnd_;
};

class SessionLogger {
  public:
	SessionLogger(const TelemetryConfig& config, bool consoleEcho);
	~SessionLogger();

	SessionLogger(const SessionLogger&) = delete;
	SessionLogger& operator=(const SessionLogger&) = delete;
	SessionLogger(SessionLogger&&) = delete;
	SessionLogger& operator=(SessionLogger&&) = delete;

	void append(const TelemetryFrame& frame);
	void flushIfDue(uint64_t nowMicros);
	void writeSummary();

  private:
	void rotateIfNeeded();
	void openCsv();
	void ensureHeader();
	void flush();

	bool consoleEcho_ = false;
	TelemetryConfig config_;
	std::ofstream csv_;
	uint64_t lastFlushMicros_ = 0;
	bool headerWritten_ = false;
	SummaryAggregator aggregator_;
};

class HapticEventLogger {
  public:
	explicit HapticEventLogger(const std::filesystem::path& csvPath);
	~HapticEventLogger();

	HapticEventLogger(const HapticEventLogger&) = delete;
	HapticEventLogger& operator=(const HapticEventLogger&) = delete;
	HapticEventLogger(HapticEventLogger&&) = delete;
	HapticEventLogger& operator=(HapticEventLogger&&) = delete;

	void append(const HapticEventFrame& frame);

  private:
	void rotateIfNeeded();
	void openCsv();
	void ensureHeader();

	std::filesystem::path csvPath_;
	std::ofstream csv_;
	bool headerWritten_ = false;
};

}  // namespace infra
