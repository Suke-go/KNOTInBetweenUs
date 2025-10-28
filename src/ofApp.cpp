#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace {

constexpr double kEnvelopeSampleIntervalSec = 0.05;  // 50 ms

float easedBlend(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return static_cast<float>(0.5 - 0.5 * std::cos(clamped * glm::pi<float>()));
}

float safeLerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

std::string sceneToString(SceneState state) {
    switch (state) {
        case SceneState::Idle:
            return "Idle";
        case SceneState::FirstPhase:
            return "FirstPhase";
        case SceneState::End:
            return "End";
    }
    return "Unknown";
}

}  // namespace

void ofApp::setup() {
    ofSetVerticalSync(true);
    ofSetFrameRate(60);

    infra::AppConfigLoader loader;
    appConfig_ = loader.load("config/app_config.json");

    sessionLogger_ = std::make_unique<infra::SessionLogger>(appConfig_.telemetry, false);
    hapticLogger_ = std::make_unique<infra::HapticEventLogger>(appConfig_.telemetry.hapticCsvPath);

    calibrationFilePath_ = appConfig_.calibrationPath;
    calibrationReportPath_ = appConfig_.calibrationReportCsvPath;
    sessionSeedPath_ = appConfig_.sessionSeedPath;
    if (calibrationReportPath_.empty()) {
        if (!calibrationFilePath_.empty()) {
            calibrationReportPath_ = calibrationFilePath_.parent_path() / "calibration_report.csv";
        } else {
            calibrationReportPath_ = std::filesystem::current_path() / "logs/calibration_report.csv";
        }
    }

    const double nowSeconds = ofGetElapsedTimef();
    sceneController_.setup(nowSeconds, 1.2);
    envelopeHistory_.setHorizon(30.0);
    latestMetrics_ = {};

    controlPanel_.setup("Session Control");
    controlPanel_.setPosition(20.0f, 20.0f);
    controlPanel_.add(sceneParam_.set("Scene", sceneToString(SceneState::Idle)));
    controlPanel_.add(bpmParam_.set("BPM", 0.0f, 0.0f, 240.0f));
    controlPanel_.add(envelopeParam_.set("Envelope", 0.0f, 0.0f, 1.0f));
    controlPanel_.add(hapticCountParam_.set("Haptic Count", 0U, 0U, 4096U));
    simulateTelemetry_ = appConfig_.enableSyntheticTelemetry;
    controlPanel_.add(simulateSignalParam_.set("Synthetic Signal", simulateTelemetry_));
    controlPanel_.add(startButton_.setup("Start First Phase"));
    controlPanel_.add(endButton_.setup("Trigger End"));
    controlPanel_.add(resetButton_.setup("Reset to Idle"));
    controlPanel_.add(envelopeCalibrationButton_.setup("Envelope Baseline 計測"));

    startButton_.addListener(this, &ofApp::onStartButtonPressed);
    endButton_.addListener(this, &ofApp::onEndButtonPressed);
    resetButton_.addListener(this, &ofApp::onResetButtonPressed);
    envelopeCalibrationButton_.addListener(this, &ofApp::onEnvelopeCalibrationButtonPressed);

    statusPanel_.setup("Monitor");
    statusPanel_.setPosition(controlPanel_.getPosition().x,
                             controlPanel_.getPosition().y + controlPanel_.getHeight() + 12.0f);
    statusPanel_.add(sceneOverviewParam_.set("シーン状態", sceneToString(sceneController_.currentState())));
    statusPanel_.add(timeInStateParam_.set("滞在時間", "0.0s"));
    statusPanel_.add(transitionProgressParam_.set("遷移進行度", 0.0f, 0.0f, 1.0f));
    statusPanel_.add(envelopeMonitorParam_.set("エンベロープ", 0.0f, 0.0f, 1.0f));
    statusPanel_.add(hapticRateParam_.set("ハプティクス/分", 0.0f, 0.0f, 240.0f));
    statusPanel_.add(calibrationStateParam_.set("キャリブレーション", makeCalibrationStatusText()));
    statusPanel_.add(limiterReductionParam_.set("リミッタ(dB)", 0.0f, -40.0f, 0.0f));
    statusPanel_.add(baselineEnvelopeParam_.set("包絡ベースライン", 0.0f, 0.0f, 2.0f));
    statusPanel_.add(envelopeCalibrationProgressParam_.set("包絡キャリブ進捗", 0.0f, 0.0f, 1.0f));
    statusPanel_.add(guidanceParam_.set("ガイダンス", "-"));

    sampleRate_ = 48000.0;
    bufferSize_ = 512;
    audioPipeline_.setup(sampleRate_, bufferSize_);
    audioPipeline_.loadCalibrationFile(calibrationFilePath_);
    initializeSessionSeed();
    calibrationSaved_ = audioPipeline_.calibrationReady();
    calibrationSaveAttempted_ = calibrationSaved_;
    calibrationReportAppended_ = false;

    if (!calibrationSaved_) {
        ensureParentDirectory(calibrationFilePath_);
        ofLogNotice("ofApp") << "Calibration file not ready. Starting calibration.";
        audioPipeline_.startCalibration();
    }

    ofSoundStreamSettings settings;
    settings.sampleRate = static_cast<unsigned int>(sampleRate_);
    settings.numInputChannels = 2;
    settings.numOutputChannels = 2;
    settings.bufferSize = bufferSize_;
    settings.setInListener(this);
    settings.setOutListener(this);

    try {
        soundStream_.setup(settings);
        soundStream_.start();
        soundStreamActive_ = true;
    } catch (const std::exception& ex) {
        ofLogError("ofApp") << "Failed to initialise sound stream: " << ex.what();
        soundStreamActive_ = false;
        simulateSignalParam_.set(true);
        simulateTelemetry_ = true;
    }

    sessionStartMicros_ = ofGetElapsedTimeMicros();
    lastTelemetryMicros_ = sessionStartMicros_;
    lastEnvelopeSampledAt_ = 0.0;
    lastSimulatedBeatAt_ = 0.0;
    beatCounter_ = 0;
    limiterReductionDbSmooth_ = 0.0f;
    lastStrongSignalAt_ = nowSeconds;
    weakSignalWarning_ = false;

    const std::string defaultSceneLower = ofToLower(appConfig_.defaultScene);
    if (defaultSceneLower == "firstphase") {
        sceneController_.requestState(SceneState::FirstPhase, nowSeconds);
    } else if (defaultSceneLower == "end") {
        sceneController_.requestState(SceneState::End, nowSeconds);
    }
}

