#include "ofApp.h"

#include <algorithm>
#include <array>
#include <deque>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

namespace {

constexpr double kEnvelopeSampleIntervalSec = 0.05;  // 50 ms

float easedBlend(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return static_cast<float>(0.5 - 0.5 * std::cos(clamped * glm::pi<float>()));
}

float safeLerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

ofMesh& fullscreenQuadMesh() {
    static ofMesh mesh;
    if (mesh.getNumVertices() == 0) {
        mesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
        mesh.addVertex(glm::vec3(-1.0f, -1.0f, 0.0f));
        mesh.addVertex(glm::vec3(1.0f, -1.0f, 0.0f));
        mesh.addVertex(glm::vec3(-1.0f, 1.0f, 0.0f));
        mesh.addVertex(glm::vec3(1.0f, 1.0f, 0.0f));
    }
    return mesh;
}

std::optional<std::size_t> participantIndex(knot::audio::ParticipantId id) {
    using knot::audio::ParticipantId;
    switch (id) {
        case ParticipantId::Participant1:
            return 0;
        case ParticipantId::Participant2:
            return 1;
        default:
            return std::nullopt;
    }
}

}  // namespace

void ofApp::setup() {
    ofSetVerticalSync(true);
    ofSetFrameRate(60);

    infra::AppConfigLoader loader;
    appConfig_ = loader.load("config/app_config.json");
    operationMode_ = ofToLower(appConfig_.operationMode);
    showControlPanel_ = appConfig_.gui.showControlPanel;
    showStatusPanel_ = appConfig_.gui.showStatusPanel;
    allowKeyboardToggle_ = appConfig_.gui.allowKeyboardToggle;
    allowCornerUnlock_ = appConfig_.gui.allowCornerUnlock;
    guiToggleHoldTimeSec_ = std::max(0.0, appConfig_.gui.keyboardToggleHoldTime);
    if (!appConfig_.gui.keyboardToggleKey.empty()) {
        guiToggleKey_ = static_cast<int>(appConfig_.gui.keyboardToggleKey.front());
    }

    if (operationMode_ == "exhibition") {
        showControlPanel_ = false;
        showStatusPanel_ = false;
        allowKeyboardToggle_ = false;
    } else if (operationMode_ == "operator") {
        showControlPanel_ = false;
        showStatusPanel_ = true;
    }

    sceneTransitionLogger_.setup(appConfig_.sceneTransitionCsvPath);

    bool displayLoaded = displayFont_.load("fonts/NotoSansJP-Thin.otf", 120, true, true, true);
    if (!displayLoaded) {
        ofLogWarning("ofApp") << "Failed to load fonts/NotoSansJP-Thin.otf. Falling back to system font.";
        displayFont_.load(OF_TTF_SANS, 120, true, true, true);
    }
    bool guideLoaded = guideFont_.load("fonts/NotoSansJP-Regular.otf", 48, true, true, true);
    if (!guideLoaded) {
        ofLogWarning("ofApp") << "Failed to load fonts/NotoSansJP-Regular.otf. Falling back to system font.";
        guideFont_.load(OF_TTF_SANS, 48, true, true, true);
    }

    auto timingConfig = SceneTimingConfig::load(appConfig_.sceneTimingConfigPath);
    sceneTimingConfig_ = std::make_shared<SceneTimingConfig>(std::move(timingConfig));

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
    sceneController_.setTimingConfig(sceneTimingConfig_);
    sceneController_.setup(nowSeconds, 1.2);
    envelopeHistory_.setHorizon(30.0);
    for (auto& history : participantEnvelopeHistory_) {
        history.setHorizon(30.0);
    }
    latestMetrics_ = {};

    controlPanel_.setup("Session Control");
    controlPanel_.setPosition(20.0f, 20.0f);
    controlPanel_.add(sceneParam_.set("Scene", sceneStateToString(SceneState::Idle)));
    controlPanel_.add(bpmParam_.set("BPM", 0.0f, 0.0f, 240.0f));
    controlPanel_.add(envelopeParam_.set("Envelope", 0.0f, 0.0f, 1.0f));
    controlPanel_.add(bpmP1Param_.set("BPM P1", 0.0f, 0.0f, 240.0f));
    controlPanel_.add(bpmP2Param_.set("BPM P2", 0.0f, 0.0f, 240.0f));
    controlPanel_.add(envelopeP1Param_.set("Env P1", 0.0f, 0.0f, 1.0f));
    controlPanel_.add(envelopeP2Param_.set("Env P2", 0.0f, 0.0f, 1.0f));
    controlPanel_.add(hapticCountParam_.set("Haptic Count", 0U, 0U, 4096U));
    simulateTelemetry_ = appConfig_.enableSyntheticTelemetry;
    controlPanel_.add(simulateSignalParam_.set("Synthetic Signal", simulateTelemetry_));
    controlPanel_.add(startButton_.setup("Start Sequence"));
    controlPanel_.add(endButton_.setup("Trigger End"));
    controlPanel_.add(resetButton_.setup("Reset to Idle"));
    controlPanel_.add(envelopeCalibrationButton_.setup("Envelope Baseline 計測"));
    controlPanel_.add(refreshDevicesButton_.setup("Refresh Audio Devices"));
    controlPanel_.add(prevInputDeviceButton_.setup("< Input Device"));
    controlPanel_.add(nextInputDeviceButton_.setup("Input Device >"));
    controlPanel_.add(prevOutputDeviceButton_.setup("< Output Device"));
    controlPanel_.add(nextOutputDeviceButton_.setup("Output Device >"));
    controlPanel_.add(applyAudioDevicesButton_.setup("Apply Audio Device Selection"));
    controlPanel_.add(inputDeviceLabel_.set("Input Device", "未選択"));
    controlPanel_.add(outputDeviceLabel_.set("Output Device", "未選択"));

    startButton_.addListener(this, &ofApp::onStartButtonPressed);
    endButton_.addListener(this, &ofApp::onEndButtonPressed);
    resetButton_.addListener(this, &ofApp::onResetButtonPressed);
    envelopeCalibrationButton_.addListener(this, &ofApp::onEnvelopeCalibrationButtonPressed);
    refreshDevicesButton_.addListener(this, &ofApp::onRefreshAudioDevices);
    prevInputDeviceButton_.addListener(this, &ofApp::onPrevInputDevice);
    nextInputDeviceButton_.addListener(this, &ofApp::onNextInputDevice);
    prevOutputDeviceButton_.addListener(this, &ofApp::onPrevOutputDevice);
    nextOutputDeviceButton_.addListener(this, &ofApp::onNextOutputDevice);
    applyAudioDevicesButton_.addListener(this, &ofApp::onApplyAudioDevices);

    statusPanel_.setup("Monitor");
    statusPanel_.setPosition(controlPanel_.getPosition().x,
                             controlPanel_.getPosition().y + controlPanel_.getHeight() + 12.0f);
    statusPanel_.add(sceneOverviewParam_.set("シーン状態", sceneStateToString(sceneController_.currentState())));
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
    audioPipeline_.setInputGainDb(appConfig_.inputGainDb);
    ofLogNotice("ofApp") << "Input gain set to " << appConfig_.inputGainDb << " dB";
    audioRouter_.setup(static_cast<float>(sampleRate_));
    audioRouter_.applyScenePreset(sceneController_.currentState());
    ofLogNotice("ofApp") << "AudioRouter initialised with scene preset: "
                         << sceneStateToString(sceneController_.currentState());
    loadShaders();

    bellSoundLoaded_ = bellSound_.load("audio/bell.wav");
    if (bellSoundLoaded_) {
        bellSound_.setVolume(0.6f);
        bellSound_.setMultiPlay(false);
        ofLogNotice("ofApp") << "Bell sound loaded: audio/bell.wav";
    } else {
        ofLogWarning("ofApp") << "Failed to load bell sound: audio/bell.wav";
    }

    audioFadeGain_ = 1.0f;
    targetAudioFadeGain_ = 1.0f;
    audioFading_ = false;

    initializeSessionSeed();
    calibrationSaved_ = audioPipeline_.calibrationReady();
    calibrationSaveAttempted_ = calibrationSaved_;
    calibrationReportAppended_ = false;
    const bool pendingAutoCalibration = !calibrationSaved_;

    refreshAudioDeviceList();
    if (!setupSoundStreamWithSelection()) {
        simulateSignalParam_.set(true);
        simulateTelemetry_ = true;
    }

    if (pendingAutoCalibration) {
        if (soundStreamActive_) {
            ensureParentDirectory(calibrationFilePath_);
            ofLogNotice("ofApp") << "Calibration file not ready. Starting calibration.";
            audioPipeline_.startCalibration();
        } else {
            ofLogWarning("ofApp")
                << "Skip auto calibration because sound stream is inactive. Proceeding with degraded settings.";
            calibrationSaved_ = true;
            calibrationSaveAttempted_ = true;
            calibrationReportAppended_ = true;
        }
    }

    sessionStartMicros_ = ofGetElapsedTimeMicros();
    lastTelemetryMicros_ = sessionStartMicros_;
    lastEnvelopeSampledAt_ = 0.0;
    lastSimulatedBeatAt_.fill(0.0);
    beatCounter_ = 0;
    limiterReductionDbSmooth_ = 0.0f;
    lastStrongSignalAt_ = nowSeconds;
    weakSignalWarning_ = false;

    if (const auto defaultScene = sceneStateFromString(appConfig_.defaultScene)) {
        if (*defaultScene != SceneState::Idle) {
            sceneController_.requestState(*defaultScene, nowSeconds, false, "config_default");
        }
    }
}