void ofApp::update() {
    const uint64_t nowMicros = ofGetElapsedTimeMicros();
    const double nowSeconds = static_cast<double>(nowMicros) * 1e-6;

    sceneController_.update(nowSeconds);
    simulateTelemetry_ = simulateSignalParam_.get();

    if (audioPipeline_.isCalibrationActive()) {
        calibrationSaved_ = false;
        calibrationSaveAttempted_ = false;
        calibrationReportAppended_ = false;
    } else if (audioPipeline_.calibrationReady() && !calibrationSaved_) {
        if (!calibrationSaveAttempted_) {
            ensureParentDirectory(calibrationFilePath_);
            if (audioPipeline_.saveCalibrationFile(calibrationFilePath_)) {
                calibrationSaved_ = true;
                ofLogNotice("ofApp") << "Calibration saved to " << calibrationFilePath_;
            } else {
                ofLogWarning("ofApp") << "Failed to save calibration to " << calibrationFilePath_;
            }
            calibrationSaveAttempted_ = true;
        }
    }

    if (calibrationSaved_ && !calibrationReportAppended_) {
        appendCalibrationReport(audioPipeline_.calibrationResult(), lastEnvelopeCalibrationStats_);
        calibrationReportAppended_ = true;
    }

    updateEnvelopeCalibrationUi(nowSeconds);

    const bool calibrationActive = audioPipeline_.isCalibrationActive();
    const bool useSynthetic = simulateTelemetry_ || !soundStreamActive_ || calibrationActive;

    if (useSynthetic) {
        updateFakeSignal(nowSeconds);
        limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
    } else {
        const auto metrics = audioPipeline_.latestMetrics();
        const bool metricsAvailable =
            (metrics.timestampSec > 0.0) || (metrics.envelope > 0.0f) || (metrics.bpm > 0.0f);
        if (metricsAvailable) {
            applyBeatMetrics(metrics, nowSeconds);
            const auto events = audioPipeline_.pollBeatEvents();
            if (!events.empty()) {
                handleBeatEvents(events, nowSeconds);
            }
            limiterReductionDbSmooth_ =
                ofLerp(limiterReductionDbSmooth_, audioPipeline_.lastLimiterReductionDb(), 0.18f);
        } else {
            updateFakeSignal(nowSeconds);
            limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
        }
    }

    updateSceneGui(nowSeconds);
    calibrationStateParam_.set(makeCalibrationStatusText());
    limiterReductionParam_.set(limiterReductionDbSmooth_);

    const uint64_t intervalMicros =
        static_cast<uint64_t>(appConfig_.telemetry.writeIntervalMs) * 1000ULL;
    if (sessionLogger_ && intervalMicros > 0 && nowMicros - lastTelemetryMicros_ >= intervalMicros) {
        infra::TelemetryFrame frame;
        frame.timestampMicros = nowMicros;
        frame.bpm = latestMetrics_.bpm;
        frame.envelopePeak = latestMetrics_.envelope;
        frame.sceneId = sceneParam_.get();
        sessionLogger_->append(frame);
        lastTelemetryMicros_ = nowMicros;
    }

    if (sessionLogger_) {
        sessionLogger_->flushIfDue(nowMicros);
    }
}

void ofApp::draw() {
    ofBackground(10);
    const double nowSeconds = ofGetElapsedTimef();
    const SceneState current = sceneController_.currentState();
    const float blend = sceneController_.transitionBlend();
    drawScene(current, blend, nowSeconds);
    controlPanel_.draw();
    statusPanel_.setPosition(controlPanel_.getPosition().x,
                             controlPanel_.getPosition().y + controlPanel_.getHeight() + 12.0f);
    statusPanel_.draw();
    drawCalibrationStatus();
    drawBeatDebug();
}

void ofApp::exit() {
    startButton_.removeListener(this, &ofApp::onStartButtonPressed);
    endButton_.removeListener(this, &ofApp::onEndButtonPressed);
    resetButton_.removeListener(this, &ofApp::onResetButtonPressed);
    envelopeCalibrationButton_.removeListener(this, &ofApp::onEnvelopeCalibrationButtonPressed);

    if (soundStreamActive_) {
        try {
            soundStream_.stop();
            soundStream_.close();
        } catch (const std::exception& ex) {
            ofLogError("ofApp") << "Sound stream shutdown failed: " << ex.what();
        }
        soundStreamActive_ = false;
    }

    if (sessionLogger_) {
        sessionLogger_->writeSummary();
        sessionLogger_.reset();
    }
    hapticLogger_.reset();
}