void ofApp::update() {
    const uint64_t nowMicros = ofGetElapsedTimeMicros();
    const double nowSeconds = static_cast<double>(nowMicros) * 1e-6;

    sceneController_.update(nowSeconds);
    processSceneTransitionEvents();

    // オーディオフェード処理
    if (audioFading_) {
        const double elapsed = nowSeconds - audioFadeStartTime_;
        const double progress = std::clamp(elapsed / audioFadeDuration_, 0.0, 1.0);

        // イーズイン・イーズアウト曲線
        const float easedProgress = static_cast<float>(0.5 - 0.5 * std::cos(progress * M_PI));
        audioFadeGain_ = audioFadeGain_ + (targetAudioFadeGain_ - audioFadeGain_) * easedProgress;

        if (progress >= 1.0) {
            audioFadeGain_ = targetAudioFadeGain_;
            audioFading_ = false;
            ofLogNotice("ofApp") << "Audio fade completed. Gain: " << audioFadeGain_;
        }
    }

    simulateTelemetry_ = simulateSignalParam_.get();

    if (audioPipeline_.isCalibrationActive()) {
        calibrationSaved_ = false;
        calibrationSaveAttempted_ = false;
        calibrationReportAppended_ = false;
    } else if (audioPipeline_.calibrationReady() && !calibrationSaved_) {
        if (!calibrationSaveAttempted_) {
            ensureParentDirectory(calibrationFilePath_);
            const bool saved = audioPipeline_.saveCalibrationFile(calibrationFilePath_);
            if (saved) {
                ofLogNotice("ofApp") << "Calibration saved to " << calibrationFilePath_;
            } else {
                ofLogWarning("ofApp") << "Failed to save calibration to " << calibrationFilePath_
                                      << ". Continuing with current calibration values.";
            }
            calibrationSaved_ = true;
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
        const auto metricsP1 =
            audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant1);
        const auto metricsP2 =
            audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant2);
        const bool metricsAvailable =
            (metricsP1.timestampSec > 0.0) || (metricsP2.timestampSec > 0.0) ||
            (metricsP1.envelope > 0.0f) || (metricsP2.envelope > 0.0f);
        if (metricsAvailable) {
            applyBeatMetrics(knot::audio::ParticipantId::Participant1, metricsP1, nowSeconds);
            applyBeatMetrics(knot::audio::ParticipantId::Participant2, metricsP2, nowSeconds);

            const auto eventsP1 =
                audioPipeline_.pollBeatEvents(knot::audio::ParticipantId::Participant1);
            if (!eventsP1.empty()) {
                handleBeatEvents(knot::audio::ParticipantId::Participant1, eventsP1, nowSeconds);
            }
            const auto eventsP2 =
                audioPipeline_.pollBeatEvents(knot::audio::ParticipantId::Participant2);
            if (!eventsP2.empty()) {
                handleBeatEvents(knot::audio::ParticipantId::Participant2, eventsP2, nowSeconds);
            }

            limiterReductionDbSmooth_ =
                ofLerp(limiterReductionDbSmooth_, audioPipeline_.lastLimiterReductionDb(), 0.18f);
        } else {
            updateFakeSignal(nowSeconds);
            limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
        }
        signalHealth_ = audioPipeline_.signalHealth();
    }

    latestMetrics_.timestampSec = nowSeconds;
    latestMetrics_.bpm = 0.5f * (participantBpms_[0] + participantBpms_[1]);
    latestMetrics_.envelope =
        std::clamp(0.5f * (participantEnvelopes_[0] + participantEnvelopes_[1]), 0.0f, 1.0f);
    signalHealth_.fallbackEnvelope = latestMetrics_.envelope;
    displayEnvelope_ = std::clamp(blendedEnvelope(), 0.0f, 1.0f);

    if (!signalHealth_.fallbackActive && useSynthetic) {
        signalHealth_.fallbackBlend = 0.0f;
    }
    updateEnvelopeHistories(nowSeconds);

    if (lastFallbackActive_ != signalHealth_.fallbackActive) {
        if (signalHealth_.fallbackActive) {
            ofLogNotice("ofApp") << "Signal dropout detected. Entering fallback mode.";
        } else {
            ofLogNotice("ofApp") << "Signal recovered. Returning to live input.";
        }
        lastFallbackActive_ = signalHealth_.fallbackActive;
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
        frame.sceneId = sceneStateToString(sceneController_.currentState());
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
    const float baseAlpha = sceneController_.isTransitioning() ? blend : 1.0f;
    drawScene(current, baseAlpha, nowSeconds);
    if (shouldDrawControlPanel()) {
        controlPanel_.draw();
    }
    if (shouldDrawStatusPanel()) {
        if (shouldDrawControlPanel()) {
            statusPanel_.setPosition(controlPanel_.getPosition().x,
                                     controlPanel_.getPosition().y + controlPanel_.getHeight() + 12.0f);
        } else {
            statusPanel_.setPosition(20.0f, 20.0f);
        }
        statusPanel_.draw();
    }
    if (shouldDrawControlPanel() || shouldDrawStatusPanel()) {
        drawCalibrationStatus();
        drawBeatDebug();
    }
}

void ofApp::exit() {
    startButton_.removeListener(this, &ofApp::onStartButtonPressed);
    endButton_.removeListener(this, &ofApp::onEndButtonPressed);
    resetButton_.removeListener(this, &ofApp::onResetButtonPressed);
    envelopeCalibrationButton_.removeListener(this, &ofApp::onEnvelopeCalibrationButtonPressed);
    refreshDevicesButton_.removeListener(this, &ofApp::onRefreshAudioDevices);
    prevInputDeviceButton_.removeListener(this, &ofApp::onPrevInputDevice);
    nextInputDeviceButton_.removeListener(this, &ofApp::onNextInputDevice);
    prevOutputDeviceButton_.removeListener(this, &ofApp::onPrevOutputDevice);
    nextOutputDeviceButton_.removeListener(this, &ofApp::onNextOutputDevice);
    applyAudioDevicesButton_.removeListener(this, &ofApp::onApplyAudioDevices);

    shutdownSoundStream();

    if (sessionLogger_) {
        sessionLogger_->writeSummary();
        sessionLogger_.reset();
    }
    sceneTransitionLogger_.flush();
    hapticLogger_.reset();
}

void ofApp::keyPressed(int key) {
    const double nowSeconds = ofGetElapsedTimef();

    if (allowKeyboardToggle_ && key == guiToggleKey_) {
        guiKeyPressedAtSec_ = nowSeconds;
    }

    if (operationMode_ == "exhibition" && !guiOverrideVisible_) {
        return;
    }

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

void ofApp::keyReleased(int key) {
    if (allowKeyboardToggle_ && key == guiToggleKey_) {
        const double nowSeconds = ofGetElapsedTimef();
        const double held = (guiKeyPressedAtSec_ > 0.0) ? nowSeconds - guiKeyPressedAtSec_ : 0.0;
        if (guiToggleHoldTimeSec_ <= 0.0 || held >= guiToggleHoldTimeSec_) {
            guiOverrideVisible_ = !guiOverrideVisible_;
            ofLogNotice("ofApp") << "GUI override toggled via keyboard: " << (guiOverrideVisible_ ? "visible" : "hidden");
        }
        guiKeyPressedAtSec_ = 0.0;
    }
}
void ofApp::mouseMoved(int, int) {}
void ofApp::mouseDragged(int, int, int) {}
void ofApp::mousePressed(int x, int y, int) {
    updateCornerUnlock(ofGetElapsedTimef(), x, y);
}
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
    const std::size_t numFrames = output.getNumFrames();
    const std::size_t numChannels = output.getNumChannels();
    if (numFrames == 0 || numChannels == 0) {
        return;
    }

    if (stereoScratch_.getNumFrames() != numFrames || stereoScratch_.getNumChannels() != 2) {
        stereoScratch_.allocate(numFrames, 2);
    }
    stereoScratch_.setSampleRate(output.getSampleRate());

    audioPipeline_.audioOut(stereoScratch_);

    const auto metricsP1 = audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant1);
    const auto metricsP2 = audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant2);
    envelopeFrame_[0] = std::clamp(metricsP1.envelope, 0.0f, 1.0f);
    envelopeFrame_[1] = std::clamp(metricsP2.envelope, 0.0f, 1.0f);

    const float* stereoData = stereoScratch_.getBuffer().data();
    float* outputData = output.getBuffer().data();

    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        headphoneFrame_[0] = stereoData[frame * 2];
        headphoneFrame_[1] = stereoData[frame * 2 + 1];

        audioRouter_.route(headphoneFrame_, envelopeFrame_, routedFrame_);

        if (numChannels >= 4) {
            outputData[frame * numChannels + 0] = routedFrame_[0];
            outputData[frame * numChannels + 1] = routedFrame_[1];
            outputData[frame * numChannels + 2] = routedFrame_[2];
            outputData[frame * numChannels + 3] = routedFrame_[3];
        } else if (numChannels >= 2) {
            outputData[frame * numChannels + 0] = routedFrame_[0];
            outputData[frame * numChannels + 1] = routedFrame_[1];
        }
    }

    if (audioFadeGain_ < 0.99f) {
        const std::size_t totalSamples = numFrames * numChannels;
        for (std::size_t i = 0; i < totalSamples; ++i) {
            outputData[i] *= audioFadeGain_;
        }
    }
}

void ofApp::onStartButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "Start request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::Start, nowSeconds, true, "button_press")) {
        ofLogNotice("ofApp") << "Start request ignored.";
    }
}

void ofApp::onEndButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "End request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::End, nowSeconds, true, "button_press")) {
        ofLogNotice("ofApp") << "End request ignored.";
    }
}

void ofApp::onResetButtonPressed() {
    if (isInteractionLocked()) {
        ofLogNotice("ofApp") << "Reset request ignored (locked state).";
        return;
    }
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::Idle, nowSeconds, true, "button_press")) {
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
        oss << sceneStateToString(current) << " → " << sceneStateToString(target);
        oss << " (" << static_cast<int>(sceneController_.transitionBlend() * 100.0f) << "%)";
        sceneLabel = oss.str();
    } else {
        sceneLabel = sceneStateToString(current);
    }
    sceneParam_.set(sceneLabel);

    bpmParam_.set(latestMetrics_.bpm);
    envelopeParam_.set(latestMetrics_.envelope);
    bpmP1Param_.set(participantBpms_[0]);
    bpmP2Param_.set(participantBpms_[1]);
    envelopeP1Param_.set(participantEnvelopes_[0]);
    envelopeP2Param_.set(participantEnvelopes_[1]);
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

void ofApp::updateEnvelopeHistories(double nowSeconds) {
    if (nowSeconds - lastEnvelopeSampledAt_ < kEnvelopeSampleIntervalSec) {
        return;
    }
    lastEnvelopeSampledAt_ = nowSeconds;
    for (std::size_t idx = 0; idx < participantEnvelopeHistory_.size(); ++idx) {
        participantEnvelopeHistory_[idx].addSample(nowSeconds, participantEnvelopes_[idx], participantBpms_[idx]);
    }
    envelopeHistory_.addSample(nowSeconds, displayEnvelope_, latestMetrics_.bpm);
}