void ofApp::keyPressed(int key) {
    switch (key) {
        case '1':
            onStartButtonPressed();
            break;
        case '2':
            onEndButtonPressed();
            break;
        case '0':
            onResetButtonPressed();
            break;
        case 't':
        case 'T':
            simulateSignalParam_.set(!simulateSignalParam_.get());
            break;
        case 'c':
        case 'C':
            ofLogNotice("ofApp") << "Manual calibration triggered.";
            audioPipeline_.startCalibration();
            calibrationSaved_ = false;
            calibrationSaveAttempted_ = false;
            break;
        case 's':
        case 'S':
            calibrationSaveAttempted_ = false;
            break;
        default:
            break;
    }
}

void ofApp::keyReleased(int) {}
void ofApp::mouseMoved(int, int) {}
void ofApp::mouseDragged(int, int, int) {}
void ofApp::mousePressed(int, int, int) {}
void ofApp::mouseReleased(int, int, int) {}
void ofApp::mouseScrolled(int, int, float, float) {}
void ofApp::mouseEntered(int, int) {}
void ofApp::mouseExited(int, int) {}
void ofApp::windowResized(int, int) {}
void ofApp::dragEvent(ofDragInfo) {}
void ofApp::gotMessage(ofMessage) {}

void ofApp::audioIn(ofSoundBuffer& input) {
    audioPipeline_.audioIn(input);
}

void ofApp::audioOut(ofSoundBuffer& output) {
    audioPipeline_.audioOut(output);
}

void ofApp::onStartButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "Start request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::FirstPhase, nowSeconds)) {
        ofLogNotice("ofApp") << "Start request ignored.";
    }
}

void ofApp::onEndButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "End request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::End, nowSeconds)) {
        ofLogNotice("ofApp") << "End request ignored.";
    }
}

void ofApp::onResetButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "Reset request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::Idle, nowSeconds)) {
        ofLogNotice("ofApp") << "Reset request ignored.";
    }
}

void ofApp::onEnvelopeCalibrationButtonPressed() {
    if (audioPipeline_.isCalibrationActive()) {
        ofLogNotice("ofApp") << "Envelope calibration ignored (channel calibration running).";
        return;
    }
    if (audioPipeline_.isEnvelopeCalibrationActive() || envelopeCalibrationRunning_) {
        ofLogNotice("ofApp") << "Envelope calibration already in progress.";
        return;
    }
    constexpr double kCalibrationDurationSec = 3.0;
    if (!soundStreamActive_) {
        ofLogWarning("ofApp") << "サウンドストリームが停止中です。実機入力で計測してください。";
    }
    audioPipeline_.startEnvelopeCalibration(kCalibrationDurationSec);
    envelopeCalibrationRunning_ = true;
    envelopeCalibrationProgressParam_.set(0.0f);
    baselineEnvelopeParam_.set(0.0f);
    ofLogNotice("ofApp") << "Starting envelope baseline measurement for " << kCalibrationDurationSec << "s";
    if (simulateTelemetry_) {
        ofLogWarning("ofApp") << "Synthetic signalが有効な状態で包絡キャリブを開始しました。実入力に切り替えることを推奨します。";
    }
}

void ofApp::updateSceneGui(double nowSeconds) {
    const SceneState current = sceneController_.currentState();
    const SceneState target = sceneController_.targetState();
    const bool transitioning = sceneController_.isTransitioning();

    std::string sceneLabel;
    if (transitioning) {
        std::ostringstream oss;
        oss << sceneToString(current) << " → " << sceneToString(target);
        oss << " (" << static_cast<int>(sceneController_.transitionBlend() * 100.0f) << "%)";
        sceneLabel = oss.str();
    } else {
        sceneLabel = sceneToString(current);
    }
    sceneParam_.set(sceneLabel);

    bpmParam_.set(latestMetrics_.bpm);
    envelopeParam_.set(latestMetrics_.envelope);
    hapticCountParam_.set(static_cast<std::uint32_t>(hapticLog_.entries().size()));

    const double horizon = std::clamp(sceneController_.timeInState(nowSeconds) * 1.2, 10.0, 45.0);
    envelopeHistory_.setHorizon(horizon);
    envelopeHistory_.prune(nowSeconds);

    sceneOverviewParam_.set(sceneLabel);
    const double timeInStateSec = sceneController_.timeInState(nowSeconds);
    timeInStateParam_.set(ofToString(timeInStateSec, 1) + "s");
    transitionProgressParam_.set(transitioning ? sceneController_.transitionBlend() : 0.0f);
    envelopeMonitorParam_.set(std::clamp(latestMetrics_.envelope, 0.0f, 1.0f));
    hapticRateParam_.set(computeHapticRatePerMinute(nowSeconds));

    if (latestMetrics_.envelope >= 0.18f) {
        lastStrongSignalAt_ = nowSeconds;
        weakSignalWarning_ = false;
    } else if (nowSeconds - lastStrongSignalAt_ > 3.0) {
        weakSignalWarning_ = true;
    }
    guidanceParam_.set(buildGuidanceMessage(nowSeconds));
}

void ofApp::updateEnvelopeHistory(double nowSeconds, float envelopeValue) {
    if (nowSeconds - lastEnvelopeSampledAt_ < kEnvelopeSampleIntervalSec) {
        return;
    }
    lastEnvelopeSampledAt_ = nowSeconds;
    envelopeHistory_.addSample(nowSeconds, envelopeValue, latestMetrics_.bpm);
}