void ofApp::updateFakeSignal(double nowSeconds) {
    const std::array<double, 2> phases{
        nowSeconds * 0.45,
        nowSeconds * 0.58 + 1.1};
    const std::array<float, 2> bpms{
        64.0f + 6.0f * static_cast<float>(std::sin(phases[0] * 0.7)),
        70.0f + 5.0f * static_cast<float>(std::cos(phases[1] * 0.5))};
    const std::array<float, 2> envelopes{
        ofClamp(0.5f + 0.45f * static_cast<float>(std::sin(phases[0])), 0.0f, 1.0f),
        ofClamp(0.48f + 0.46f * static_cast<float>(std::sin(phases[1] + 0.6)), 0.0f, 1.0f)};

    for (std::size_t idx = 0; idx < 2; ++idx) {
        participantMetrics_[idx].timestampSec = nowSeconds;
        participantMetrics_[idx].bpm = bpms[idx];
        participantMetrics_[idx].envelope = envelopes[idx];
        participantBpms_[idx] = bpms[idx];
        participantEnvelopes_[idx] = envelopes[idx];

        const double beatIntervalSec = 60.0 / std::max(35.0f, bpms[idx]);
        if (nowSeconds - lastSimulatedBeatAt_[idx] >= beatIntervalSec) {
            lastSimulatedBeatAt_[idx] = nowSeconds;
            const float intensity =
                ofClamp(0.4f + 0.5f * static_cast<float>(std::sin(phases[idx] * 1.3)), 0.0f, 1.0f);
            const std::string label = idx == 0 ? "P1_synthetic" : "P2_synthetic";
            appendHapticEvent(nowSeconds, intensity, label);
        }
    }

    latestMetrics_.timestampSec = nowSeconds;
    latestMetrics_.bpm = 0.5f * (bpms[0] + bpms[1]);
    latestMetrics_.envelope = 0.5f * (envelopes[0] + envelopes[1]);

    signalHealth_.envelopeShort = latestMetrics_.envelope;
    signalHealth_.envelopeMid = latestMetrics_.envelope;
    signalHealth_.envelopeLong = latestMetrics_.envelope;
    signalHealth_.bpmAverage = latestMetrics_.bpm;
    signalHealth_.dropoutSeconds = 0.0f;
    signalHealth_.fallbackActive = false;
    signalHealth_.fallbackBlend = 0.0f;
    signalHealth_.fallbackEnvelope = latestMetrics_.envelope;
}

void ofApp::applyBeatMetrics(knot::audio::ParticipantId participant,
                             const knot::audio::AudioPipeline::ChannelMetrics& metrics, double nowSeconds) {
    const auto idx = participantIndex(participant);
    if (!idx) {
        return;
    }
    participantMetrics_[*idx].timestampSec = nowSeconds;
    participantMetrics_[*idx].bpm = metrics.bpm;
    participantMetrics_[*idx].envelope = metrics.envelope;
    participantEnvelopes_[*idx] = std::clamp(metrics.envelope, 0.0f, 1.0f);
    participantBpms_[*idx] = std::max(0.0f, metrics.bpm);
}

void ofApp::handleBeatEvents(knot::audio::ParticipantId participant,
                             const std::vector<knot::audio::BeatEvent>& events, double nowSeconds) {
    const auto idx = participantIndex(participant);
    if (!idx) {
        return;
    }
    for (const auto& evt : events) {
        if (evt.bpm > 1.0f) {
            participantBpms_[*idx] = evt.bpm;
            participantMetrics_[*idx].bpm = evt.bpm;
        }
        const float intensity = ofClamp(evt.envelope, 0.2f, 1.0f);
        const std::string labelPrefix = (participant == knot::audio::ParticipantId::Participant1) ? "P1" : "P2";
        const std::string label = signalHealth_.fallbackActive ? labelPrefix + "_fallback" : labelPrefix + "_detected";
        appendHapticEvent(nowSeconds, intensity, label);
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

void ofApp::loadShaders() {
    const auto loadShaderFn = [&](ofShader& shader, bool& loaded, const std::string& vert, const std::string& frag) {
        loaded = false;
        const std::string vertPath = ofToDataPath(vert, true);
        const std::string fragPath = ofToDataPath(frag, true);
        if (!ofFile::doesFileExist(vertPath) || !ofFile::doesFileExist(fragPath)) {
            ofLogWarning("ofApp") << "Shader files not found: " << vert << " / " << frag;
            return;
        }
        try {
            loaded = shader.load(vert, frag);
        } catch (const std::exception& ex) {
            loaded = false;
            ofLogWarning("ofApp") << "Failed to load shader (" << vert << ", " << frag << "): " << ex.what();
        }
        if (!loaded) {
            ofLogWarning("ofApp") << "Shader compile failed for " << vert << " / " << frag;
        }
    };

    loadShaderFn(starfieldShader_, starfieldShaderLoaded_, "shaders/starfield.vert", "shaders/starfield.frag");
    loadShaderFn(torusShader_, torusShaderLoaded_, "shaders/torus.vert", "shaders/torus.frag");
    loadShaderFn(rippleShader_, rippleShaderLoaded_, "shaders/ripple.vert", "shaders/ripple.frag");
}

void ofApp::drawStarfieldLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2) {
    if (!starfieldShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float env1 = std::clamp(envelopeP1, 0.0f, 1.0f);
    const float env2 = std::clamp(envelopeP2, 0.0f, 1.0f);
    ofFill();
    starfieldShader_.begin();
    starfieldShader_.setUniform2f("uResolution", static_cast<float>(ofGetWidth()), static_cast<float>(ofGetHeight()));
    starfieldShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    starfieldShader_.setUniform2f("uEnvelopes", env1, env2);
    starfieldShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    starfieldShader_.end();
}

void ofApp::drawRippleLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2) {
    if (!rippleShaderLoaded_) {
        return;
    }
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float env1 = std::clamp(envelopeP1, 0.0f, 1.0f);
    const float env2 = std::clamp(envelopeP2, 0.0f, 1.0f);
    ofFill();
    rippleShader_.begin();
    rippleShader_.setUniform2f("uResolution", static_cast<float>(ofGetWidth()), static_cast<float>(ofGetHeight()));
    rippleShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    rippleShader_.setUniform2f("uEnvelopes", env1, env2);
    rippleShader_.setUniform1f("uAlpha", clampedAlpha);
    fullscreenQuadMesh().draw();
    rippleShader_.end();
}

void ofApp::refreshAudioDeviceList() {
    const int currentInputId =
        (selectedInputDevice_ >= 0 && selectedInputDevice_ < static_cast<int>(inputDevices_.size()))
            ? inputDevices_[selectedInputDevice_].deviceID
            : -1;
    const int currentOutputId =
        (selectedOutputDevice_ >= 0 && selectedOutputDevice_ < static_cast<int>(outputDevices_.size()))
            ? outputDevices_[selectedOutputDevice_].deviceID
            : -1;

    inputDevices_.clear();
    outputDevices_.clear();

    const auto devices = ofSoundStreamListDevices();
    for (const auto& device : devices) {
        if (device.inputChannels > 0) {
            inputDevices_.push_back(device);
        }
        if (device.outputChannels > 0) {
            outputDevices_.push_back(device);
        }
    }

    auto resolveSelection = [](const std::vector<ofSoundDevice>& devices, int desiredId, bool preferInput) {
        if (devices.empty()) {
            return -1;
        }
        if (desiredId != -1) {
            for (std::size_t i = 0; i < devices.size(); ++i) {
                if (devices[i].deviceID == desiredId) {
                    return static_cast<int>(i);
                }
            }
        }
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (preferInput && devices[i].isDefaultInput) {
                return static_cast<int>(i);
            }
            if (!preferInput && devices[i].isDefaultOutput) {
                return static_cast<int>(i);
            }
        }
        return 0;
    };

    selectedInputDevice_ = resolveSelection(inputDevices_, currentInputId, true);
    selectedOutputDevice_ = resolveSelection(outputDevices_, currentOutputId, false);

    updateAudioDeviceLabels();
}

void ofApp::updateAudioDeviceLabels() {
    if (inputDevices_.empty()) {
        inputDeviceLabel_.set("Input Device", "入力デバイス未検出");
    } else if (selectedInputDevice_ >= 0 &&
               selectedInputDevice_ < static_cast<int>(inputDevices_.size())) {
        const auto& device = inputDevices_[selectedInputDevice_];
        const std::string label = device.name + " (ID " + ofToString(device.deviceID) + ", " +
                                  ofToString(device.inputChannels) + "ch)";
        inputDeviceLabel_.set("Input Device", label);
    } else {
        inputDeviceLabel_.set("Input Device", "入力デバイス未選択");
    }

    if (outputDevices_.empty()) {
        outputDeviceLabel_.set("Output Device", "出力デバイス未検出");
    } else if (selectedOutputDevice_ >= 0 &&
               selectedOutputDevice_ < static_cast<int>(outputDevices_.size())) {
        const auto& device = outputDevices_[selectedOutputDevice_];
        std::string label = device.name + " (ID " + ofToString(device.deviceID) + ", " +
                            ofToString(device.outputChannels) + "ch)";
        if (device.outputChannels < 4) {
            label += " ※4ch未対応";
        }
        outputDeviceLabel_.set("Output Device", label);
    } else {
        outputDeviceLabel_.set("Output Device", "出力デバイス未選択");
    }
}

void ofApp::selectNextInputDevice(int delta) {
    if (inputDevices_.empty()) {
        return;
    }
    if (selectedInputDevice_ < 0) {
        selectedInputDevice_ = 0;
    }
    const int size = static_cast<int>(inputDevices_.size());
    selectedInputDevice_ = (selectedInputDevice_ + delta + size) % size;
    updateAudioDeviceLabels();
}

void ofApp::selectNextOutputDevice(int delta) {
    if (outputDevices_.empty()) {
        return;
    }
    if (selectedOutputDevice_ < 0) {
        selectedOutputDevice_ = 0;
    }
    const int size = static_cast<int>(outputDevices_.size());
    selectedOutputDevice_ = (selectedOutputDevice_ + delta + size) % size;
    updateAudioDeviceLabels();
}

void ofApp::onRefreshAudioDevices() {
    refreshAudioDeviceList();
}

void ofApp::onPrevInputDevice() {
    selectNextInputDevice(-1);
}

void ofApp::onNextInputDevice() {
    selectNextInputDevice(1);
}

void ofApp::onPrevOutputDevice() {
    selectNextOutputDevice(-1);
}

void ofApp::onNextOutputDevice() {
    selectNextOutputDevice(1);
}

void ofApp::onApplyAudioDevices() {
    if (setupSoundStreamWithSelection()) {
        simulateTelemetry_ = simulateSignalParam_.get();
    } else {
        simulateSignalParam_.set(true);
        simulateTelemetry_ = true;
    }
}

void ofApp::shutdownSoundStream() {
    if (!soundStreamActive_) {
        return;
    }
    try {
        soundStream_.stop();
        soundStream_.close();
    } catch (const std::exception& ex) {
        ofLogError("ofApp") << "Sound stream shutdown failed: " << ex.what();
    }
    soundStreamActive_ = false;
}

bool ofApp::setupSoundStreamWithSelection() {
    if (outputDevices_.empty()) {
        ofLogError("ofApp") << "出力デバイスが検出できません。";
        shutdownSoundStream();
        return false;
    }

    if (selectedOutputDevice_ < 0 || selectedOutputDevice_ >= static_cast<int>(outputDevices_.size())) {
        selectedOutputDevice_ = std::min<int>(0, static_cast<int>(outputDevices_.size()) - 1);
    }
    if (selectedInputDevice_ < 0 || selectedInputDevice_ >= static_cast<int>(inputDevices_.size())) {
        selectedInputDevice_ = inputDevices_.empty() ? -1 : 0;
    }

    ofSoundStreamSettings settings;
    settings.sampleRate = static_cast<unsigned int>(sampleRate_);
    settings.bufferSize = bufferSize_;
    settings.numBuffers = 4;
    settings.setInListener(this);
    settings.setOutListener(this);

    if (selectedInputDevice_ >= 0 && selectedInputDevice_ < static_cast<int>(inputDevices_.size())) {
        const auto& inDevice = inputDevices_[selectedInputDevice_];
        settings.setInDevice(inDevice);
        settings.numInputChannels = std::min<std::size_t>(2, inDevice.inputChannels);
        if (settings.numInputChannels == 0) {
            ofLogWarning("ofApp") << "選択した入力デバイス '" << inDevice.name
                                  << "' は入力チャンネルを提供しません。";
        }
    } else {
        settings.numInputChannels = 0;
    }

    const auto& outDevice = outputDevices_[selectedOutputDevice_];
    settings.setOutDevice(outDevice);
    settings.numOutputChannels = std::min<std::size_t>(4, outDevice.outputChannels);
    if (settings.numOutputChannels < 2) {
        ofLogError("ofApp") << "選択した出力デバイス '" << outDevice.name
                            << "' はステレオ出力に必要なチャンネル数を満たしていません。";
        return false;
    }

    shutdownSoundStream();

    try {
        soundStream_.setup(settings);
        soundStream_.start();
        soundStreamActive_ = true;
        configuredOutputChannels_ = static_cast<int>(settings.numOutputChannels);
        if (settings.numOutputChannels < 4) {
            ofLogWarning("ofApp") << "出力デバイス '" << outDevice.name << "' は "
                                  << settings.numOutputChannels
                                  << "ch までしか対応していません。ハプティクス用CH3/4は自動的にダウンミックスされます。";
        } else {
            ofLogNotice("ofApp") << "出力デバイス '" << outDevice.name << "' を "
                                 << settings.numOutputChannels << "ch で初期化しました。";
        }
        updateAudioDeviceLabels();
        return true;
    } catch (const std::exception& ex) {
        ofLogError("ofApp") << "オーディオデバイス初期化に失敗しました: " << ex.what();
        soundStreamActive_ = false;
        updateAudioDeviceLabels();
        return false;
    }
}