void ofApp::updateFakeSignal(double nowSeconds) {
    const double phase = nowSeconds * 0.5;
    const float bpm = 64.0f + 6.0f * static_cast<float>(std::sin(phase * 0.6));
    const float env = ofClamp(0.52f + 0.42f * static_cast<float>(std::sin(phase)), 0.0f, 1.0f);

    latestMetrics_.timestampSec = nowSeconds;
    latestMetrics_.bpm = bpm;
    latestMetrics_.envelope = env;
    updateEnvelopeHistory(nowSeconds, env);

    const double beatIntervalSec = 60.0 / std::max(35.0f, bpm);
    if (nowSeconds - lastSimulatedBeatAt_ >= beatIntervalSec) {
        lastSimulatedBeatAt_ = nowSeconds;
        appendHapticEvent(nowSeconds, ofClamp(0.4f + 0.5f * static_cast<float>(std::sin(phase * 1.3)), 0.0f, 1.0f),
                          "synthetic");
    }
}

void ofApp::applyBeatMetrics(const knot::audio::AudioPipeline::BeatMetrics& metrics, double nowSeconds) {
    latestMetrics_.timestampSec = nowSeconds;
    latestMetrics_.bpm = metrics.bpm;
    latestMetrics_.envelope = metrics.envelope;
    updateEnvelopeHistory(nowSeconds, metrics.envelope);
}

void ofApp::handleBeatEvents(const std::vector<knot::audio::BeatEvent>& events, double nowSeconds) {
    for (const auto& evt : events) {
        if (evt.bpm > 1.0f) {
            latestMetrics_.bpm = evt.bpm;
        }
        const float intensity = ofClamp(evt.envelope, 0.2f, 1.0f);
        appendHapticEvent(nowSeconds, intensity, "detected");
    }
}

void ofApp::appendHapticEvent(double nowSeconds, float intensity, const std::string& label) {
    HapticEventLogEntry entry;
    entry.beatId = ++beatCounter_;
    entry.intensity = ofClamp(intensity, 0.0f, 1.0f);
    entry.holdMs = 140;
    entry.createdAtSec = nowSeconds;
    hapticLog_.push(entry);

    if (hapticLogger_) {
        infra::HapticEventFrame frame;
        frame.timestampMicros = static_cast<std::uint64_t>(nowSeconds * 1'000'000.0);
        frame.label = label;
        frame.intensity = entry.intensity;
        hapticLogger_->append(frame);
    }
}

void ofApp::updateEnvelopeCalibrationUi(double /*nowSeconds*/) {
    const bool active = audioPipeline_.isEnvelopeCalibrationActive();
    envelopeCalibrationRunning_ = active;
    if (active) {
        envelopeCalibrationProgressParam_.set(audioPipeline_.envelopeCalibrationProgress());
    } else {
        envelopeCalibrationProgressParam_.set(0.0f);
    }

    knot::audio::EnvelopeCalibrationStats stats;
    if (audioPipeline_.pollEnvelopeCalibrationStats(stats)) {
        lastEnvelopeCalibrationStats_ = stats;
        baselineEnvelopeParam_.set(std::clamp(stats.mean, 0.0f, 2.0f));
        logEnvelopeCalibrationResult(stats);
    } else if (lastEnvelopeCalibrationStats_) {
        baselineEnvelopeParam_.set(std::clamp(lastEnvelopeCalibrationStats_->mean, 0.0f, 2.0f));
    } else if (!active) {
        baselineEnvelopeParam_.set(0.0f);
    }
}

void ofApp::initializeSessionSeed() {
    if (sessionSeedPath_.empty()) {
        sessionSeed_ = 0;
        return;
    }

    ensureParentDirectory(sessionSeedPath_);

    if (std::filesystem::exists(sessionSeedPath_)) {
        try {
            const ofJson json = ofLoadJson(sessionSeedPath_.string());
            if (json.contains("seed")) {
                if (json["seed"].is_number_unsigned()) {
                    sessionSeed_ = json["seed"].get<std::uint64_t>();
                } else if (json["seed"].is_number_integer()) {
                    sessionSeed_ = static_cast<std::uint64_t>(json["seed"].get<long long>());
                }
            }
        } catch (const std::exception& ex) {
            ofLogWarning("ofApp") << "Failed to load session seed: " << sessionSeedPath_ << " reason: " << ex.what();
        }
    }

    if (sessionSeed_ == 0) {
        std::random_device rd;
        const std::uint64_t randomHigh = static_cast<std::uint64_t>(rd()) << 32;
        const std::uint64_t tick = static_cast<std::uint64_t>(ofGetElapsedTimeMicros());
        sessionSeed_ = randomHigh ^ tick;
        if (sessionSeed_ == 0) {
            sessionSeed_ = 1;  // ensure non-zero
        }

        ofJson json = {
            {"seed", sessionSeed_},
            {"createdUtc", ofGetTimestampString("%FT%TZ")},
            {"note", "auto-generated for reproducibility"},
        };

        try {
            ofSavePrettyJson(sessionSeedPath_.string(), json);
        } catch (const std::exception& ex) {
            ofLogWarning("ofApp") << "Failed to write session seed: " << sessionSeedPath_ << " reason: " << ex.what();
        }
    }

    audioPipeline_.setNoiseSeed(static_cast<std::uint32_t>(sessionSeed_ & 0xffffffffu));
}

bool ofApp::ensureParentDirectory(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        ofLogWarning("ofApp") << "Failed to create directory " << parent << ": " << ec.message();
        return false;
    }
    return true;
}