float ofApp::blendedEnvelope() const {
    const float base = std::clamp(0.6f * signalHealth_.envelopeShort +
                                      0.3f * signalHealth_.envelopeMid +
                                      0.1f * signalHealth_.envelopeLong,
                                  0.0f, 1.0f);
    if (signalHealth_.fallbackActive) {
        const float fallbackEnv = std::clamp(signalHealth_.fallbackEnvelope, 0.0f, 1.0f);
        const float blend = std::clamp(signalHealth_.fallbackBlend, 0.0f, 1.0f);
        return safeLerp(base, fallbackEnv, blend);
    }
    return base;
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
    const bool gainOkCh1 = std::isfinite(gainDbCh1) && std::abs(gainDbCh1) <= 30.0;
    const bool gainOkCh2 = std::isfinite(gainDbCh2) && std::abs(gainDbCh2) <= 30.0;
    const bool delayOkCh1 = std::abs(values[0].delaySamples) <= 200;
    const bool delayOkCh2 = std::abs(values[1].delaySamples) <= 200;

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
        ofLogWarning("ofApp") << "Calibration quality degraded (proceeding anyway)."
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

void ofApp::processSceneTransitionEvents() {
    while (const auto event = sceneController_.popTransitionEvent()) {
        handleTransitionEvent(*event);
    }
}

void ofApp::handleTransitionEvent(const SceneController::TransitionEvent& event) {
    infra::SceneTransitionLogger::TransitionRecord record;
    record.timestampMicros = static_cast<uint64_t>(event.timestamp * 1'000'000.0);
    record.sceneFrom = event.from;
    record.sceneTo = event.to;
    record.transitionType = event.manual ? "manual" : "auto";
    record.triggerReason = event.triggerReason.empty() ? (event.manual ? "manual" : "timeout") : event.triggerReason;
    record.timeInStateSec = event.timeInState;
    record.blendDurationSec = event.blendDuration;
    record.completed = event.completed;

    // シーン遷移開始時の処理
    if (!event.completed) {
        // ベル音を鳴らす(Start→FirstPhase, FirstPhase→Exchange等の主要遷移)
        if (bellSoundLoaded_) {
            const bool playBell = (event.to == SceneState::FirstPhase) ||
                                  (event.to == SceneState::Exchange) ||
                                  (event.to == SceneState::Mixed) ||
                                  (event.to == SceneState::End);
            if (playBell) {
                bellSound_.play();
                ofLogNotice("ofApp") << "Bell sound played for transition: "
                                      << sceneStateToString(event.from) << " → "
                                      << sceneStateToString(event.to);
            }
        }

        // オーディオフェードアウト開始(心音交換シーンへの遷移時)
        if (event.to == SceneState::Exchange || event.to == SceneState::Mixed) {
            audioFadeStartTime_ = event.timestamp;
            targetAudioFadeGain_ = 0.1f;  // 10%まで減衰
            audioFading_ = true;
            ofLogNotice("ofApp") << "Audio fade-out started for scene transition";
        }
    }

    // シーン遷移完了時の処理
    if (event.completed) {
        // オーディオフェードイン(Exchange/Mixed完了後)
        if (event.to == SceneState::Exchange || event.to == SceneState::Mixed) {
            audioFadeStartTime_ = event.timestamp;
            targetAudioFadeGain_ = 1.0f;  // 100%に復帰
            audioFading_ = true;
            ofLogNotice("ofApp") << "Audio fade-in started after scene transition";
        }
        audioRouter_.applyScenePreset(event.to);
        ofLogNotice("ofApp") << "Audio routing preset reapplied for scene: " << sceneStateToString(event.to);
    }

    if (!event.manual && sceneTimingConfig_) {
        if (const auto expected = sceneTimingConfig_->effectiveDuration(event.from)) {
            record.expectedDurationSec = expected;
            if (!event.completed) {
                record.deviationSec = event.timeInState - *expected;
            }
        }
    }

    sceneTransitionLogger_.recordTransition(record);
}

bool ofApp::shouldDrawControlPanel() const {
    return showControlPanel_ || guiOverrideVisible_;
}

bool ofApp::shouldDrawStatusPanel() const {
    return showStatusPanel_ || guiOverrideVisible_;
}

void ofApp::updateCornerUnlock(double nowSeconds, int x, int y) {
    if (!allowCornerUnlock_) {
        return;
    }

    const glm::vec2 point(static_cast<float>(x), static_cast<float>(y));
    const float margin = 48.0f;
    const std::array<glm::vec2, 4> corners = {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(static_cast<float>(ofGetWidth()), 0.0f),
        glm::vec2(0.0f, static_cast<float>(ofGetHeight())),
        glm::vec2(static_cast<float>(ofGetWidth()), static_cast<float>(ofGetHeight()))};

    bool nearCorner = false;
    for (const auto& corner : corners) {
        if (glm::distance(point, corner) <= margin) {
            nearCorner = true;
            break;
        }
    }
    if (!nearCorner) {
        return;
    }

    cornerTouches_.emplace_back(nowSeconds, point);
    cornerTouches_.erase(std::remove_if(cornerTouches_.begin(), cornerTouches_.end(), [&](const auto& entry) {
                              return nowSeconds - entry.first > cornerUnlockWindowSec_;
                          }),
        cornerTouches_.end());

    std::array<bool, 4> activated {false, false, false, false};
    for (const auto& entry : cornerTouches_) {
        for (size_t i = 0; i < corners.size(); ++i) {
            if (glm::distance(entry.second, corners[i]) <= margin) {
                activated[i] = true;
            }
        }
    }

    if (std::all_of(activated.begin(), activated.end(), [](bool v) { return v; })) {
        guiOverrideVisible_ = !guiOverrideVisible_;
        cornerTouches_.clear();
        ofLogNotice("ofApp") << "GUI override toggled via corner unlock: " << (guiOverrideVisible_ ? "visible" : "hidden");
    }
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
            case SceneState::Start:
                drawStartScene(layerAlpha, nowSeconds);
                break;
            case SceneState::FirstPhase:
                drawFirstPhaseScene(layerAlpha, nowSeconds);
                break;
            case SceneState::Exchange:
                drawExchangeScene(layerAlpha, nowSeconds);
                break;
            case SceneState::Mixed:
                drawMixedScene(layerAlpha, nowSeconds);
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
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    const float envelope = latestMetrics_.envelope;
    const float baseRadius = 180.0f;
    const float radiusOffset = envelope * 240.0f;
    const float centerY = ofGetHeight() * 0.5f;

    if (starfieldShaderLoaded_) {
        drawStarfieldLayer(clampedAlpha, nowSeconds, envelopeP1, envelopeP2);
    } else {
        ofColor background(8, 9, 18);
        background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
        ofSetColor(background);
        ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

        ofFill();
        ofColor starBase(255, 255, 255, static_cast<unsigned char>(clampedAlpha * 90.0f));
        const int starCount = 120;
        for (int i = 0; i < starCount; ++i) {
            const float u = static_cast<float>(i) / starCount;
            const float x = u * ofGetWidth();
            const float y = std::fmod(static_cast<float>(i) * 53.0f + static_cast<float>(nowSeconds) * 40.0f,
                                      static_cast<float>(ofGetHeight()));
            const float radius = 1.0f + 1.2f * std::sin(nowSeconds * 0.8 + i * 0.25f);
            const float localEnv = ofLerp(envelopeP1, envelopeP2, u);
            ofColor starColor = starBase;
            starColor.a = static_cast<unsigned char>(
                starBase.a * (0.4f + 0.6f * localEnv) *
                (0.6f + 0.4f * std::sin(nowSeconds * 0.5f + i * 0.2f)));
            ofSetColor(starColor);
            ofDrawCircle(x, y, radius);
        }
    }

    if (rippleShaderLoaded_) {
        drawRippleLayer(clampedAlpha, nowSeconds, envelopeP1, envelopeP2);
    } else {
        ofNoFill();
        ofSetLineWidth(2.0f);
        ofColor rippleBase(120, 140, 255, static_cast<unsigned char>(safeLerp(20.0f, 90.0f, envelope)));

        for (int i = 0; i < 3; ++i) {
            const float falloff = std::max(0.0f, 1.0f - i * 0.28f);
            ofColor rippleColor = rippleBase;
            rippleColor.a = static_cast<unsigned char>(rippleColor.a * falloff * clampedAlpha);
            const float radius = baseRadius + radiusOffset + i * 36.0f;
            ofSetColor(rippleColor);
            ofDrawCircle(ofGetWidth() * 0.3f, centerY, radius * (0.6f + 0.4f * envelopeP1));
            ofDrawCircle(ofGetWidth() * 0.7f, centerY, radius * (0.6f + 0.4f * envelopeP2));
        }
        ofFill();
    }

    ofSetColor(220, 240, 255, static_cast<int>(clampedAlpha * 200.0f));
    const std::string idleText = "Idle — 入力監視中 / 環境整備フェーズ";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(idleText, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(idleText, 40, 80);
    }

    ofPopStyle();
}

void ofApp::drawStartScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const double timeInState = sceneController_.timeInState(nowSeconds);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    if (starfieldShaderLoaded_) {
        drawStarfieldLayer(clampedAlpha, nowSeconds, envelopeP1, envelopeP2);
    } else {
        ofColor background(12, 10, 28);
        background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
        ofSetColor(background);
        ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());
    }

    const auto stageLookup = [&](const std::string& name) -> const SceneTimingConfig::Stage* {
        if (!sceneTimingConfig_) {
            return nullptr;
        }
        return sceneTimingConfig_->findStage(SceneState::Start, name);
    };

    const auto stageProgress = [&](const SceneTimingConfig::Stage* stage) -> float {
        if (stage == nullptr || stage->duration <= 0.0) {
            return 0.0f;
        }
        const double local = timeInState - stage->startAt;
        if (local <= 0.0) {
            return 0.0f;
        }
        if (local >= stage->duration) {
            return 1.0f;
        }
        return static_cast<float>(local / stage->duration);
    };

    const SceneTimingConfig::Stage* fadeInStage = stageLookup("textFadeIn");
    const SceneTimingConfig::Stage* fadeOutStage = stageLookup("textFadeOut");
    float fadeInFactor = fadeInStage ? stageProgress(fadeInStage) : std::clamp(static_cast<float>(timeInState), 0.0f, 1.0f);
    float fadeOutFactor = 1.0f;
    if (fadeOutStage && timeInState >= fadeOutStage->startAt) {
        const float progress = stageProgress(fadeOutStage);
        fadeOutFactor = std::clamp(1.0f - progress, 0.0f, 1.0f);
    }

    const std::string title = "体験を始めます";
    const float titleAlpha = clampedAlpha * fadeInFactor * fadeOutFactor;
    if (titleAlpha > 0.01f) {
        ofSetColor(255, 255, 255, static_cast<int>(titleAlpha * 255.0f));
        if (displayFont_.isLoaded()) {
            const float textWidth = displayFont_.stringWidth(title);
            const float textHeight = displayFont_.stringHeight(title);
            const float x = (ofGetWidth() - textWidth) * 0.5f;
            const float y = ofGetHeight() * 0.33f + textHeight * 0.5f;
            displayFont_.drawString(title, x, y);
        } else {
            ofDrawBitmapStringHighlight(title, ofGetWidth() * 0.5f - 80.0f, ofGetHeight() * 0.35f);
        }
    }

    const SceneTimingConfig::Stage* breathingStage = stageLookup("breathingGuide");
    float breathingEnvelope = 0.0f;
    if (breathingStage) {
        const double local = timeInState - breathingStage->startAt;
        if (local >= 0.0 && local <= breathingStage->duration) {
            const double norm = breathingStage->duration > 0.0 ? local / breathingStage->duration : 0.0;
            const double envelope = std::clamp(std::min(norm, 1.0 - norm) * 2.0, 0.0, 1.0);
            breathingEnvelope = static_cast<float>(envelope);
        }
    }

    if (breathingEnvelope > 0.01f) {
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(nowSeconds * 1.2));
        const float radius = safeLerp(90.0f, 160.0f, pulse);
        ofNoFill();
        ofSetLineWidth(6.0f);
        ofColor ringColor(180, 200, 255, static_cast<unsigned char>(clampedAlpha * breathingEnvelope * 160.0f));
        ofSetColor(ringColor);
        ofDrawCircle(glm::vec2(ofGetWidth() * 0.5f, ofGetHeight() * 0.55f), radius);
        const std::string guidance = "ゆっくり深呼吸を 3 回行ってください";
        ofSetColor(240, 240, 240, static_cast<int>(clampedAlpha * breathingEnvelope * 220.0f));
        if (guideFont_.isLoaded()) {
            const float gw = guideFont_.stringWidth(guidance);
            guideFont_.drawString(guidance, (ofGetWidth() - gw) * 0.5f, ofGetHeight() * 0.65f);
        } else {
            ofDrawBitmapString(guidance, ofGetWidth() * 0.5f - 140.0f, ofGetHeight() * 0.65f);
        }
    }

    const SceneTimingConfig::Stage* bellStage = stageLookup("bellSound");
    if (bellStage) {
        const double local = timeInState - bellStage->startAt;
        if (local >= 0.0 && local <= bellStage->duration) {
            const float bellAlpha = clampedAlpha * static_cast<float>(1.0 - std::clamp(local / bellStage->duration, 0.0, 1.0));
            ofSetColor(255, 210, 140, static_cast<int>(bellAlpha * 255.0f));
            const std::string bellText = "🔔 深呼吸のリズムに合わせてベルが鳴ります";
            if (guideFont_.isLoaded()) {
                const float w = guideFont_.stringWidth(bellText);
                guideFont_.drawString(bellText, (ofGetWidth() - w) * 0.5f, ofGetHeight() * 0.8f);
            } else {
                ofDrawBitmapString(bellText, ofGetWidth() * 0.5f - 160.0f, ofGetHeight() * 0.8f);
            }
        }
    }

    if (!starfieldShaderLoaded_) {
        ofFill();
        ofColor starBase(255, 255, 255, static_cast<unsigned char>(clampedAlpha * 70.0f));
        const int starCount = 140;
        for (int i = 0; i < starCount; ++i) {
            const float u = static_cast<float>(i) / starCount;
            const float x = u * ofGetWidth();
            const float y = std::fmod(static_cast<float>(i) * 47.0f + static_cast<float>(nowSeconds) * 30.0f,
                                      static_cast<float>(ofGetHeight()));
            const float radius = 1.2f + 1.5f * std::sin(nowSeconds * 0.6 + i * 0.18f);
            ofColor starColor = starBase;
            starColor.a = static_cast<unsigned char>(starBase.a * (0.5f + 0.5f * std::sin(nowSeconds * 0.4f + i * 0.3f)));
            ofSetColor(starColor);
            ofDrawCircle(x, y, radius);
        }
    } else if (rippleShaderLoaded_) {
        const float rippleEnvelopeP1 = std::max(envelopeP1, breathingEnvelope);
        const float rippleEnvelopeP2 = std::max(envelopeP2, breathingEnvelope);
        drawRippleLayer(clampedAlpha * 0.6f, nowSeconds, rippleEnvelopeP1, rippleEnvelopeP2);
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
    const std::string title = "First Phase — 心音提示＆同期フェーズ";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(title, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(title, 40, 80);
    }

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
    if (torusShaderLoaded_) {
        torusShader_.begin();
        torusShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
        torusShader_.setUniform1f("uEnvelope", pulseStrength);
        torusShader_.setUniform1f("uAlpha", clampedAlpha);
        mesh.draw();
        torusShader_.end();
    } else {
        mesh.draw();
    }

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

void ofApp::drawExchangeScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);

    ofColor background(16, 12, 32);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    ofSetColor(255, 255, 255, static_cast<int>(clampedAlpha * 210.0f));
    const std::string title = "Exchange — 心音を交換中";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(title, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(title, 40, 80);
    }

    const glm::vec2 leftCenter(ofGetWidth() * 0.35f, ofGetHeight() * 0.52f);
    const glm::vec2 rightCenter(ofGetWidth() * 0.65f, ofGetHeight() * 0.48f);

    const float leftRadius = safeLerp(140.0f, 260.0f, envelopeP2);
    const float rightRadius = safeLerp(140.0f, 260.0f, envelopeP1);

    ofNoFill();
    ofSetLineWidth(7.0f);
    ofColor leftColor(110, 200, 255,
                      static_cast<unsigned char>(clampedAlpha * (120.0f + 120.0f * envelopeP2)));
    ofSetColor(leftColor);
    ofDrawCircle(leftCenter, leftRadius);
    ofColor rightColor(255, 150, 190,
                       static_cast<unsigned char>(clampedAlpha * (120.0f + 120.0f * envelopeP1)));
    ofSetColor(rightColor);
    ofDrawCircle(rightCenter, rightRadius);

    const int particleCount = 32;
    ofFill();
    for (int i = 0; i < particleCount; ++i) {
        const float t = (static_cast<float>(i) + static_cast<float>(nowSeconds) * 0.6f) / particleCount;
        const float blend = std::fmod(t, 1.0f);
        const glm::vec2 pos = glm::mix(leftCenter, rightCenter, blend);
        const float size = 4.0f + 3.0f * std::sin(nowSeconds * 1.4f + i * 0.3f);
        ofColor particleColor = leftColor.getLerped(rightColor, blend);
        particleColor.a = static_cast<unsigned char>(particleColor.a * clampedAlpha * 0.8f);
        ofSetColor(particleColor);
        ofDrawCircle(pos, size);
    }

    ofColor linkColor(200, 180, 255, static_cast<unsigned char>(clampedAlpha * 120.0f));
    ofSetColor(linkColor);
    ofSetLineWidth(3.0f);
    const glm::vec2 topControl(ofGetWidth() * 0.5f, ofGetHeight() * 0.28f);
    const glm::vec2 bottomControl(ofGetWidth() * 0.5f, ofGetHeight() * 0.75f);
    ofDrawBezier(leftCenter.x, leftCenter.y, topControl.x, topControl.y, bottomControl.x, bottomControl.y, rightCenter.x,
                 rightCenter.y);

    ofPopStyle();
}

void ofApp::drawMixedScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);

    ofColor background(18, 14, 24);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    ofSetColor(255, 255, 255, static_cast<int>(clampedAlpha * 210.0f));
    const std::string title = "Mixed — 共鳴のハーモニー";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(title, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(title, 40, 80);
    }

    const float envelope = latestMetrics_.envelope;
    const float noisePhase = static_cast<float>(std::sin(nowSeconds * 0.5));
    const float radius = safeLerp(160.0f, 320.0f, envelope);

    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    const glm::vec2 center(ofGetWidth() * 0.5f, ofGetHeight() * 0.55f);
    mesh.addVertex(glm::vec3(center, 0.0f));
    mesh.addColor(ofFloatColor(0.0f, 0.0f, 0.0f, 0.0f));

    const int segments = 180;
    for (int i = 0; i <= segments; ++i) {
        const float angle = ofDegToRad(static_cast<float>(i) / segments * 360.0f);
        const float wobble = 1.0f + 0.06f * std::sin(nowSeconds * 1.1f + i * 0.27f);
        const float r = radius * wobble;
        const glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * r;
        const float hueMix = 0.5f + 0.5f * std::sin(angle * 3.0f + noisePhase);
        const float envMix = ofLerp(envelopeP1, envelopeP2, (std::sin(angle) + 1.0f) * 0.5f);
        const ofFloatColor color = ofFloatColor::fromHsb(ofWrap(hueMix * 0.08f + 0.55f, 0.0f, 1.0f),
                                                         0.6f + 0.3f * envMix,
                                                         0.55f + 0.35f * envMix,
                                                         clampedAlpha * 0.6f);
        mesh.addVertex(glm::vec3(pos, 0.0f));
        mesh.addColor(color);
    }
    mesh.draw();

    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofSetColor(255, 220, 200, static_cast<int>(clampedAlpha * 120.0f));
    for (int i = 0; i < 5; ++i) {
        const float pulse = static_cast<float>(std::sin(nowSeconds * 0.7f + i * 0.6f) * 0.5f + 0.5f);
        ofDrawCircle(center, safeLerp(40.0f, radius * 0.7f, pulse));
    }
    ofDisableBlendMode();

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
    const std::string title = "End — セッション終了。ログとサマリを確認してください。";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(title, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(title, 40, 80);
    }
    ofPopStyle();
}