void ofApp::appendCalibrationReport(
    const std::array<knot::audio::ChannelCalibrationValue, 2>& values,
    const std::optional<knot::audio::EnvelopeCalibrationStats>& envelopeStats) {
    if (calibrationReportPath_.empty()) {
        return;
    }

    if (!ensureParentDirectory(calibrationReportPath_)) {
        return;
    }

    bool needsHeader = true;
    if (std::filesystem::exists(calibrationReportPath_)) {
        std::error_code sizeError;
        const auto fileSize = std::filesystem::file_size(calibrationReportPath_, sizeError);
        needsHeader = sizeError ? true : (fileSize == 0);
    }

    std::ofstream stream(calibrationReportPath_, std::ios::app);
    if (!stream.is_open()) {
        ofLogWarning("ofApp") << "Failed to open calibration report: " << calibrationReportPath_;
        return;
    }

    auto gainDb = [](float gain) -> double {
        if (gain <= 0.0f) {
            return -std::numeric_limits<double>::infinity();
        }
        return 20.0 * std::log10(static_cast<double>(gain));
    };

    const double gainDbCh1 = gainDb(values[0].gain);
    const double gainDbCh2 = gainDb(values[1].gain);
    const bool gainOkCh1 = std::isfinite(gainDbCh1) && std::abs(gainDbCh1) <= 3.0;
    const bool gainOkCh2 = std::isfinite(gainDbCh2) && std::abs(gainDbCh2) <= 3.0;
    const bool delayOkCh1 = std::abs(values[0].delaySamples) <= 2;
    const bool delayOkCh2 = std::abs(values[1].delaySamples) <= 2;

    auto okText = [](bool ok) { return ok ? "OK" : "NG"; };

    if (needsHeader) {
        stream << "timestampUtc,sessionSeed,sampleRateHz,"
               << "gainCh1,gainDbCh1,gainSpecCh1,delaySamplesCh1,delaySpecCh1,phaseDegCh1,"
               << "gainCh2,gainDbCh2,gainSpecCh2,delaySamplesCh2,delaySpecCh2,phaseDegCh2,"
               << "envelopeMean,envelopePeak,envelopeRatio,envelopeSpec\n";
    }

    stream << std::fixed << std::setprecision(6);

    const std::string timestamp = ofGetTimestampString("%FT%TZ");
    stream << timestamp << ',' << sessionSeed_ << ',' << sampleRate_ << ','
           << values[0].gain << ',' << gainDbCh1 << ',' << okText(gainOkCh1) << ','
           << values[0].delaySamples << ',' << okText(delayOkCh1) << ',' << values[0].phaseDeg << ','
           << values[1].gain << ',' << gainDbCh2 << ',' << okText(gainOkCh2) << ','
           << values[1].delaySamples << ',' << okText(delayOkCh2) << ',' << values[1].phaseDeg << ',';

    if (envelopeStats) {
        const float mean = envelopeStats->mean;
        const float peak = envelopeStats->peak;
        const float ratio = (mean > 1e-6f) ? (peak / mean) : 0.0f;
        const bool envOk = envelopeStats->valid && ratio >= 1.15f;
        stream << mean << ',' << peak << ',' << ratio << ',' << okText(envOk) << '\n';
        if (!envOk) {
            ofLogWarning("ofApp") << "Envelope calibration below target ratio: " << ratio
                                   << " (mean=" << mean << ", peak=" << peak << ")";
        }
    } else {
        stream << "NA,NA,NA,NA\n";
    }

    if (!(gainOkCh1 && gainOkCh2 && delayOkCh1 && delayOkCh2)) {
        ofLogWarning("ofApp") << "Calibration exceeds thresholds."
                               << " gainDbCh1=" << gainDbCh1
                               << " gainDbCh2=" << gainDbCh2
                               << " delayCh1=" << values[0].delaySamples
                               << " delayCh2=" << values[1].delaySamples;
    }
}

void ofApp::logEnvelopeCalibrationResult(const knot::audio::EnvelopeCalibrationStats& stats) {
    const float mean = stats.mean;
    const float peak = stats.peak;
    const float ratio = (mean > 1e-6f) ? (peak / mean) : 0.0f;
    ofLogNotice("ofApp") << "Envelope calibration completed."
                           << " mean=" << mean
                           << " peak=" << peak
                           << " ratio=" << ratio
                           << " valid=" << (stats.valid ? "true" : "false");

    if (stats.valid && ratio < 1.15f) {
        ofLogWarning("ofApp") << "Envelope ratio below recommended threshold. Consider再測定 or gain調整.";
    }

    appendCalibrationReport(audioPipeline_.calibrationResult(), stats);
}

std::string ofApp::makeCalibrationStatusText() const {
    std::ostringstream oss;
    if (audioPipeline_.isCalibrationActive()) {
        oss << "running";
    } else if (audioPipeline_.calibrationReady()) {
        oss << "ready";
        if (!calibrationSaved_) {
            oss << " (unsaved)";
        }
    } else {
        oss << "idle";
    }

    if (!calibrationReportPath_.empty()) {
        oss << " → " << calibrationReportPath_;
    }
    if (envelopeCalibrationRunning_) {
        oss << " | env=calibrating";
    } else if (lastEnvelopeCalibrationStats_) {
        const float ratio = (lastEnvelopeCalibrationStats_->mean > 1e-6f)
                                ? (lastEnvelopeCalibrationStats_->peak / lastEnvelopeCalibrationStats_->mean)
                                : 0.0f;
        oss << " | env=" << std::fixed << std::setprecision(3) << lastEnvelopeCalibrationStats_->mean
            << " (ratio=" << ratio << ')';
        oss << std::defaultfloat;
    }
    return oss.str();
}

void ofApp::drawScene(SceneState state, float alpha, double nowSeconds) {
    const auto drawLayer = [&](SceneState layerState, float layerAlpha) {
        switch (layerState) {
            case SceneState::Idle:
                drawIdleScene(layerAlpha, nowSeconds);
                break;
            case SceneState::FirstPhase:
                drawFirstPhaseScene(layerAlpha, nowSeconds);
                break;
            case SceneState::End:
                drawEndScene(layerAlpha);
                break;
        }
    };

    if (sceneController_.isTransitioning()) {
        const float blend = easedBlend(sceneController_.transitionBlend());
        drawLayer(sceneController_.currentState(), 1.0f - blend);
        drawLayer(sceneController_.targetState(), blend);
    } else {
        drawLayer(state, std::clamp(alpha, 0.0f, 1.0f));
    }
}