void ofApp::drawEnvelopeGraph(const ofRectangle& area) const {
    ofPushStyle();
    ofNoFill();
    ofSetColor(255, 255, 255, 160);
    ofDrawRectangle(area);

    const auto& aggregateSamples = envelopeHistory_.samples();
    if (aggregateSamples.size() < 2) {
        ofPopStyle();
        return;
    }

    const double start = aggregateSamples.front().timestampSec;
    const double end = aggregateSamples.back().timestampSec;
    const double span = std::max(end - start, 1.0);

    auto drawSeries = [&](const std::deque<BeatVisualMetrics>& samples, const ofColor& color) {
        if (samples.size() < 2) {
            return;
        }
        ofPolyline line;
        for (const auto& sample : samples) {
            const double norm = (sample.timestampSec - start) / span;
            const float x = static_cast<float>(area.x + std::clamp(norm, 0.0, 1.0) * area.width);
            const float y = static_cast<float>(area.getBottom() - sample.envelope * area.height);
            line.addVertex(x, y);
        }
        ofSetColor(color);
        line.draw();
    };

    drawSeries(participantEnvelopeHistory_[0].samples(), ofColor(90, 200, 255, 200));
    drawSeries(participantEnvelopeHistory_[1].samples(), ofColor(255, 150, 200, 200));
    drawSeries(aggregateSamples, ofColor(200, 255, 180, 160));

    ofSetColor(255, 200);
    ofDrawBitmapString("Envelope P1/P2 (" + ofToString(span, 1) + "s)", area.x + 4.0f, area.y + 16.0f);
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
    if (audioPipeline_.isCalibrationActive()) {
        return true;
    }
    if (audioPipeline_.isEnvelopeCalibrationActive()) {
        return true;
    }
    if (sceneController_.isTransitioning()) {
        return true;
    }

    const double nowSeconds = ofGetElapsedTimef();
    const SceneState current = sceneController_.currentState();
    const double timeInStateSec = sceneController_.timeInState(nowSeconds);

    if (current == SceneState::Start) {
        double lockUntil = 11.0;
        if (sceneTimingConfig_) {
            if (const auto* stage = sceneTimingConfig_->findStage(SceneState::Start, "textFadeOut")) {
                lockUntil = std::max(lockUntil, stage->startAt + stage->duration);
            }
        }
        if (timeInStateSec < lockUntil) {
            return true;
        }
    }

    if (current == SceneState::End) {
        double lockUntil = 10.0;
        if (sceneTimingConfig_) {
            if (const auto* stage = sceneTimingConfig_->findStage(SceneState::End, "fadeOut")) {
                lockUntil = stage->startAt + stage->duration;
            }
        }
        if (timeInStateSec < lockUntil) {
            return true;
        }
    }

    return false;
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
    if (signalHealth_.fallbackActive) {
        return "実入力が不安定なため推定波形を表示中です。マイク接続とゲインを点検してください。";
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
    if (signalHealth_.fallbackActive) {
        std::ostringstream oss;
        oss << "情報: 推定モード稼働中 — dropout=" << std::fixed << std::setprecision(2)
            << signalHealth_.dropoutSeconds << "s";
        oss << std::defaultfloat;
        lines.push_back({oss.str(), ofColor(255, 220, 140)});
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