void ofApp::drawIdleScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    ofColor background(8, 9, 18);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    ofSetColor(220, 240, 255, static_cast<int>(clampedAlpha * 200.0f));
    ofDrawBitmapString("Idle — 入力監視中 / 環境整備フェーズ", 40, 80);

    const float envelope = latestMetrics_.envelope;
    const float baseRadius = 180.0f;
    const float radiusOffset = envelope * 240.0f;
    const float centerY = ofGetHeight() * 0.5f;

    ofNoFill();
    ofSetLineWidth(2.0f);
    ofColor rippleBase(120, 140, 255, static_cast<unsigned char>(safeLerp(20.0f, 90.0f, envelope)));

    for (int i = 0; i < 3; ++i) {
        const float falloff = std::max(0.0f, 1.0f - i * 0.28f);
        ofColor rippleColor = rippleBase;
        rippleColor.a = static_cast<unsigned char>(rippleColor.a * falloff * clampedAlpha);
        const float radius = baseRadius + radiusOffset + i * 36.0f;
        ofSetColor(rippleColor);
        ofDrawCircle(ofGetWidth() * 0.3f, centerY, radius);
        ofDrawCircle(ofGetWidth() * 0.7f, centerY, radius);
    }

    ofFill();
    ofColor starBase(255, 255, 255, static_cast<unsigned char>(clampedAlpha * 90.0f));
    const int starCount = 120;
    for (int i = 0; i < starCount; ++i) {
        const float u = static_cast<float>(i) / starCount;
        const float x = u * ofGetWidth();
        const float y = std::fmod(static_cast<float>(i) * 53.0f + static_cast<float>(nowSeconds) * 40.0f,
                                  static_cast<float>(ofGetHeight()));
        const float radius = 1.0f + 1.2f * std::sin(nowSeconds * 0.8 + i * 0.25f);
        ofColor starColor = starBase;
        starColor.a = static_cast<unsigned char>(
            starBase.a * (0.6f + 0.4f * std::sin(nowSeconds * 0.5f + i * 0.2f)));
        ofSetColor(starColor);
        ofDrawCircle(x, y, radius);
    }

    ofPopStyle();
}

void ofApp::drawFirstPhaseScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);

    ofColor background(12, 14, 28);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    ofSetColor(255, 255, 255, static_cast<int>(clampedAlpha * 220.0f));
    ofDrawBitmapString("First Phase — 心音提示＆同期フェーズ", 40, 80);

    const float envelope = latestMetrics_.envelope;
    const float pulseStrength = safeLerp(0.25f, 1.0f, envelope);

    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLE_FAN);

    const glm::vec2 center2D(ofGetWidth() * 0.5f, ofGetHeight() * 0.5f);
    const glm::vec3 center3D(center2D, 0.0f);
    mesh.addVertex(center3D);
    mesh.addColor(ofFloatColor(0.05f, 0.08f, 0.3f, 0.0f));

    const float radius = safeLerp(120.0f, 280.0f, envelope);
    const int segments = 180;
    for (int i = 0; i <= segments; ++i) {
        const float angle = ofDegToRad(static_cast<float>(i) / segments * 360.0f);
        const float wobble = 1.0f + 0.05f * std::sin(nowSeconds * 1.6f + i * 0.5f);
        const glm::vec2 point2D =
            center2D + glm::vec2(std::cos(angle), std::sin(angle)) * radius * wobble;
        mesh.addVertex(glm::vec3(point2D, 0.0f));
        mesh.addColor(ofFloatColor(
            0.3f,
            safeLerp(0.2f, 0.6f, pulseStrength),
            safeLerp(0.4f, 0.9f, pulseStrength),
            std::clamp(clampedAlpha * 0.35f, 0.0f, 1.0f)));
    }
    mesh.draw();

    ofSetColor(240, 160, 120, static_cast<int>(clampedAlpha * 180.0f));
    const float ringRadius = safeLerp(40.0f, 90.0f, envelope);
    const float ringWidth = safeLerp(6.0f, 18.0f, envelope);
    ofSetLineWidth(ringWidth);
    ofNoFill();
    ofDrawCircle(center2D, ringRadius);

    const float panelRight = controlPanel_.getPosition().x + controlPanel_.getWidth();
    const float margin = 24.0f;
    const float availableWidth = std::max(220.0f, static_cast<float>(ofGetWidth()) - panelRight - margin * 2.0f);
    const float graphHeight = std::min(260.0f, static_cast<float>(ofGetHeight()) * 0.35f);
    const ofRectangle graphArea(panelRight + margin, margin, availableWidth, graphHeight);
    drawEnvelopeGraph(graphArea);

    const float logHeight =
        std::max(160.0f, static_cast<float>(ofGetHeight()) - graphArea.getBottom() - margin * 2.0f);
    const ofRectangle logArea(panelRight + margin, graphArea.getBottom() + margin, availableWidth, logHeight);
    drawHapticLog(logArea, nowSeconds);

    ofPopStyle();
}

void ofApp::drawEndScene(float alpha) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    ofColor base(20, 20, 20);
    base.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(base);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    ofSetColor(240, 200, 200, static_cast<int>(clampedAlpha * 200.0f));
    ofDrawBitmapString("End — セッション終了。ログとサマリを確認してください。", 40, 80);
    ofPopStyle();
}

void ofApp::drawEnvelopeGraph(const ofRectangle& area) const {
    ofPushStyle();
    ofNoFill();
    ofSetColor(255, 255, 255, 160);
    ofDrawRectangle(area);

    const auto& samples = envelopeHistory_.samples();
    if (samples.size() < 2) {
        ofPopStyle();
        return;
    }

    const double start = samples.front().timestampSec;
    const double end = samples.back().timestampSec;
    const double span = std::max(end - start, 1.0);

    ofPolyline line;
    for (const auto& sample : samples) {
        const double norm = (sample.timestampSec - start) / span;
        const float x = static_cast<float>(area.x + norm * area.width);
        const float y = static_cast<float>(area.getBottom() - sample.envelope * area.height);
        line.addVertex(x, y);
    }
    ofSetColor(120, 230, 255, 200);
    line.draw();

    ofSetColor(255, 180);
    ofDrawBitmapString("エンベロープ (直近 " + ofToString(span, 1) + "s)", area.x + 4.0f, area.y + 16.0f);
    ofPopStyle();
}

void ofApp::drawHapticChart(const ofRectangle& area, double nowSeconds) const {
    ofPushStyle();
    ofFill();
    ofSetColor(24, 36, 58, 180);
    ofDrawRectangle(area);
    ofNoFill();
    ofSetColor(120, 140, 180, 200);
    ofDrawRectangle(area);

    const double windowSec = 10.0;
    const double startTime = nowSeconds - windowSec;

    const float baselineY = area.getBottom();
    ofSetColor(160, 170, 190, 160);
    ofDrawBitmapString("0.0", area.x + 4.0f, baselineY - 4.0f);

    const float thresholdIntensity = 0.30f;
    const float thresholdY = ofMap(thresholdIntensity, 0.0f, 1.0f, area.getBottom(), area.getTop(), true);
    ofSetColor(255, 120, 120, 140);
    ofDrawLine(area.x, thresholdY, area.getRight(), thresholdY);
    ofDrawBitmapString("推奨閾値 0.30", area.x + 4.0f, thresholdY - 4.0f);

    ofPolyline poly;
    poly.clear();

    const auto& entries = hapticLog_.entries();
    for (const auto& entry : entries) {
        if (entry.createdAtSec < startTime) {
            continue;
        }
        const double norm = std::clamp((entry.createdAtSec - startTime) / windowSec, 0.0, 1.0);
        const float x = area.x + static_cast<float>(norm) * area.width;
        const float y = ofMap(entry.intensity, 0.0f, 1.0f, area.getBottom(), area.getTop(), true);
        poly.addVertex(x, y);
    }

    if (poly.size() >= 2) {
        ofSetColor(255, 196, 120, 220);
        poly.draw();
    } else if (poly.size() == 1) {
        ofSetColor(255, 196, 120, 220);
        ofDrawCircle(poly.getVertices()[0], 3.0f);
    } else {
        ofSetColor(200, 210, 230, 180);
        ofDrawBitmapString("最近10秒のイベントなし", area.x + 8.0f, area.y + 18.0f);
    }

    ofPopStyle();
}

void ofApp::drawHapticLog(const ofRectangle& area, double nowSeconds) const {
    ofPushStyle();
    ofNoFill();
    ofSetColor(255, 255, 255, 140);
    ofDrawRectangle(area);

    ofSetColor(255, 230, 150);
    ofDrawBitmapString("ハプティクスイベント可視化", area.x + 6.0f, area.y + 18.0f);

    const float chartMargin = 8.0f;
    const float chartHeight = std::max(70.0f, area.height * 0.45f);
    ofRectangle chartArea(area.x + chartMargin, area.y + 26.0f,
                          area.width - chartMargin * 2.0f, chartHeight);
    drawHapticChart(chartArea, nowSeconds);

    const auto& entries = hapticLog_.entries();
    const float listTop = chartArea.getBottom() + 24.0f;

    if (entries.empty()) {
        ofSetColor(220, 220, 220);
        ofDrawBitmapString("ログなし (イベント未検出)", area.x + 6.0f, listTop);
        ofPopStyle();
        return;
    }

    float y = listTop;
    std::size_t drawn = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && y < area.getBottom() - 8.0f; ++it) {
        std::ostringstream oss;
        const double ageSec = std::max(0.0, nowSeconds - it->createdAtSec);
        oss << "拍#" << it->beatId
            << "  強度=" << std::fixed << std::setprecision(2) << it->intensity
            << "  維持=" << it->holdMs << "ms"
            << "  経過=" << std::fixed << std::setprecision(2) << ageSec << "s";
        ofSetColor(220, 240, 255, static_cast<int>(255.0f * (1.0f - std::min(drawn * 0.12f, 0.7f))));
        ofDrawBitmapString(oss.str(), area.x + 6.0f, y);
        y += 14.0f;
        ++drawn;
    }
    ofPopStyle();
}

bool ofApp::isInteractionLocked() const {
    return audioPipeline_.isCalibrationActive() || audioPipeline_.isEnvelopeCalibrationActive() || envelopeCalibrationRunning_;
}

float ofApp::computeHapticRatePerMinute(double nowSeconds) const {
    const double windowSec = 10.0;
    if (windowSec <= 0.0) {
        return 0.0f;
    }
    const double startTime = nowSeconds - windowSec;
    const auto& entries = hapticLog_.entries();
    int count = 0;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->createdAtSec < startTime) {
            break;
        }
        ++count;
    }
    if (count == 0) {
        return 0.0f;
    }
    return static_cast<float>(count * (60.0 / windowSec));
}

std::string ofApp::buildGuidanceMessage(double /*nowSeconds*/) const {
    if (!soundStreamActive_) {
        return "音声入出力が停止中です。デバイス選択と接続を確認してください。";
    }
    if (audioPipeline_.isCalibrationActive()) {
        return "キャリブレーション中です。測定完了までシーン操作は無効になります。";
    }
    if (envelopeCalibrationRunning_) {
        return "包絡キャリブレーションを実行中です。周囲を静かにして 3 秒ほどお待ちください。";
    }
    if (lastEnvelopeCalibrationStats_ && !lastEnvelopeCalibrationStats_->valid) {
        return "包絡ベースラインが取得できていません。再測定し、入力レベルを確認してください。";
    }
    if (lastEnvelopeCalibrationStats_ && lastEnvelopeCalibrationStats_->valid) {
        const float mean = lastEnvelopeCalibrationStats_->mean;
        const float ratio = (mean > 1e-6f)
                                ? (lastEnvelopeCalibrationStats_->peak / mean)
                                : 0.0f;
        if (ratio < 1.15f) {
            return "包絡比が低下しています。マイクゲインか胸ピースの固定を見直してください。";
        }
    }
    if (weakSignalWarning_) {
        return "心音信号が弱い可能性があります。マイク位置と胸ピース固定を確認してください。";
    }
    if (simulateTelemetry_) {
        return "シミュレーション信号を再生中です。実入力を確認するには Synthetic Signal を OFF にしてください。";
    }
    if (hapticRateParam_.get() < 30.0f && latestMetrics_.bpm > 0.0f) {
        return "ハプティクス出力が BPM に追従していません。BeatTimeline 設定とログを確認してください。";
    }
    return "正常稼働中です。KPI はステータスパネルを参照してください。";
}

void ofApp::drawCalibrationStatus() const {
    ofPushStyle();
    const float x = controlPanel_.getPosition().x;
    const float y = controlPanel_.getPosition().y + controlPanel_.getHeight() + 24.0f;

    struct Line {
        std::string text;
        ofColor color;
    };
    std::vector<Line> lines;

    if (soundStreamActive_) {
        lines.push_back({"オーディオ入出力: 稼働中 (48kHz/2ch)", ofColor(160, 240, 160)});
    } else {
        lines.push_back({"オーディオ入出力: 停止中 (シミュレーション再生)", ofColor(255, 160, 160)});
    }

    const bool calibrationActive = audioPipeline_.isCalibrationActive();
    const bool calibrationReady = audioPipeline_.calibrationReady();
    const std::string calStatus = makeCalibrationStatusText();

    if (calibrationActive) {
        lines.push_back({"キャリブレーション: 実行中 — GUI 操作は完了までロックされます", ofColor(255, 210, 120)});
    } else if (!calibrationReady || !calibrationSaved_) {
        lines.push_back({"キャリブレーション: 未保存/要再測定 — " + calStatus, ofColor(255, 180, 120)});
    } else {
        lines.push_back({"キャリブレーション: OK — " + calStatus, ofColor(160, 240, 160)});
    }

    if (envelopeCalibrationRunning_) {
        const float progress = envelopeCalibrationProgressParam_.get() * 100.0f;
        std::ostringstream oss;
        oss << "包絡ベースライン: 計測中 (" << std::fixed << std::setprecision(1) << progress << "%)";
        oss << std::defaultfloat;
        lines.push_back({oss.str(), ofColor(255, 210, 150)});
    } else if (lastEnvelopeCalibrationStats_) {
        const auto& stats = *lastEnvelopeCalibrationStats_;
        const float ratio = (stats.mean > 1e-6f) ? (stats.peak / stats.mean) : 0.0f;
        std::ostringstream oss;
        oss << "包絡ベースライン: mean=" << std::fixed << std::setprecision(3) << stats.mean
            << " peak=" << stats.peak << " ratio=" << ratio;
        oss << std::defaultfloat;
        lines.push_back({oss.str(), stats.valid && ratio >= 1.15f ? ofColor(160, 240, 160)
                                                                  : ofColor(255, 200, 140)});
    } else {
        lines.push_back({"包絡ベースライン: 未測定 — Envelope Baseline 計測 を実行してください", ofColor(255, 200, 150)});
    }

    if (weakSignalWarning_) {
        lines.push_back({"警告: 心音エンベロープが閾値を下回っています。マイク位置とゲインを点検してください。",
                         ofColor(255, 200, 140)});
    }

    if (simulateTelemetry_) {
        lines.push_back({"情報: シミュレーション信号が有効です。Synthetic Signal を OFF にすると実入力をモニタできます。",
                         ofColor(200, 220, 255)});
    }

    const std::string guidance = guidanceParam_.get();
    if (!guidance.empty() && guidance != "-") {
        lines.push_back({"ガイダンス: " + guidance, ofColor(200, 220, 255)});
    }

    float yCursor = y;
    for (const auto& line : lines) {
        ofSetColor(line.color);
        ofDrawBitmapString(line.text, x, yCursor);
        yCursor += 18.0f;
    }
    ofPopStyle();
}

void ofApp::drawBeatDebug() const {
    ofPushStyle();
    const float margin = 20.0f;
    std::ostringstream oss;
    oss << "Limiter減衰: " << std::fixed << std::setprecision(1) << limiterReductionDbSmooth_ << " dB"
        << " / BPM: " << std::setprecision(1) << latestMetrics_.bpm
        << " / Envelope: " << std::setprecision(2) << latestMetrics_.envelope
        << " / Haptic/min: " << std::setprecision(1) << hapticRateParam_.get();
    ofSetColor(210, 210, 220);
    ofDrawBitmapString(oss.str(), margin, ofGetHeight() - margin);
    ofPopStyle();
}
