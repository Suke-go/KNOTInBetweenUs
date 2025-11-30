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

// Smoothstep function (equivalent to GLSL smoothstep)
float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Double version for time calculations
double smoothstep(double edge0, double edge1, double x) {
    double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
    auto initialNoiseMode = parseNoiseMode(appConfig_.noiseMode);
    if (initialNoiseMode != knot::audio::AudioPipeline::NoiseMode::SpecSub) {
        ofLogWarning("ofApp")
            << "Spectrum subtraction forced ON by default. Overriding noise mode to SpecSub.";
        initialNoiseMode = knot::audio::AudioPipeline::NoiseMode::SpecSub;
    }
    lastNoiseMode_ = static_cast<int>(initialNoiseMode);
    lastNoiseGateThreshold_ = appConfig_.noiseGateThreshold;
    lastNoiseGateAttenuation_ = appConfig_.noiseGateAttenuation;
    lastSpecSubEnabled_ = true;
    lastSpecSubAlpha_ = appConfig_.noiseSpecSubAlpha;
    lastSpecSubFloor_ = appConfig_.noiseSpecSubFloor;
    lastSpecSubSmoothing_ = appConfig_.noiseSpecSubSmoothing;
    controlPanel_.add(noiseModeParam_.set("Noise Mode (0=Raw,1=Gate,2=SpecSub)", lastNoiseMode_, 0, 2));
    controlPanel_.add(noiseGateThresholdParam_.set("Gate Threshold", lastNoiseGateThreshold_, 0.0f, 1.0f));
    controlPanel_.add(noiseGateAttenuationParam_.set("Gate Attenuation", lastNoiseGateAttenuation_, 0.0f, 1.0f));
    controlPanel_.add(noiseSpecSubEnabledParam_.set("SS Enabled", lastSpecSubEnabled_));
    controlPanel_.add(noiseSpecSubAlphaParam_.set("SS Alpha", lastSpecSubAlpha_, 0.0f, 5.0f));
    controlPanel_.add(noiseSpecSubFloorParam_.set("SS Floor", lastSpecSubFloor_, 0.0f, 0.1f));
    controlPanel_.add(
        noiseSpecSubSmoothingParam_.set("SS Noise Smooth", lastSpecSubSmoothing_, 0.0f, 1.0f));
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
    inputGainDbParam_.addListener(this, &ofApp::onInputGainChanged);
    noiseGainDbParam_.addListener(this, &ofApp::onNoiseGainChanged);
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
    audioPipeline_.setNoiseControlMode(initialNoiseMode);
    audioPipeline_.setNoiseGate(lastNoiseGateThreshold_, lastNoiseGateAttenuation_);
    audioPipeline_.setSpectralSubtractionEnabled(lastSpecSubEnabled_);
    audioPipeline_.setSpectralSubtraction(lastSpecSubAlpha_, lastSpecSubFloor_, lastSpecSubSmoothing_);
    ofLogNotice("ofApp") << "Input gain set to " << appConfig_.inputGainDb << " dB";
    ofLogNotice("ofApp") << "Generated pink noise level set to " << appConfig_.noiseGainDb << " dB";
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
    
    // Initialize participant data with default values to ensure visible output from the start
    // This prevents invisible rendering when no audio input is available
    participantBpms_[0] = 64.0f;
    participantBpms_[1] = 70.0f;
    participantEnvelopes_[0] = 0.5f;
    participantEnvelopes_[1] = 0.48f;
    participantHeartbeatPhase_[0] = 0.0f;
    participantHeartbeatPhase_[1] = 0.0f;  // 左右で同じ位相から開始（視覚的な統一感）

    if (const auto defaultScene = sceneStateFromString(appConfig_.defaultScene)) {
        if (*defaultScene != SceneState::Idle) {
            sceneController_.requestState(*defaultScene, nowSeconds, false, "config_default");
        }
    }

    // Bloom renderer initialization - deferred to first draw() if window size is invalid
    // This will be checked and initialized in draw() if needed
    const int windowWidth = ofGetWidth();
    const int windowHeight = ofGetHeight();
    if (windowWidth > 0 && windowHeight > 0) {
        bloomRenderer_.setup(windowWidth, windowHeight, 8.0f);
        bloomRenderer_.setBloomIntensity(1.5f);
        bloomRenderer_.setExposure(1.0f);
        ofLogNotice("ofApp") << "Bloom renderer initialized: " << windowWidth << "x" << windowHeight;
    } else {
        ofLogWarning("ofApp") << "Window size invalid at setup: " << windowWidth << "x" << windowHeight << ". Bloom renderer will be initialized on first draw.";
    }
}

void ofApp::update() {
    const uint64_t nowMicros = ofGetElapsedTimeMicros();
    const double nowSeconds = static_cast<double>(nowMicros) * 1e-6;

    sceneController_.update(nowSeconds);
    processSceneTransitionEvents();
    
    // Update dynamic panning based on scene state
    const SceneState currentScene = sceneController_.currentState();
    const double timeInState = sceneController_.timeInState(nowSeconds);
    const float transitionBlend = sceneController_.transitionBlend();
    audioRouter_.updateDynamicPanning(currentScene, timeInState, transitionBlend);
    audioRouter_.updateSpatialAudio(currentScene, timeInState);

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
    applyNoiseControlParamsIfChanged();

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

    // Always try to get metrics from AudioPipeline (even in synthetic mode, since we're injecting audio)
    const auto metricsP1 =
        audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant1);
    const auto metricsP2 =
        audioPipeline_.channelMetrics(knot::audio::ParticipantId::Participant2);
    
    // Improved metrics availability check: accept metrics if we have any valid data
    // Even if BPM is not detected, we should still use envelope values (weak signals are still valid)
    // Envelope threshold lowered to 0.0001 to catch very weak signals
    const bool metricsP1Valid = metricsP1.timestampSec > 0.0 && metricsP1.envelope > 0.0001f;
    const bool metricsP2Valid = metricsP2.timestampSec > 0.0 && metricsP2.envelope > 0.0001f;
    const bool metricsAvailable = metricsP1Valid || metricsP2Valid;
    
    // Debug: Log comprehensive state information periodically (every 2 seconds)
    static double lastEnvelopeDebugTime = 0.0;
    if (nowSeconds - lastEnvelopeDebugTime > 2.0) {
        ofLogNotice("ofApp::update") << "=== Update Debug (Idle State) ===";
        ofLogNotice("ofApp::update") << "simulateTelemetry_: " << (simulateTelemetry_ ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "soundStreamActive_: " << (soundStreamActive_ ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "calibrationActive: " << (calibrationActive ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "useSynthetic: " << (useSynthetic ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "Metrics P1 - envelope: " << metricsP1.envelope 
                                     << " | BPM: " << metricsP1.bpm 
                                     << " | timestamp: " << metricsP1.timestampSec
                                     << " | valid: " << (metricsP1Valid ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "Metrics P2 - envelope: " << metricsP2.envelope 
                                     << " | BPM: " << metricsP2.bpm 
                                     << " | timestamp: " << metricsP2.timestampSec
                                     << " | valid: " << (metricsP2Valid ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "metricsAvailable: " << (metricsAvailable ? "YES" : "NO");
        ofLogNotice("ofApp::update") << "participantEnvelopes_[0]: " << participantEnvelopes_[0];
        ofLogNotice("ofApp::update") << "participantEnvelopes_[1]: " << participantEnvelopes_[1];
        ofLogNotice("ofApp::update") << "Current scene: " << sceneStateToString(sceneController_.currentState());
        lastEnvelopeDebugTime = nowSeconds;
    }

    // If using synthetic mode or no valid metrics, use fake signal
    // This ensures that even when audio pipeline returns default values, we still generate visible data
    if (metricsAvailable && !useSynthetic) {
        // Use real metrics from AudioPipeline (works with synthetic heartbeat audio too)
        // Always apply metrics if available, even if BPM is not detected (envelope alone is useful)
        applyBeatMetrics(knot::audio::ParticipantId::Participant1, metricsP1, nowSeconds);
        applyBeatMetrics(knot::audio::ParticipantId::Participant2, metricsP2, nowSeconds);

        const auto eventsP1 =
            audioPipeline_.pollBeatEvents(knot::audio::ParticipantId::Participant1);
        if (!eventsP1.empty()) {
            handleBeatEvents(knot::audio::ParticipantId::Participant1, eventsP1, nowSeconds);
            
            // Heartbeat phase management and ripple generation for Participant1
            // Each event in the vector represents a new heartbeat trigger
            SceneState currentState = sceneController_.currentState();
            double timeInState = sceneController_.timeInState(nowSeconds);
            for (const auto& event : eventsP1) {
                // Reset phase on heartbeat (each event is a trigger)
                participantHeartbeatPhase_[0] = 0.0f;

                // Generate ripple (Start phase 6秒以降、または Exchange phase 30秒以降)
                if (currentState == SceneState::Start && timeInState >= 6.0) {
                    // Start phaseでは中央から波紋を生成
                    ripples_.push_back({
                        nowSeconds,
                        glm::vec2(ofGetWidth() * 0.5f, ofGetHeight() * 0.5f),
                        0.0f
                    });
                } else if (currentState == SceneState::Exchange && timeInState >= 30.0) {
                    // Exchange phaseでは、左側と右側の光の位置から波紋を生成
                    // 光の位置は動的に移動するため、現在の位置を計算
                    const float exchangeProgress = std::clamp(static_cast<float>(timeInState) / 20.0f, 0.0f, 1.0f);
                    const float easedProgress = 0.5f - 0.5f * std::cos(exchangeProgress * glm::pi<float>());
                    
                    // 左側の光の位置（参加者2の心音）
                    const float leftBaseX = ofGetWidth() * 0.32f;
                    const float leftX = leftBaseX + (ofGetWidth() * 0.5f - leftBaseX) * easedProgress;
                    ripples_.push_back({
                        nowSeconds,
                        glm::vec2(leftX, ofGetHeight() * 0.5f),
                        0.0f
                    });
                    
                    // 右側の光の位置（参加者1の心音）
                    const float rightBaseX = ofGetWidth() * 0.68f;
                    const float rightX = rightBaseX - (rightBaseX - ofGetWidth() * 0.5f) * easedProgress;
                    ripples_.push_back({
                        nowSeconds,
                        glm::vec2(rightX, ofGetHeight() * 0.5f),
                        0.0f
                    });
                }
                // FirstPhaseでは波紋を生成しない
            }
        }
        const auto eventsP2 =
            audioPipeline_.pollBeatEvents(knot::audio::ParticipantId::Participant2);
        if (!eventsP2.empty()) {
            handleBeatEvents(knot::audio::ParticipantId::Participant2, eventsP2, nowSeconds);
            
            // Heartbeat phase management for Participant2
            // Each event in the vector represents a new heartbeat trigger
            SceneState currentState = sceneController_.currentState();
            double timeInState = sceneController_.timeInState(nowSeconds);
            for (const auto& event : eventsP2) {
                participantHeartbeatPhase_[1] = 0.0f;
                
                // Generate ripple (Start phase 6秒以降、または Exchange phase 30秒以降)
                // Note: P1のイベントで既に波紋を生成しているため、P2のイベントでは生成しない
                // 両方の心音から波紋を生成したい場合は、P2のイベントでも同様の処理を追加
            }
        }

        limiterReductionDbSmooth_ =
            ofLerp(limiterReductionDbSmooth_, audioPipeline_.lastLimiterReductionDb(), 0.18f);
        signalHealth_ = audioPipeline_.signalHealth();

        // Auto-disable spectral subtraction if MICINPUT3 is missing and re-enable when recovered
        const bool specSubForced = noiseModeParam_.get() == 2;
        if (specSubForced && signalHealth_.specSubAutoDisabled && noiseSpecSubEnabledParam_.get()) {
            noiseSpecSubEnabledParam_.set(false);
            lastSpecSubEnabled_ = false;
            audioPipeline_.setSpectralSubtractionEnabled(false);
            specSubAutoDisabled_ = true;
            ofLogWarning("ofApp")
                << "Spectral subtraction disabled automatically: MICINPUT3 not detected.";
        } else if (specSubForced && specSubAutoDisabled_ && !signalHealth_.specSubAutoDisabled &&
                   !noiseSpecSubEnabledParam_.get()) {
            noiseSpecSubEnabledParam_.set(true);
            lastSpecSubEnabled_ = true;
            audioPipeline_.setSpectralSubtractionEnabled(true);
            specSubAutoDisabled_ = false;
            ofLogNotice("ofApp") << "Spectral subtraction re-enabled: MICINPUT3 restored.";
        }
    } else {
        // Fallback to fake signal if no metrics available or using synthetic mode
        // This ensures visible output even when there's no audio input
        updateFakeSignal(nowSeconds);
        limiterReductionDbSmooth_ = ofLerp(limiterReductionDbSmooth_, 0.0f, 0.15f);
        if (!useSynthetic) {
            signalHealth_ = audioPipeline_.signalHealth();
        }
    }
    
    // Ensure participantBpms_ and participantEnvelopes_ are never zero (fallback to default values)
    // This prevents invisible rendering when data is missing
    for (std::size_t i = 0; i < 2; ++i) {
        if (participantBpms_[i] <= 0.0f || participantBpms_[i] < 30.0f || participantBpms_[i] > 180.0f) {
            // Default to realistic BPM values if invalid
            participantBpms_[i] = (i == 0) ? 64.0f : 70.0f;
        }
        if (participantEnvelopes_[i] <= 0.0f) {
            // Default to a visible envelope value if zero (minimum 0.3 for visibility)
            participantEnvelopes_[i] = 0.3f;
        }
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

    // Update heartbeat phase progression (assuming BPM 60: 1 cycle per second)
    float frameTime = static_cast<float>(ofGetLastFrameTime());
    for (std::size_t i = 0; i < 2; ++i) {
        float bpm = participantBpms_[i];
        if (bpm > 30.0f && bpm < 180.0f) {
            float phaseIncrement = frameTime * (bpm / 60.0f);
            participantHeartbeatPhase_[i] += phaseIncrement;
            if (participantHeartbeatPhase_[i] >= 1.0f) {
                participantHeartbeatPhase_[i] -= 1.0f;
            }
        } else {
            // Fallback: advance phase at 60 BPM
            participantHeartbeatPhase_[i] += frameTime;
            if (participantHeartbeatPhase_[i] >= 1.0f) {
                participantHeartbeatPhase_[i] -= 1.0f;
            }
        }
    }

    // Clean up old ripples
    ripples_.erase(
        std::remove_if(ripples_.begin(), ripples_.end(),
            [nowSeconds](const Ripple& r) {
                // Remove ripples older than 3 seconds (safety margin)
                return nowSeconds - r.birthTime > 3.0;
            }),
        ripples_.end()
    );

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
    // Ensure BloomRenderer is initialized (deferred initialization if window size was 0 at setup)
    if (!bloomRenderer_.isInitialized()) {
        const int windowWidth = ofGetWidth();
        const int windowHeight = ofGetHeight();
        if (windowWidth > 0 && windowHeight > 0) {
            bloomRenderer_.setup(windowWidth, windowHeight, 8.0f);
            bloomRenderer_.setBloomIntensity(1.5f);
            bloomRenderer_.setExposure(1.0f);
            ofLogNotice("ofApp") << "Bloom renderer initialized in draw(): " << windowWidth << "x" << windowHeight;
        }
    }

    // Clear background - all scenes draw their own background
    const SceneState current = sceneController_.currentState();
    if (current != SceneState::FirstPhase) {
        ofBackground(10);
    } else {
        // FirstPhase: Clear with deep blue background (RGB: 5, 6, 12 per specification)
        ofBackground(5, 6, 12);
    }
    
    const double nowSeconds = ofGetElapsedTimef();
    const float blend = sceneController_.transitionBlend();
    const float baseAlpha = sceneController_.isTransitioning() ? blend : 1.0f;
    
    // Log scene state (reduced frequency for performance)
    static int frameCount = 0;
    frameCount++;
    if (current == SceneState::FirstPhase) {
        // Log periodically for FirstPhase (every 60 frames = ~1 second at 60fps)
        if (frameCount <= 60 || frameCount % 60 == 0) {
            ofLogNotice("ofApp::draw") << "FirstPhase Frame #" << frameCount 
                                       << " | Alpha: " << baseAlpha 
                                       << " | Transitioning: " << (sceneController_.isTransitioning() ? "yes" : "no")
                                       << " | Window: " << ofGetWidth() << "x" << ofGetHeight();
        }
    } else if (frameCount % 300 == 0) {
        // Log other scenes less frequently (every 5 seconds)
        ofLogNotice("ofApp::draw") << "Scene: " << sceneStateToString(current) 
                                   << " | Alpha: " << baseAlpha 
                                   << " | Transitioning: " << (sceneController_.isTransitioning() ? "yes" : "no");
    }
    
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

    // Debug mode overlay
    if (debugMode_) {
        ofPushStyle();
        ofSetColor(255, 255, 0);
        float yOffset = ofGetHeight() - 80.0f;
        ofDrawBitmapString("Heartbeat Phase P1: " + ofToString(participantHeartbeatPhase_[0], 2), 20.0f, yOffset);
        ofDrawBitmapString("Heartbeat Phase P2: " + ofToString(participantHeartbeatPhase_[1], 2), 20.0f, yOffset - 20.0f);
        ofDrawBitmapString("Ripples: " + ofToString(ripples_.size()), 20.0f, yOffset - 40.0f);
        ofDrawBitmapString("Bloom Intensity: " + ofToString(bloomRenderer_.getBloomIntensity(), 2), 20.0f, yOffset - 60.0f);
        ofPopStyle();
    }
}

void ofApp::exit() {
    startButton_.removeListener(this, &ofApp::onStartButtonPressed);
    endButton_.removeListener(this, &ofApp::onEndButtonPressed);
    resetButton_.removeListener(this, &ofApp::onResetButtonPressed);
    envelopeCalibrationButton_.removeListener(this, &ofApp::onEnvelopeCalibrationButtonPressed);
    inputGainDbParam_.removeListener(this, &ofApp::onInputGainChanged);
    noiseGainDbParam_.removeListener(this, &ofApp::onNoiseGainChanged);
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
        case 'd':
        case 'D':
            debugMode_ = !debugMode_;
            ofLogNotice("ofApp") << "Debug mode: " << (debugMode_ ? "ON" : "OFF");
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
void ofApp::windowResized(int w, int h) {
    // Reinitialize BloomRenderer with new window size
    bloomRenderer_.setup(w, h, 8.0f);
    bloomRenderer_.setBloomIntensity(1.5f);
    bloomRenderer_.setExposure(1.0f);
    ofLogNotice("ofApp") << "Bloom renderer resized to: " << w << "x" << h;
}
void ofApp::dragEvent(ofDragInfo) {}
void ofApp::gotMessage(ofMessage) {}

void ofApp::audioIn(ofSoundBuffer& input) {
    // Debug: Log audio input status periodically
    static std::size_t audioInCallCount = 0;
    static double lastAudioInDebugTime = 0.0;
    audioInCallCount++;
    const double nowSeconds = ofGetElapsedTimef();
    
    // Log every 2 seconds
    if (nowSeconds - lastAudioInDebugTime > 2.0) {
        ofLogNotice("ofApp::audioIn") << "=== Audio Input Debug ===";
        ofLogNotice("ofApp::audioIn") << "Call count: " << audioInCallCount;
        ofLogNotice("ofApp::audioIn") << "Input channels: " << input.getNumChannels();
        ofLogNotice("ofApp::audioIn") << "Input frames: " << input.getNumFrames();
        ofLogNotice("ofApp::audioIn") << "Sample rate: " << input.getSampleRate();
        ofLogNotice("ofApp::audioIn") << "simulateTelemetry_: " << (simulateTelemetry_ ? "YES" : "NO");
        ofLogNotice("ofApp::audioIn") << "soundStreamActive_: " << (soundStreamActive_ ? "YES" : "NO");
        
        // Calculate input signal level
        if (input.getNumChannels() >= 2 && input.getNumFrames() > 0) {
            float maxL = 0.0f, maxR = 0.0f, maxNoise = 0.0f;
            float rmsL = 0.0f, rmsR = 0.0f, rmsNoise = 0.0f;
            const float* data = input.getBuffer().data();
            const std::size_t numChannels = input.getNumChannels();
            const bool hasNoiseChannel = numChannels > 2;
            for (std::size_t i = 0; i < input.getNumFrames(); ++i) {
                const float left = data[i * numChannels];
                const float right = data[i * numChannels + 1];
                const float noise = hasNoiseChannel ? data[i * numChannels + 2] : 0.0f;
                maxL = std::max(maxL, std::fabs(left));
                maxR = std::max(maxR, std::fabs(right));
                maxNoise = std::max(maxNoise, std::fabs(noise));
                rmsL += left * left;
                rmsR += right * right;
                rmsNoise += noise * noise;
            }
            rmsL = std::sqrt(rmsL / input.getNumFrames());
            rmsR = std::sqrt(rmsR / input.getNumFrames());
            rmsNoise = hasNoiseChannel ? std::sqrt(rmsNoise / input.getNumFrames()) : 0.0f;
            ofLogNotice("ofApp::audioIn") << "Input level - L: max=" << maxL << " rms=" << rmsL
                                          << " | R: max=" << maxR << " rms=" << rmsR
                                          << (hasNoiseChannel ? " | CH3(noise): max=" + ofToString(maxNoise) +
                                                                     " rms=" + ofToString(rmsNoise)
                                                              : " | CH3(noise): N/A");
        }
        lastAudioInDebugTime = nowSeconds;
    }
    
    // If synthetic signal is enabled, generate synthetic heartbeat audio instead of using mic input
    if (simulateTelemetry_) {
        // Ensure synthetic buffer matches input buffer size
        const std::size_t numFrames = input.getNumFrames();
        if (syntheticHeartbeatBuffer_.getNumFrames() != numFrames || 
            syntheticHeartbeatBuffer_.getNumChannels() != 2) {
            syntheticHeartbeatBuffer_.allocate(numFrames, 2);
            syntheticHeartbeatBuffer_.setSampleRate(input.getSampleRate());
        }
        
        // Generate synthetic heartbeat using current BPM from updateFakeSignal()
        // Note: participantBpms_ is updated in update() which runs on main thread,
        // but this should be safe for read-only access
        generateSyntheticHeartbeatBuffer(syntheticHeartbeatBuffer_, 
                                        participantBpms_[0], 
                                        participantBpms_[1]);
        
        // Send synthetic signal to audio pipeline
        audioPipeline_.audioIn(syntheticHeartbeatBuffer_);
    } else {
        // Use real mic input
        // Check if input is valid
        if (input.getNumChannels() < 2) {
            static double lastWarningTime = 0.0;
            if (nowSeconds - lastWarningTime > 5.0) {
                ofLogWarning("ofApp::audioIn") << "Input buffer has less than 2 channels: "
                                               << input.getNumChannels();
                lastWarningTime = nowSeconds;
            }
            return;
        } else if (input.getNumChannels() < 3) {
            static double lastNoiseWarningTime = 0.0;
            if (nowSeconds - lastNoiseWarningTime > 5.0) {
                ofLogWarning("ofApp::audioIn") << "Input buffer has only " << input.getNumChannels()
                                                << " channels. CH3 noise reference is unavailable.";
                lastNoiseWarningTime = nowSeconds;
            }
        }
        audioPipeline_.audioIn(input);
    }
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
    
    // デバッグ: FirstPhaseでのオーディオレベル確認
    static std::size_t audioOutCallCount = 0;
    static double lastAudioDebugLogTime = 0.0;
    static float maxRoutedLevel[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    audioOutCallCount++;
    const double nowSeconds = ofGetElapsedTimef();
    const SceneState currentScene = sceneController_.currentState();
    const bool isFirstPhase = (currentScene == SceneState::FirstPhase);
    const bool shouldLog = isFirstPhase && (nowSeconds - lastAudioDebugLogTime) > 2.0;
    
    for (std::size_t frame = 0; frame < numFrames; ++frame) {
        headphoneFrame_[0] = stereoData[frame * 2];
        headphoneFrame_[1] = stereoData[frame * 2 + 1];

        audioRouter_.route(headphoneFrame_, envelopeFrame_, routedFrame_);
        
        // ルーティング後のレベルを記録（FirstPhaseの場合、ログ出力直前にリセット）
        if (isFirstPhase) {
            for (std::size_t ch = 0; ch < 6; ++ch) {
                maxRoutedLevel[ch] = std::max(maxRoutedLevel[ch], std::fabs(routedFrame_[ch]));
            }
        }

        if (numChannels >= 6) {
            // 6-channel output: CH1-2 (P1 stereo), CH3-4 (P2 stereo), CH5-6 (haptics)
            outputData[frame * numChannels + 0] = routedFrame_[0];  // P1 Left
            outputData[frame * numChannels + 1] = routedFrame_[1];  // P1 Right
            outputData[frame * numChannels + 2] = routedFrame_[2];  // P2 Left
            outputData[frame * numChannels + 3] = routedFrame_[3];  // P2 Right
            outputData[frame * numChannels + 4] = routedFrame_[4];  // Haptic P1
            outputData[frame * numChannels + 5] = routedFrame_[5];  // Haptic P2
        } else if (numChannels >= 4) {
            // 4-channel fallback: P1/P2 stereo only (haptics disabled)
            outputData[frame * numChannels + 0] = routedFrame_[0];  // P1 Left
            outputData[frame * numChannels + 1] = routedFrame_[1];  // P1 Right
            outputData[frame * numChannels + 2] = routedFrame_[2];  // P2 Left
            outputData[frame * numChannels + 3] = routedFrame_[3];  // P2 Right
        } else if (numChannels >= 2) {
            // 2-channel fallback: P1 stereo only
            outputData[frame * numChannels + 0] = routedFrame_[0];  // P1 Left
            outputData[frame * numChannels + 1] = routedFrame_[1];  // P1 Right
        }
    }
    
    // デバッグログ出力（2秒ごと、FirstPhaseの場合のみ）
    if (shouldLog) {
        // AudioPipelineからの出力レベルを計算
        float maxLeft = 0.0f, maxRight = 0.0f;
        float rmsLeft = 0.0f, rmsRight = 0.0f;
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            const float left = stereoData[frame * 2];
            const float right = stereoData[frame * 2 + 1];
            maxLeft = std::max(maxLeft, std::fabs(left));
            maxRight = std::max(maxRight, std::fabs(right));
            rmsLeft += left * left;
            rmsRight += right * right;
        }
        rmsLeft = std::sqrt(rmsLeft / numFrames);
        rmsRight = std::sqrt(rmsRight / numFrames);
        
        ofLogNotice("ofApp::audioOut") << "=== FirstPhase Audio Debug ===";
        ofLogNotice("ofApp::audioOut") << "Pipeline Output: Left max=" << maxLeft << " rms=" << rmsLeft 
            << " | Right max=" << maxRight << " rms=" << rmsRight;
        ofLogNotice("ofApp::audioOut") << "Envelopes: P1=" << envelopeFrame_[0] << " P2=" << envelopeFrame_[1]
            << " | BPM: P1=" << metricsP1.bpm << " P2=" << metricsP2.bpm;
        ofLogNotice("ofApp::audioOut") << "Routed Levels: CH1=" << maxRoutedLevel[0] 
            << " CH2=" << maxRoutedLevel[1] << " CH3=" << maxRoutedLevel[2] 
            << " CH4=" << maxRoutedLevel[3] << " CH5=" << maxRoutedLevel[4] 
            << " CH6=" << maxRoutedLevel[5];
        ofLogNotice("ofApp::audioOut") << "Output channels=" << numChannels;
        
        // リセット
        for (std::size_t ch = 0; ch < 6; ++ch) {
            maxRoutedLevel[ch] = 0.0f;
        }
        lastAudioDebugLogTime = nowSeconds;
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

void ofApp::onInputGainChanged(float& gainDb) {
    audioPipeline_.setInputGainDb(gainDb);
    ofLogNotice("ofApp") << "Mic gain adjusted to " << gainDb << " dB";
}

void ofApp::onNoiseGainChanged(float& gainDb) {
    audioPipeline_.setNoiseGainDb(gainDb);
    ofLogNotice("ofApp") << "Generated pink noise level adjusted to " << gainDb << " dB";
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
    // Generate synthetic heartbeat data that is always visible
    // Use time-based phases to create realistic heartbeat patterns
    const std::array<double, 2> phases{
        nowSeconds * 0.45,
        nowSeconds * 0.58 + 1.1};
    const std::array<float, 2> bpms{
        64.0f + 6.0f * static_cast<float>(std::sin(phases[0] * 0.7)),
        70.0f + 5.0f * static_cast<float>(std::cos(phases[1] * 0.5))};
    // Ensure envelopes are always visible (minimum 0.3 to guarantee visibility)
    const std::array<float, 2> envelopes{
        std::max(0.3f, ofClamp(0.5f + 0.45f * static_cast<float>(std::sin(phases[0])), 0.0f, 1.0f)),
        std::max(0.3f, ofClamp(0.48f + 0.46f * static_cast<float>(std::sin(phases[1] + 0.6)), 0.0f, 1.0f))};

    for (std::size_t idx = 0; idx < 2; ++idx) {
        participantMetrics_[idx].timestampSec = nowSeconds;
        participantMetrics_[idx].bpm = bpms[idx];
        participantMetrics_[idx].envelope = envelopes[idx];
        participantBpms_[idx] = bpms[idx];
        participantEnvelopes_[idx] = envelopes[idx];

        const double beatIntervalSec = 60.0 / std::max(35.0f, bpms[idx]);
        if (nowSeconds - lastSimulatedBeatAt_[idx] >= beatIntervalSec) {
            lastSimulatedBeatAt_[idx] = nowSeconds;
            // Reset heartbeat phase on simulated beat to create visible pulse
            // This ensures the heartbeat light pulses are synchronized with the beat events
            participantHeartbeatPhase_[idx] = 0.0f;
            
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
    
    // Apply amplification to envelope values to make weak signals more visible
    // Envelope values from BeatTimeline may be very small (0.0001 - 0.01 range for weak signals)
    // Amplify and clamp to ensure weak signals are still visible in graphics
    float rawEnvelope = std::max(0.0f, metrics.envelope);
    
    // Apply amplification: multiply by a factor to make weak signals more visible
    // This ensures even weak audio input will produce visible visual feedback
    // Typical envelope values: very weak = 0.0001-0.001, weak = 0.001-0.01, normal = 0.01-0.1, strong = 0.1-1.0
    float amplifiedEnvelope = rawEnvelope;
    
    // Amplify weak signals more aggressively
    if (rawEnvelope > 0.0f && rawEnvelope < 0.1f) {
        // For weak signals, apply stronger amplification
        // Map range [0.0001, 0.1] to [0.2, 0.8] for better visibility
        amplifiedEnvelope = 0.2f + 0.6f * (rawEnvelope / 0.1f);
    } else if (rawEnvelope >= 0.1f) {
        // For stronger signals, use linear mapping with saturation
        amplifiedEnvelope = std::min(1.0f, 0.8f + 0.2f * ((rawEnvelope - 0.1f) / 0.9f));
    }
    // If rawEnvelope is 0, keep it as 0 (no signal)
    
    participantEnvelopes_[*idx] = std::clamp(amplifiedEnvelope, 0.0f, 1.0f);
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

float ofApp::generateHeartbeatSample(double timeSinceBeatStart, double sampleRate) {
    // Generate dual-peak heartbeat pattern (ドクンドクン)
    // Based on generate_test_signals.py's generate_heartbeat()
    // timeSinceBeatStart is in seconds since the start of the current beat
    constexpr float primaryFreq = 80.0f;  // Hz
    constexpr float secondaryFreq = 80.0f;
    const float primaryAmp = std::pow(10.0f, -14.0f / 20.0f);  // -14 dBFS
    const float secondaryAmp = primaryAmp * 0.6f;  // 60% of primary
    constexpr float decay = 0.003f;  // Decay time constant
    constexpr float pulseDuration = 0.04f;  // 40ms pulse duration
    constexpr float secondaryOffset = 0.07f;  // 70ms offset for second peak
    
    float sample = 0.0f;
    
    // First peak (ドクン) - starts at 0.0 seconds
    if (timeSinceBeatStart >= 0.0 && timeSinceBeatStart < pulseDuration) {
        const double localTime = timeSinceBeatStart;
        const int n = static_cast<int>(localTime * sampleRate);
        const int maxN = static_cast<int>(pulseDuration * sampleRate);
        if (n >= 0 && n < maxN) {
            const float env = std::exp(-static_cast<float>(n) / (decay * static_cast<float>(sampleRate)));
            sample += primaryAmp * env * std::sin(2.0f * static_cast<float>(M_PI) * primaryFreq * static_cast<float>(n) / static_cast<float>(sampleRate));
        }
    }
    
    // Second peak (ドクン) - starts at 0.07 seconds (70ms after first peak)
    if (timeSinceBeatStart >= secondaryOffset && timeSinceBeatStart < secondaryOffset + pulseDuration) {
        const double localTime = timeSinceBeatStart - secondaryOffset;
        const int n = static_cast<int>(localTime * sampleRate);
        const int maxN = static_cast<int>(pulseDuration * sampleRate);
        if (n >= 0 && n < maxN) {
            const float env = std::exp(-static_cast<float>(n) / (decay * static_cast<float>(sampleRate)));
            sample += secondaryAmp * env * std::sin(2.0f * static_cast<float>(M_PI) * secondaryFreq * static_cast<float>(n) / static_cast<float>(sampleRate));
        }
    }
    
    // Note: Python code normalizes after generation, but we clamp here for safety
    // The actual normalization happens in the Python version after all samples are generated
    return sample;
}

void ofApp::generateSyntheticHeartbeatBuffer(ofSoundBuffer& buffer, float bpmP1, float bpmP2) {
    const std::size_t numFrames = buffer.getNumFrames();
    if (numFrames == 0) {
        return;
    }
    
    const double sampleRate = buffer.getSampleRate();
    const std::array<float, 2> bpms{bpmP1, bpmP2};
    
    // Ensure buffer is stereo (2 channels)
    if (buffer.getNumChannels() != 2) {
        buffer.allocate(numFrames, 2);
        buffer.setSampleRate(sampleRate);
    }
    
    float* bufferData = buffer.getBuffer().data();
    
    // Track maximum amplitude for normalization (like Python code)
    float maxAmplitude = 0.0f;
    std::vector<float> tempSamples(numFrames * 2, 0.0f);
    
    // Generate heartbeat for each channel
    for (std::size_t channel = 0; channel < 2; ++channel) {
        auto& gen = syntheticHeartbeatGenerators_[channel];
        const float bpm = std::max(30.0f, std::min(180.0f, bpms[channel]));  // Clamp BPM
        const double beatIntervalSeconds = 60.0 / static_cast<double>(bpm);
        const double beatIntervalSamples = beatIntervalSeconds * sampleRate;
        
        for (std::size_t frame = 0; frame < numFrames; ++frame) {
            // Check if we need to start a new beat
            if (gen.totalSamples_ - gen.lastBeatSample_ >= beatIntervalSamples) {
                gen.lastBeatSample_ = gen.totalSamples_;
            }
            
            // Calculate time since beat start in seconds
            const double samplesSinceBeatStart = gen.totalSamples_ - gen.lastBeatSample_;
            const double timeSinceBeatStart = samplesSinceBeatStart / sampleRate;
            
            // Generate sample for current time position
            const float sample = generateHeartbeatSample(timeSinceBeatStart, sampleRate);
            tempSamples[frame * 2 + channel] = sample;
            
            // Track maximum for normalization
            const float absSample = std::abs(sample);
            if (absSample > maxAmplitude) {
                maxAmplitude = absSample;
            }
            
            gen.totalSamples_ += 1.0;
        }
    }
    
    // Normalize to -12 dBFS (like Python code)
    const float targetAmp = std::pow(10.0f, -12.0f / 20.0f);
    const float scale = (maxAmplitude > 0.0f) ? (targetAmp / maxAmplitude) : 1.0f;
    
    // Apply normalization and copy to output buffer
    for (std::size_t i = 0; i < numFrames * 2; ++i) {
        bufferData[i] = std::clamp(tempSamples[i] * scale, -1.0f, 1.0f);
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
    loadShaderFn(realisticLightShader_, realisticLightShaderLoaded_, "shaders/gaussian_blur.vert", "shaders/realistic_light.frag");
    loadShaderFn(amoebaOrganismShader_, amoebaOrganismShaderLoaded_, "shaders/amoeba_organism.vert", "shaders/amoeba_organism.frag");
    
    if (amoebaOrganismShaderLoaded_) {
        ofLogNotice("ofApp") << "Amoeba organism shader loaded successfully";
    } else {
        ofLogWarning("ofApp") << "Failed to load amoeba organism shader - fallback will be used";
    }
}

void ofApp::drawStarfieldLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2, bool idleMode) {
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
    starfieldShader_.setUniform1f("uIdleMode", idleMode ? 1.0f : 0.0f);
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
        if (device.outputChannels < 6) {
            label += " ※6ch推奨";
            if (device.outputChannels < 4) {
                label += " (4ch未対応)";
            }
        } else {
            label += " (6ch対応)";
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
        settings.numInputChannels = std::min<std::size_t>(3, inDevice.inputChannels);
        if (settings.numInputChannels == 0) {
            ofLogWarning("ofApp") << "選択した入力デバイス '" << inDevice.name
                                  << "' は入力チャンネルを提供しません。";
        } else if (settings.numInputChannels < 3) {
            ofLogWarning("ofApp") << "入力チャンネルが " << settings.numInputChannels
                                  << "ch しかありません。CH3 をノイズ参照として使うには"
                                  << " 3ch 以上のデバイスを選択してください。";
        }
    } else {
        settings.numInputChannels = 0;
    }

    const auto& outDevice = outputDevices_[selectedOutputDevice_];
    settings.setOutDevice(outDevice);
    settings.numOutputChannels = std::min<std::size_t>(6, outDevice.outputChannels);
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
        if (settings.numOutputChannels < 6) {
            ofLogWarning("ofApp") << "出力デバイス '" << outDevice.name << "' は "
                                  << settings.numOutputChannels
                                  << "ch までしか対応していません。6ch出力が必要です（P1/P2ステレオ + ハプティクス）。";
            if (settings.numOutputChannels < 4) {
                ofLogWarning("ofApp") << "ハプティクス出力（CH5/CH6）は利用できません。";
            }
        } else {
            ofLogNotice("ofApp") << "出力デバイス '" << outDevice.name << "' を "
                                 << settings.numOutputChannels << "ch で初期化しました。"
                                 << " (CH1-2: P1ステレオ, CH3-4: P2ステレオ, CH5-6: ハプティクス)";
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

knot::audio::AudioPipeline::NoiseMode ofApp::parseNoiseMode(const std::string& mode) const {
    const std::string lower = ofToLower(mode);
    if (lower == "gate") {
        return knot::audio::AudioPipeline::NoiseMode::Gate;
    }
    if (lower == "specsub" || lower == "spectral" || lower == "spec_sub") {
        return knot::audio::AudioPipeline::NoiseMode::SpecSub;
    }
    return knot::audio::AudioPipeline::NoiseMode::SpecSub;
}

void ofApp::applyNoiseControlParamsIfChanged() {
    const int modeSelection = noiseModeParam_.get();
    if (modeSelection != lastNoiseMode_) {
        knot::audio::AudioPipeline::NoiseMode mode = knot::audio::AudioPipeline::NoiseMode::Raw;
        if (modeSelection == 1) {
            mode = knot::audio::AudioPipeline::NoiseMode::Gate;
        } else if (modeSelection == 2) {
            mode = knot::audio::AudioPipeline::NoiseMode::SpecSub;
        }
        audioPipeline_.setNoiseControlMode(mode);
        lastNoiseMode_ = modeSelection;
        ofLogNotice("ofApp") << "Noise mode set to "
                              << (mode == knot::audio::AudioPipeline::NoiseMode::Gate
                                      ? "Gate"
                                      : (mode == knot::audio::AudioPipeline::NoiseMode::SpecSub ? "SpecSub" : "Raw"));
    }

    const float threshold = std::clamp(noiseGateThresholdParam_.get(), 0.0f, 1.0f);
    const float attenuation = std::clamp(noiseGateAttenuationParam_.get(), 0.0f, 1.0f);
    if (std::fabs(threshold - lastNoiseGateThreshold_) > 1e-6f ||
        std::fabs(attenuation - lastNoiseGateAttenuation_) > 1e-6f) {
        audioPipeline_.setNoiseGate(threshold, attenuation);
        lastNoiseGateThreshold_ = threshold;
        lastNoiseGateAttenuation_ = attenuation;
        ofLogNotice("ofApp") << "Noise gate updated - threshold: " << threshold
                              << " attenuation: " << attenuation;
    }

    const bool specSubEnabled = noiseSpecSubEnabledParam_.get();
    if (specSubEnabled != lastSpecSubEnabled_) {
        audioPipeline_.setSpectralSubtractionEnabled(specSubEnabled);
        lastSpecSubEnabled_ = specSubEnabled;
        ofLogNotice("ofApp") << "Spectral subtraction " << (specSubEnabled ? "enabled" : "disabled")
                              << " via control panel.";
    }

    const float alpha = std::clamp(noiseSpecSubAlphaParam_.get(), 0.0f, 5.0f);
    const float floor = std::clamp(noiseSpecSubFloorParam_.get(), 0.0f, 0.1f);
    const float smoothing = std::clamp(noiseSpecSubSmoothingParam_.get(), 0.0f, 1.0f);
    if (std::fabs(alpha - lastSpecSubAlpha_) > 1e-5f || std::fabs(floor - lastSpecSubFloor_) > 1e-5f ||
        std::fabs(smoothing - lastSpecSubSmoothing_) > 1e-5f) {
        audioPipeline_.setSpectralSubtraction(alpha, floor, smoothing);
        lastSpecSubAlpha_ = alpha;
        lastSpecSubFloor_ = floor;
        lastSpecSubSmoothing_ = smoothing;
        ofLogNotice("ofApp") << "Spectral subtraction updated - alpha: " << alpha << " floor: " << floor
                              << " smoothing: " << smoothing;
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
    // DEBUG: Log all transition events
    ofLogNotice("ofApp::handleTransitionEvent") << "Transition: " << sceneStateToString(event.from) 
                                                 << " → " << sceneStateToString(event.to)
                                                 << " (manual: " << (event.manual ? "yes" : "no")
                                                 << ", completed: " << (event.completed ? "yes" : "no")
                                                 << ", reason: " << event.triggerReason << ")";
    
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
        // DEBUG: Special log for FirstPhase transition
        if (event.to == SceneState::FirstPhase) {
            ofLogNotice("ofApp::handleTransitionEvent") << "*** TRANSITIONING TO FIRSTPHASE ***";
        }
        
        // オーディオルーティングを遷移開始時に適用（FirstPhaseなどでは即座に適用が必要）
        // FirstPhase、Exchange、Mixedでは遷移開始時にルーティングを適用
        if (event.to == SceneState::FirstPhase || 
            event.to == SceneState::Exchange || 
            event.to == SceneState::Mixed ||
            event.to == SceneState::End) {
            audioRouter_.applyScenePreset(event.to);
            ofLogNotice("ofApp") << "Audio routing preset applied at transition start for scene: " << sceneStateToString(event.to);
        }
        
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
        // 遷移完了時にもルーティングを再適用（念のため）
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
    // DEBUG: Log drawScene calls for FirstPhase
    static int drawSceneCallCount = 0;
    drawSceneCallCount++;
    if (state == SceneState::FirstPhase && drawSceneCallCount % 60 == 0) {
        ofLogNotice("ofApp::drawScene") << "Drawing FirstPhase scene (call " << drawSceneCallCount 
                                        << ", alpha: " << alpha 
                                        << ", transitioning: " << (sceneController_.isTransitioning() ? "yes" : "no") << ")";
    }
    
    const auto drawLayer = [&](SceneState layerState, float layerAlpha) {
        // DEBUG: Log which layer is being drawn
        if (layerState == SceneState::FirstPhase) {
            static int firstPhaseLayerCount = 0;
            firstPhaseLayerCount++;
            if (firstPhaseLayerCount % 60 == 0) {
                ofLogNotice("ofApp::drawScene::drawLayer") << "Drawing FirstPhase layer (count: " << firstPhaseLayerCount 
                                                            << ", layerAlpha: " << layerAlpha << ")";
            }
        }
        
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
                drawEndScene(layerAlpha, nowSeconds);
                break;
        }
    };

    if (sceneController_.isTransitioning()) {
        const float blend = easedBlend(sceneController_.transitionBlend());
        if (sceneController_.currentState() == SceneState::FirstPhase || 
            sceneController_.targetState() == SceneState::FirstPhase) {
            ofLogNotice("ofApp::drawScene") << "Transitioning with FirstPhase (blend: " << blend 
                                            << ", from: " << sceneStateToString(sceneController_.currentState())
                                            << ", to: " << sceneStateToString(sceneController_.targetState()) << ")";
        }
        drawLayer(sceneController_.currentState(), 1.0f - blend);
        drawLayer(sceneController_.targetState(), blend);
    } else {
        drawLayer(state, std::clamp(alpha, 0.0f, 1.0f));
    }
}

void ofApp::drawIdleScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    
    // Idle状態では音入力に依存せず、時間ベースで美しい星空を表示
    // エンベロープ値は使用せず、固定値（または時間ベースの値）を渡す
    // これにより、音入力が弱くても星空が確実に表示される
    const float timeBasedEnvelope = 0.7f + 0.3f * static_cast<float>(std::sin(nowSeconds * 0.3));
    const float envelopeP1 = timeBasedEnvelope;
    const float envelopeP2 = timeBasedEnvelope;
    
    // Debug: Log envelope values used in drawing periodically
    static double lastDrawDebugTime = 0.0;
    if (nowSeconds - lastDrawDebugTime > 2.0) {
        ofLogNotice("ofApp::drawIdleScene") << "=== DrawIdleScene Debug ===";
        ofLogNotice("ofApp::drawIdleScene") << "Using time-based envelope: " << timeBasedEnvelope;
        ofLogNotice("ofApp::drawIdleScene") << "Actual participantEnvelopes_[0]: " << participantEnvelopes_[0];
        ofLogNotice("ofApp::drawIdleScene") << "Actual participantEnvelopes_[1]: " << participantEnvelopes_[1];
        ofLogNotice("ofApp::drawIdleScene") << "alpha: " << clampedAlpha;
        ofLogNotice("ofApp::drawIdleScene") << "starfieldShaderLoaded_: " << (starfieldShaderLoaded_ ? "YES" : "NO");
        lastDrawDebugTime = nowSeconds;
    }

    // 背景: 深い宇宙の黒（星空エフェクトで補完されるため、シェーダー内で調整）
    ofColor background(2, 3, 8);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    // 星空エフェクト: Idleモードで美しく見える星空を表示
    // アルファ値を最大にして視認性を向上
    // idleMode=trueでシェーダー内のIdle専用処理を有効化
    // 時間ベースのエンベロープ値を使用して、音入力に関係なく星空を表示
    drawStarfieldLayer(clampedAlpha * 1.0f, nowSeconds, envelopeP1, envelopeP2, true);

    // ノイズエフェクト
    drawSubtleNoise(clampedAlpha * 0.8f, nowSeconds);

    // リップルエフェクト（心拍に反応した波紋）- Idle状態では非表示
    // 音入力に依存しない星空表示を優先するため、リップルは無効化
    // drawRippleLayer(clampedAlpha * 0.0f, nowSeconds, envelopeP1, envelopeP2);

    // ハートビートライト - Idle状態では時間ベースで美しく表示
    // 音入力に依存せず、時間ベースのアニメーションで表示
    const float timeBasedLightAlpha = 0.4f + 0.2f * static_cast<float>(std::sin(nowSeconds * 0.5));
    const float lightAlpha1 = clampedAlpha * timeBasedLightAlpha;
    const float lightAlpha2 = clampedAlpha * timeBasedLightAlpha;
    
    // 時間ベースでハートビートの位相を進める（音入力に依存しない）
    const float timeBasedPhase1 = static_cast<float>(std::fmod(nowSeconds * 0.8, 1.0));
    const float timeBasedPhase2 = static_cast<float>(std::fmod(nowSeconds * 0.85, 1.0));
    
    if (bloomRenderer_.isInitialized()) {
        bloomRenderer_.begin();
        drawHeartbeatLight(glm::vec2(ofGetWidth() * 0.32f, ofGetHeight() * 0.5f),
                           timeBasedPhase1, lightAlpha1, 1.0f);
        drawHeartbeatLight(glm::vec2(ofGetWidth() * 0.68f, ofGetHeight() * 0.5f),
                           timeBasedPhase2, lightAlpha2, 1.0f);
        bloomRenderer_.end();
        bloomRenderer_.draw(0, 0);
    } else {
        // Fallback: direct rendering without bloom
        drawHeartbeatLight(glm::vec2(ofGetWidth() * 0.32f, ofGetHeight() * 0.5f),
                           timeBasedPhase1, lightAlpha1, 1.0f);
        drawHeartbeatLight(glm::vec2(ofGetWidth() * 0.68f, ofGetHeight() * 0.5f),
                           timeBasedPhase2, lightAlpha2, 1.0f);
    }

    if (guideFont_.isLoaded()) {
        const std::string text = "準備ができたら、開始してください";
        const float textWidth = guideFont_.stringWidth(text);
        ofSetColor(180, 175, 170, static_cast<int>(clampedAlpha * 200.0f));
        guideFont_.drawString(text, (ofGetWidth() - textWidth) * 0.5f, ofGetHeight() * 0.85f);
    }

    ofPopStyle();
}

void ofApp::drawStartScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const double timeInState = sceneController_.timeInState(nowSeconds);

    float enhancementFactor = smoothstep(8.0f, 9.0f, static_cast<float>(timeInState));
    ofColor background(2 + 3 * enhancementFactor,
                       3 + 3 * enhancementFactor,
                       8 + 4 * enhancementFactor);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    drawSubtleNoise(clampedAlpha, nowSeconds);

    // 2分割構成: 左側と右側に2つの光が独立して表示（中央に収束しない）
    const glm::vec2 leftCenter(ofGetWidth() * 0.32f, ofGetHeight() * 0.5f);
    const glm::vec2 rightCenter(ofGetWidth() * 0.68f, ofGetHeight() * 0.5f);

    // 光の密度が時間の経過とともに大きくなっていく
    const float densityFactor = smoothstep(0.0f, 10.0f, static_cast<float>(timeInState));
    const float lightIntensity = 0.5f + 0.5f * densityFactor;

    if (bloomRenderer_.isInitialized()) {
        bloomRenderer_.begin();
        // 左側: 参加者1の心拍に同期した光
        drawHeartbeatLight(leftCenter, participantHeartbeatPhase_[0], clampedAlpha * lightIntensity, 1.0f);
        // 右側: 参加者2の心拍に同期した光
        drawHeartbeatLight(rightCenter, participantHeartbeatPhase_[1], clampedAlpha * lightIntensity, 1.0f);
        bloomRenderer_.end();
        bloomRenderer_.draw(0, 0);
    } else {
        // Fallback: direct rendering without bloom
        drawHeartbeatLight(leftCenter, participantHeartbeatPhase_[0], clampedAlpha * lightIntensity, 1.0f);
        drawHeartbeatLight(rightCenter, participantHeartbeatPhase_[1], clampedAlpha * lightIntensity, 1.0f);
    }

    // 約15秒以降、心拍に合わせて波紋が広がっていく
    if (timeInState >= 15.0) {
        drawHeartbeatRipples(clampedAlpha, nowSeconds);
    }

    drawStageText(timeInState, clampedAlpha);

    ofPopStyle();
}

void ofApp::drawFirstPhaseScene(float alpha, double nowSeconds) {
    // Log entry for debugging
    static int firstPhaseDrawCount = 0;
    firstPhaseDrawCount++;
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const double timeInPhase = sceneController_.timeInState(nowSeconds);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    
    if (firstPhaseDrawCount <= 10 || firstPhaseDrawCount % 60 == 0) {
        ofLogNotice("ofApp::drawFirstPhaseScene") << "Draw #" << firstPhaseDrawCount 
                                                   << " | alpha: " << clampedAlpha 
                                                   << " | timeInPhase: " << timeInPhase
                                                   << " | window: " << ofGetWidth() << "x" << ofGetHeight()
                                                   << " | BPMs: [" << participantBpms_[0] << ", " << participantBpms_[1] << "]"
                                                   << " | Phases: [" << participantHeartbeatPhase_[0] << ", " << participantHeartbeatPhase_[1] << "]";
    }
    
    ofPushStyle();

    // Step 1: Draw background
    ofColor background(5, 6, 12);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());
    if (firstPhaseDrawCount <= 5) {
        ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 1: Background drawn (RGB: 5, 6, 12, alpha: " << clampedAlpha << ")";
    }
    
    // Step 2: Debug elements (debug mode only)
    if (debugMode_) {
        ofSetColor(255, 0, 0, 255);  // Bright red
        ofDrawRectangle(10, 10, 300, 100);
        ofSetColor(255, 255, 255, 255);
        ofDrawBitmapString("FIRSTPHASE ACTIVE #" + ofToString(firstPhaseDrawCount), 20, 40);
        ofDrawBitmapString("Alpha: " + ofToString(clampedAlpha, 3) + " | Time: " + ofToString(timeInPhase, 2) + "s", 20, 55);
        ofDrawBitmapString("BPM: [" + ofToString(participantBpms_[0], 1) + ", " + ofToString(participantBpms_[1], 1) + "]", 20, 70);
        ofDrawBitmapString("Phase: [" + ofToString(participantHeartbeatPhase_[0], 3) + ", " + ofToString(participantHeartbeatPhase_[1], 3) + "]", 20, 85);
        
        // Draw animated test circles that pulse to show time is passing
        const float pulse = 0.5f + 0.5f * std::sin(nowSeconds * 2.0f);
        ofSetColor(255, 255, 0, 255);  // Bright yellow
        ofDrawCircle(ofGetWidth() * 0.2f, ofGetHeight() * 0.2f, 30.0f + 20.0f * pulse);
        ofDrawCircle(ofGetWidth() * 0.8f, ofGetHeight() * 0.2f, 30.0f + 20.0f * pulse);
    }

    // Step 3: Draw noise texture for subtle background movement
    drawSubtleNoise(clampedAlpha * 0.3f, nowSeconds);
    if (firstPhaseDrawCount <= 5) {
        ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 3: Noise texture drawn";
    }

    // Step 4: Calculate glow parameters
    const float glowGrowth = smoothstep(0.0f, 60.0f, static_cast<float>(timeInPhase));
    const float glowIntensity = 0.3f + 0.7f * glowGrowth;  // 0.3から1.0へ成長
    const float glowSize = 0.3f + 1.2f * glowGrowth;  // 0.3から1.5へ成長（より大きく成長）
    if (firstPhaseDrawCount <= 5) {
        ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 4: Glow calculated (growth: " << glowGrowth 
                                                   << ", intensity: " << glowIntensity 
                                                   << ", size: " << glowSize << ")";
    }

    // Step 5: Calculate light positions and parameters
    const glm::vec2 leftCenter(ofGetWidth() * 0.32f, ofGetHeight() * 0.5f);
    const glm::vec2 rightCenter(ofGetWidth() * 0.68f, ofGetHeight() * 0.5f);
    const bool useBloom = bloomRenderer_.isInitialized();
    const bool shaderLoaded = bloomRenderer_.isShaderLoaded();
    const float phase1 = participantHeartbeatPhase_[0];
    const float phase2 = participantHeartbeatPhase_[1];
    const float lightAlpha = clampedAlpha * glowIntensity;
    const float lightSize = glowSize;
    
    if (firstPhaseDrawCount <= 5) {
        ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 5: Light parameters (Bloom: " << useBloom 
                                                   << ", Shader: " << shaderLoaded
                                                   << ", alpha: " << lightAlpha 
                                                   << ", size: " << lightSize << ")";
    }
    
    // Debug: Draw test circles at light positions (debug mode only)
    if (debugMode_) {
        ofSetColor(0, 255, 0, 200);  // Bright green circles to mark positions
        ofDrawCircle(leftCenter, 80.0f);
        ofDrawCircle(rightCenter, 80.0f);
    }
    
    // Step 6: Draw heartbeat lights
    if (useBloom && shaderLoaded) {
        // Bloom強度も時間とともに増加（光の拡散効果）
        const float bloomGrowth = smoothstep(0.0f, 3.0f, static_cast<float>(timeInPhase));
        const float bloomIntensity = 1.5f + 1.5f * bloomGrowth;  // 1.5から3.0へ成長
        const float previousBloom = bloomRenderer_.getBloomIntensity();
        bloomRenderer_.setBloomIntensity(bloomIntensity);
        
        if (firstPhaseDrawCount <= 5) {
            ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 6: Using BloomRenderer (intensity: " << bloomIntensity << ")";
        }
        
        // Draw lights to FBO with bloom effect
        bloomRenderer_.begin();
        ofPushStyle();
        drawHeartbeatLight(leftCenter, phase1, lightAlpha, lightSize);
        drawHeartbeatLight(rightCenter, phase2, lightAlpha, lightSize);
        ofPopStyle();
        bloomRenderer_.end();
        
        // Draw bloom result to main framebuffer
        // Reset OpenGL state to ensure proper rendering
        ofPushStyle();
        ofSetColor(255, 255, 255, 255);  // Ensure full opacity for FBO texture
        ofDisableBlendMode();  // Disable any active blend mode before drawing FBO
        bloomRenderer_.draw(0, 0);
        ofPopStyle();
        bloomRenderer_.setBloomIntensity(previousBloom);
        
        if (firstPhaseDrawCount <= 5) {
            ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 6: BloomRenderer.draw() called (FBO to main framebuffer)";
        }
    } else {
        if (firstPhaseDrawCount <= 5) {
            ofLogNotice("ofApp::drawFirstPhaseScene") << "  Step 6: Using fallback direct drawing (Bloom: " << useBloom 
                                                       << ", Shader: " << shaderLoaded << ")";
        }
        
        // Fallback: Direct drawing with additive blend mode for visibility on dark background
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        drawHeartbeatLight(leftCenter, phase1, lightAlpha, lightSize);
        drawHeartbeatLight(rightCenter, phase2, lightAlpha, lightSize);
        ofDisableBlendMode();
    }

    // FirstPhaseでは波紋なし（リアルな光のみ）

    ofPopStyle();
}

void ofApp::drawExchangeScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    const float averageEnvelope = 0.5f * (envelopeP1 + envelopeP2);
    const double timeInExchange = sceneController_.timeInState(nowSeconds);

    // 背景: FirstPhaseと同様の深い青色（星空や銀河の表現は使用しない）
    const float bgEnhancement = 0.5f;
    ofColor background(2 + 3 * bgEnhancement,
                       3 + 3 * bgEnhancement,
                       8 + 4 * bgEnhancement);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    drawSubtleNoise(clampedAlpha, nowSeconds);

    // 光が移動して交換している感じの演出
    // 60秒間で完全な交換サイクル（中央で交差後、反対側へ移動）
    const float exchangeDuration = 60.0f;
    const float exchangeProgress = std::fmod(static_cast<float>(timeInExchange), exchangeDuration) / exchangeDuration;
    
    // 交換の動き: 0.0-0.5で中央へ移動、0.5-1.0で反対側へ移動
    float movementProgress;
    if (exchangeProgress < 0.5f) {
        // 前半: 左→中央、右→中央
        movementProgress = exchangeProgress * 2.0f;  // 0.0-1.0
    } else {
        // 後半: 中央→右、中央→左（交換）
        movementProgress = (exchangeProgress - 0.5f) * 2.0f;  // 0.0-1.0
    }
    const float easedProgress = 0.5f - 0.5f * std::cos(movementProgress * glm::pi<float>());  // イーズイン・アウト

    // 2分割構成: 左側と右側に2つの光が独立して表示
    const float leftBaseX = ofGetWidth() * 0.32f;
    const float rightBaseX = ofGetWidth() * 0.68f;
    const float centerY = ofGetHeight() * 0.5f;
    const float centerX = ofGetWidth() * 0.5f;

    // 光の位置が移動する（交換の演出）
    glm::vec2 leftCenter, rightCenter;
    if (exchangeProgress < 0.5f) {
        // 前半: 左→中央、右→中央
        const float leftX = leftBaseX + (centerX - leftBaseX) * easedProgress;
        const float rightX = rightBaseX - (rightBaseX - centerX) * easedProgress;
        leftCenter = glm::vec2(leftX, centerY);
        rightCenter = glm::vec2(rightX, centerY);
    } else {
        // 後半: 中央→右、中央→左（交換）
        const float leftX = centerX + (rightBaseX - centerX) * easedProgress;
        const float rightX = centerX - (centerX - leftBaseX) * easedProgress;
        leftCenter = glm::vec2(leftX, centerY);
        rightCenter = glm::vec2(rightX, centerY);
    }

    // Glowの強度: 常に明るく保つ（可視性を確保）
    const float glowIntensity = 0.9f + 0.1f * (1.0f - easedProgress * 0.5f);  // 0.9-1.0の範囲
    const float bloomIntensity = 2.0f + 0.5f * (1.0f - easedProgress * 0.5f);
    const float previousBloom = bloomRenderer_.getBloomIntensity();
    bloomRenderer_.setBloomIntensity(bloomIntensity);
    bloomRenderer_.begin();

    // 左側: 参加者2の心拍に同期（パートナーの心音）
    const float phase2 = participantHeartbeatPhase_[1];
    const float flash2 = 0.85f + 0.15f * (smoothstep(0.0f, 0.3f, phase2) - smoothstep(0.3f, 1.0f, phase2));
    const float alpha2 = std::max(0.5f, clampedAlpha * glowIntensity * flash2);
    drawHeartbeatLight(leftCenter, phase2, alpha2, 1.0f);

    // 右側: 参加者1の心拍に同期（パートナーの心音）
    const float phase1 = participantHeartbeatPhase_[0];
    const float flash1 = 0.85f + 0.15f * (smoothstep(0.0f, 0.3f, phase1) - smoothstep(0.3f, 1.0f, phase1));
    const float alpha1 = std::max(0.5f, clampedAlpha * glowIntensity * flash1);
    drawHeartbeatLight(rightCenter, phase1, alpha1, 1.0f);

    bloomRenderer_.end();
    bloomRenderer_.draw(0, 0);
    bloomRenderer_.setBloomIntensity(previousBloom);

    // Exchange phaseでは常に波紋を表示（光の位置から直接生成）
    // 波紋の強度は時間とともに増加
    const float rippleIntensity = smoothstep(0.0f, 10.0f, static_cast<float>(timeInExchange));
    drawExchangeRipplesFromLights(clampedAlpha * rippleIntensity, nowSeconds, leftCenter, rightCenter, phase1, phase2);

    ofPopStyle();
}

void ofApp::drawMixedScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    const float averageEnvelope = 0.5f * (envelopeP1 + envelopeP2);

    // 背景
    ofColor background(18, 14, 24);
    background.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(background);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());

    // 背景グローの呼吸感（最小輝度を確保しつつ8-12秒周期で揺らぐ）
    const float breathingPeriod = 10.0f;
    const float breathingPhase = static_cast<float>(nowSeconds * glm::two_pi<double>() / breathingPeriod);
    const float breathingGlow = 0.15f + 0.85f * (0.5f + 0.5f * std::sin(breathingPhase));
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofSetColor(60, 50, 80, static_cast<unsigned char>(clampedAlpha * breathingGlow * 90.0f));
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());
    ofDisableBlendMode();

    // テキスト表示を削除（USER_EXPERIENCE_PHASES.mdの要件に従って）

    const glm::vec2 center(ofGetWidth() * 0.5f, ofGetHeight() * 0.55f);
    const float envelope = latestMetrics_.envelope;
    const float easedEnvelope = 0.5f - 0.5f * std::cos(std::clamp(envelope, 0.0f, 1.0f) * glm::pi<float>());
    const float baseRadius = safeLerp(160.0f, 320.0f, easedEnvelope);

    // 2つの心拍の位相を取得（0.0-1.0の範囲をラジアンに変換）
    const float phase1 = participantHeartbeatPhase_[0] * 2.0f * glm::pi<float>();
    const float phase2 = participantHeartbeatPhase_[1] * 2.0f * glm::pi<float>();

    // 2つの心拍の周波数（BPMから計算、最小値と最大値を設定）
    const float freq1 = std::max(0.5f, std::min(2.0f, participantBpms_[0] / 60.0f));
    const float freq2 = std::max(0.5f, std::min(2.0f, participantBpms_[1] / 60.0f));

    // 2つの心拍の同期度を計算（位相差に基づく）
    float phaseDiff = std::abs(phase1 - phase2);
    // 位相差を0-πの範囲に正規化
    if (phaseDiff > glm::pi<float>()) {
        phaseDiff = 2.0f * glm::pi<float>() - phaseDiff;
    }
    const float syncLevel = 1.0f - (phaseDiff / glm::pi<float>());

    // 同期瞬間の波紋を追加（位相差が十分に小さいときのみ）
    const float syncRippleThreshold = 0.1f;
    const double minSyncRippleInterval = 0.8;
    if (phaseDiff < syncRippleThreshold && (nowSeconds - lastMixedSyncRippleTime_) > minSyncRippleInterval) {
        ripples_.push_back({nowSeconds, center, baseRadius * 0.12f});
        lastMixedSyncRippleTime_ = nowSeconds;
    }

    // 時間ベースのオフセット（連続性のための）
    const float angleOffset = static_cast<float>(nowSeconds * 0.1f);

    // 高解像度なセグメント分割（360以上）
    const int segments = 360;

    // メッシュの生成
    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    mesh.addVertex(glm::vec3(center, 0.0f));
    mesh.addColor(ofFloatColor(0.0f, 0.0f, 0.0f, 0.0f));

    for (int i = 0; i <= segments; ++i) {
        // 角度計算（0から2πまで、最後の点は最初の点と同じ角度に）
        const float normalizedAngle = (i == segments) ? 1.0f : (static_cast<float>(i) / segments);
        float angle = normalizedAngle * 2.0f * glm::pi<float>() + angleOffset;

        // 2つの心拍の波の干渉を計算
        // 空間的な波の干渉: 角度位置に基づいて各波源からの距離を計算
        // 各角度位置での波の位相（空間的な位相と時間的な位相の組み合わせ）
        const float spatialPhase1 = angle * 2.0f;  // 空間的な位相
        const float spatialPhase2 = angle * 2.0f;
        const float wavePhase1 = phase1 + spatialPhase1 * freq1;
        const float wavePhase2 = phase2 + spatialPhase2 * freq2;

        // 2つの波（エンベロープで振幅を調整）
        const float wave1 = std::sin(wavePhase1) * envelopeP1;
        const float wave2 = std::sin(wavePhase2) * envelopeP2;

        // 波の干渉（建設的干渉と破壊的干渉）
        const float interference = wave1 + wave2;
        // 干渉の強度（0-2の範囲を0-1に正規化）
        const float interferenceStrength = (interference + 2.0f) * 0.25f;  // 0-1の範囲

        // 高次調波を追加（有機的な不規則性）
        const float harmonic2 = 0.15f * std::sin(phase1 * 2.0f + spatialPhase1 * freq1 * 2.0f) * envelopeP1;
        const float harmonic3 = 0.1f * std::sin(phase2 * 3.0f + spatialPhase2 * freq2 * 3.0f) * envelopeP2;

        // フラクタルノイズ（有機的な不規則性、より強く）
        // 複数のオクターブのノイズを重ねる
        const float noiseTime = static_cast<float>(nowSeconds * 0.5f);
        float noiseValue = 0.0f;
        float noiseAmplitude = 1.0f;
        float noiseFrequency = 1.0f;
        
        // フラクタルノイズ（4オクターブ）
        for (int octave = 0; octave < 4; ++octave) {
            const float noiseAngle = angle * noiseFrequency + noiseTime;
            const float noiseRadius = normalizedAngle * noiseFrequency * 2.0f + noiseTime * 0.3f;
            noiseValue += noiseAmplitude * std::sin(noiseAngle * 7.0f + noiseRadius * 5.0f) *
                         std::cos(noiseAngle * 11.0f + noiseRadius * 3.0f);
            noiseAmplitude *= 0.5f;
            noiseFrequency *= 2.0f;
        }
        const float contourRoughness = 1.0f + 0.1f * (1.0f - syncLevel);  // 非同期時に輪郭をわずかに粗く
        noiseValue *= 0.15f * contourRoughness;  // ノイズの強度を調整（0.15 = 15%の変形）

        // 最終的な半径（干渉パターンによる変形 + ノイズによる有機的な変形）
        // 建設的干渉（interferenceStrengthが大きい）ほど半径が大きくなる
        const float radiusVariation = 0.8f + 0.4f * interferenceStrength + 0.1f * harmonic2 + 0.05f * harmonic3 + noiseValue;
        const float r = baseRadius * std::max(0.4f, std::min(1.6f, radiusVariation));

        // 位置計算
        const glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * r;

        // 距離による透明度の変化（中心部は明るく、外側へ向かって暗くなる）
        // 半径の正規化された距離を使用
        const float normalizedRadius = r / baseRadius;
        const float alphaFalloff = 1.0f - smoothstep(0.3f, 1.2f, normalizedRadius);

        // 色彩の計算（心拍同期による統一）
        // 同期度が高いほど、色彩が統一される
        // 色相範囲: 紫(0.8) → ピンク(0.9) → オレンジ(0.05)
        // 同期度が低い時: 紫からオレンジまで広い範囲
        // 同期度が高い時: ピンク中心に統一
        const float baseHue = 0.8f + 0.1f * (1.0f - syncLevel);  // 0.8-0.9（紫-ピンク系）
        const float hueVariation = 0.4f * (1.0f - syncLevel);  // 同期度が高いほど変化が小さい（最大0.4で0.0-0.1のオレンジ範囲もカバー）
        const float hueLfo = 0.016f * std::sin(static_cast<float>(nowSeconds) * 0.6f);  // 色相だけを揺らすLFO（±約5.7度）

        // 角度に基づく色相の変化
        const float angleHue = baseHue + hueVariation * std::sin(angle * 3.0f + nowSeconds * 0.2f) + hueLfo;

        // 彩度・明度は同期度をベースにエンベロープでゆるやかにスケーリング
        const float envMix = ofLerp(envelopeP1, envelopeP2, (std::sin(angle) + 1.0f) * 0.5f);
        const float saturation = ofLerp(0.55f, ofLerp(0.75f, 0.95f, syncLevel), envMix);
        const float brightness = ofLerp(0.5f, ofLerp(0.75f, 0.95f, syncLevel), envMix);

        // 最終的な色
        const ofFloatColor color = ofFloatColor::fromHsb(
            ofWrap(angleHue, 0.0f, 1.0f),
            saturation,
            brightness,
            clampedAlpha * 0.6f * alphaFalloff
        );

        mesh.addVertex(glm::vec3(pos, 0.0f));
        mesh.addColor(color);
    }
    
    // メッシュを描画する前に、shaderベースの背景レイヤーを追加
    // より美しい波紋効果をshaderで実現
    if (rippleShaderLoaded_) {
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        drawRippleLayer(clampedAlpha * 0.3f, nowSeconds, envelopeP1, envelopeP2);
        ofDisableBlendMode();
    }
    
    // メッシュを描画（noiseベースの変形が適用された有機的な形状）
    mesh.draw();

    // 光の拡散：オーロラのような光の流れ
    // BloomRendererを使用してより美しい光の拡散を実現
    bloomRenderer_.begin();

    // 中心の明るい光（同期度に応じて強度が変化）
    const float centerIntensity = 0.6f + 0.4f * syncLevel;
    ofSetColor(255, 240, 220, static_cast<int>(clampedAlpha * centerIntensity * 200.0f));
    ofFill();
    ofDrawCircle(center, baseRadius * 0.35f);
    
    // 追加のグロー効果（より柔らかい光の拡散）
    ofSetColor(255, 245, 235, static_cast<int>(clampedAlpha * centerIntensity * 120.0f));
    ofDrawCircle(center, baseRadius * 0.5f);

    bloomRenderer_.end();
    bloomRenderer_.draw(0, 0);

    // 追加の光の層（アンビエントな拡散）
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    for (int layer = 0; layer < 3; ++layer) {
        const float layerSpeed = 0.3f + layer * 0.2f;
        const float layerRadius = baseRadius * (0.6f + layer * 0.15f);
        float layerAlpha = clampedAlpha * (1.0f - layer * 0.25f) * 0.3f;

        // オーロラのような流動的な動き
        const float flowAngle = static_cast<float>(nowSeconds * layerSpeed + layer * glm::pi<float>() * 0.5f);
        const float flowRadius = layerRadius * (1.0f + 0.08f * std::sin(flowAngle));

        // 心拍のリズムに合わせた脈動
        const float pulse1 = 0.5f + 0.5f * std::sin(phase1);
        const float pulse2 = 0.5f + 0.5f * std::sin(phase2);
        const float combinedPulse = (pulse1 + pulse2) * 0.5f;

        const float finalRadius = flowRadius * (0.75f + 0.25f * combinedPulse);

        if (layer == 0) {
            layerAlpha *= std::clamp(0.6f + 0.4f * averageEnvelope, 0.0f, 1.0f);
        } else {
            layerAlpha *= syncLevel;  // 位相差が大きいほど外層の透過度を下げる
        }

        ofSetColor(255, 220, 200, static_cast<int>(layerAlpha * 255.0f));
        ofDrawCircle(center, finalRadius);
    }
    ofDisableBlendMode();

    // 同期時の波紋を描画（追加フィードバック）
    drawHeartbeatRipples(clampedAlpha * 0.35f, nowSeconds);

    ofPopStyle();
}

void ofApp::drawEndScene(float alpha, double nowSeconds) {
    ofPushStyle();
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    
    // 暗い背景（生命体の輝きを際立たせる）
    ofColor base(8, 6, 12);
    base.a = static_cast<unsigned char>(clampedAlpha * 255.0f);
    ofSetColor(base);
    ofDrawRectangle(0, 0, ofGetWidth(), ofGetHeight());
    
    // アメーバ生命体を描画（BloomRendererを使用してより美しいグロー効果を実現）
    // まず直接描画して、その後にBloomを適用
    if (bloomRenderer_.isInitialized()) {
        // Bloomレンダリング
        bloomRenderer_.setBloomIntensity(3.5f);
        bloomRenderer_.setExposure(1.3f);
        bloomRenderer_.begin();
        drawAmoebaOrganism(clampedAlpha, nowSeconds);
        bloomRenderer_.end();
        bloomRenderer_.draw(0, 0);
        
        // さらに直接描画して強度を上げる（オーバーレイ）
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        drawAmoebaOrganism(clampedAlpha * 0.3f, nowSeconds);
        ofDisableBlendMode();
    } else {
        // Fallback: 直接描画（ブレンドモードを追加）
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        drawAmoebaOrganism(clampedAlpha, nowSeconds);
        ofDisableBlendMode();
    }
    
    // テキスト表示（控えめに）
    ofSetColor(200, 180, 180, static_cast<int>(clampedAlpha * 150.0f));
    const std::string title = "End — セッション終了。ログとサマリを確認してください。";
    if (guideFont_.isLoaded()) {
        guideFont_.drawString(title, 40.0f, 80.0f);
    } else {
        ofDrawBitmapString(title, 40, 80);
    }
    ofPopStyle();
}

void ofApp::drawAmoebaOrganism(float alpha, double nowSeconds) {
    if (!amoebaOrganismShaderLoaded_) {
        // フォールバック: シンプルな円を描画（デバッグ用に目立つ色）
        ofSetColor(255, 100, 100, static_cast<int>(alpha * 255.0f));
        ofFill();
        ofDrawCircle(ofGetWidth() * 0.5f, ofGetHeight() * 0.5f, 150.0f);
        ofSetColor(255, 255, 255);
        ofDrawBitmapString("Amoeba shader not loaded", ofGetWidth() * 0.5f - 100, ofGetHeight() * 0.5f);
        return;
    }
    
    const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const float envelopeP1 = std::clamp(participantEnvelopes_[0], 0.0f, 1.0f);
    const float envelopeP2 = std::clamp(participantEnvelopes_[1], 0.0f, 1.0f);
    
    // 2つの心拍の位相を取得
    const float phase1 = participantHeartbeatPhase_[0];
    const float phase2 = participantHeartbeatPhase_[1];
    
    // 同期度の計算
    const float phase1Rad = phase1 * 2.0f * glm::pi<float>();
    const float phase2Rad = phase2 * 2.0f * glm::pi<float>();
    float phaseDiff = std::abs(phase1Rad - phase2Rad);
    if (phaseDiff > glm::pi<float>()) {
        phaseDiff = 2.0f * glm::pi<float>() - phaseDiff;
    }
    const float syncLevel = 1.0f - (phaseDiff / glm::pi<float>());
    
    // ライトの位置（心拍に応じて動的に変化）
    const float lightOffset1 = 0.15f * std::sin(phase1Rad + nowSeconds * 0.3f);
    const float lightOffset2 = 0.15f * std::cos(phase2Rad + nowSeconds * 0.35f);
    
    // 同期度が高いほど、ライトが中央に近づく
    const float syncFactor = syncLevel;
    const float leftLightX = 0.4f - 0.1f * (1.0f - syncFactor) + lightOffset1 * 0.3f;
    const float rightLightX = 0.6f + 0.1f * (1.0f - syncFactor) + lightOffset2 * 0.3f;
    const float lightY = 0.5f + 0.1f * std::sin(nowSeconds * 0.2f);
    
    glm::vec2 light1(leftLightX, lightY);
    glm::vec2 light2(rightLightX, lightY);
    
    // ライトの強度（心拍とエンベロープに基づく）
    const float pulse1 = 0.5f + 0.5f * std::sin(phase1Rad);
    const float pulse2 = 0.5f + 0.5f * std::sin(phase2Rad);
    const float lightIntensity1 = 0.7f + 0.3f * pulse1 * envelopeP1;
    const float lightIntensity2 = 0.7f + 0.3f * pulse2 * envelopeP2;
    
    // シェーダーを開始（starfieldShaderと同じ方法を使用）
    ofPushStyle();
    ofFill();
    amoebaOrganismShader_.begin();
    
    // ユニフォーム変数の設定
    amoebaOrganismShader_.setUniform2f("uResolution", static_cast<float>(ofGetWidth()), static_cast<float>(ofGetHeight()));
    amoebaOrganismShader_.setUniform1f("uTime", static_cast<float>(nowSeconds));
    amoebaOrganismShader_.setUniform1f("uPhase1", phase1);
    amoebaOrganismShader_.setUniform1f("uPhase2", phase2);
    amoebaOrganismShader_.setUniform1f("uEnvelope1", envelopeP1);
    amoebaOrganismShader_.setUniform1f("uEnvelope2", envelopeP2);
    amoebaOrganismShader_.setUniform1f("uSyncLevel", syncLevel);
    amoebaOrganismShader_.setUniform2f("uLight1", light1.x, light1.y);
    amoebaOrganismShader_.setUniform2f("uLight2", light2.x, light2.y);
    amoebaOrganismShader_.setUniform1f("uLightIntensity1", lightIntensity1);
    amoebaOrganismShader_.setUniform1f("uLightIntensity2", lightIntensity2);
    amoebaOrganismShader_.setUniform1f("uAlpha", clampedAlpha);
    
    // 全画面クワッドを描画
    fullscreenQuadMesh().draw();
    
    amoebaOrganismShader_.end();
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

void ofApp::drawHeartbeatLight(const glm::vec2& position, float phase, float alpha, float sizeScale) {
    // DEBUG: Validate inputs
    if (alpha <= 0.0f) {
        static int zeroAlphaCount = 0;
        if (zeroAlphaCount++ < 5) {
            ofLogWarning("ofApp::drawHeartbeatLight") << "Called with zero alpha! position: (" 
                                                       << position.x << ", " << position.y 
                                                       << "), phase: " << phase 
                                                       << ", sizeScale: " << sizeScale;
        }
        return;  // Skip drawing if alpha is zero
    }
    
    // sizeScaleパラメータを追加（デフォルト1.0）
    if (sizeScale <= 0.0f) sizeScale = 1.0f;
    
    // Calculate heartbeat pulse
    float systole = smoothstep(0.0f, 0.3f, phase);
    float diastole = smoothstep(0.3f, 1.0f, phase);
    float pulse = 0.75f + 0.25f * (systole - diastole);
    
    // 時間的な流動性（オーロラのような動き）
    double currentTime = ofGetElapsedTimef();
    float timeFlow = static_cast<float>(currentTime) * 0.3f;
    
    // ノイズベースの流動的な変形（簡易版）
    float noiseScale = 0.01f;
    float flowX = ofNoise(position.x * noiseScale + timeFlow, position.y * noiseScale) - 0.5f;
    float flowY = ofNoise(position.x * noiseScale, position.y * noiseScale + timeFlow * 0.7f) - 0.5f;
    glm::vec2 flowOffset(flowX * 8.0f, flowY * 8.0f);  // より控えめな流動性
    
    ofColor warmWhite(255, 245, 230);  // わずかに黄色がかった白（電球の色）
    
    // リアルな電球の光: より多くの層で滑らかなグラデーションを実現
    // 層数を増やして、より滑らかなグラデーションにする
    const int numLayers = 20;  // 6層から20層に増加（より滑らかに）
    const float maxRadius = 200.0f * sizeScale;  // 最大半径（sizeScaleを適用）
    
    // 各層を描画（外側から内側へ、滑らかなグラデーション）
    ofEnableBlendMode(OF_BLENDMODE_ADD);  // 加算ブレンドでより滑らかに
    for (int i = numLayers - 1; i >= 0; i--) {
        // 半径を滑らかに分布（ガウシアン風の分布）
        float t = static_cast<float>(i) / static_cast<float>(numLayers - 1);  // 0.0 (外側) から 1.0 (中心)
        float radius = maxRadius * (0.1f + 0.9f * t) * pulse;  // 中心から外側へ滑らかに
        
        // アルファ値を滑らかなガウシアンカーブで計算（中心が明るく、外側が暗い）
        float gaussian = std::exp(-t * t * 3.0f);  // ガウシアン関数（3.0は広がりのパラメータ）
        float layerAlpha = alpha * pulse * gaussian;
        
        // 外側の層はより柔らかく（距離による減衰）
        float distanceAttenuation = 1.0f / (1.0f + (1.0f - t) * 2.0f);
        layerAlpha *= distanceAttenuation;
        
        // DEBUG: Skip if alpha is too low to be visible
        if (layerAlpha < 0.005f) {
            continue;
        }
        
        // オーロラのような流動的な変形を適用（外側ほど強く）
        float flowStrength = (1.0f - t) * 0.1f;  // 外側ほど変形が強い
        glm::vec2 layerPosition = position + flowOffset * flowStrength;
        
        // 滑らかなグロー（各層を1つの円で描画、重ね合わせが目立たないように）
        warmWhite.a = static_cast<unsigned char>(std::min(255.0f, layerAlpha * 255.0f));
        ofSetColor(warmWhite);
        ofFill();
        ofDrawCircle(layerPosition, radius);
    }
    ofDisableBlendMode();
    
    // 追加: 散乱光の表現（微細な粒子による散乱）
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    for (int i = 0; i < 2; i++) {
        float scatterRadius = 180.0f * pulse * sizeScale * (1.0f + i * 0.4f);
        float scatterAlpha = alpha * 0.04f * pulse / (1.0f + i * 0.5f);
        warmWhite.a = static_cast<unsigned char>(scatterAlpha * 255.0f);
        ofSetColor(warmWhite);
        ofDrawCircle(position + flowOffset * (i + 1) * 0.15f, scatterRadius);
    }
    ofDisableBlendMode();
}

void ofApp::drawHeartbeatLightRealistic(const glm::vec2& position, float phase, float alpha, float sizeScale, double nowSeconds) {
    if (!realisticLightShaderLoaded_) {
        // フォールバック: CPUベースの描画
        drawHeartbeatLight(position, phase, alpha, sizeScale);
        return;
    }
    
    // シェーダーベースの描画（将来の拡張用）
    // 現在はCPUベースの実装を使用
    drawHeartbeatLight(position, phase, alpha, sizeScale);
}

void ofApp::drawSubtleNoise(float alpha, double nowSeconds) {
    // Perlin noise for subtle background movement
    ofMesh mesh;
    mesh.setMode(OF_PRIMITIVE_TRIANGLES);
    
    int gridSize = 8;
    float cellW = ofGetWidth() / static_cast<float>(gridSize);
    float cellH = ofGetHeight() / static_cast<float>(gridSize);
    
    for (int y = 0; y < gridSize; y++) {
        for (int x = 0; x < gridSize; x++) {
            float noise = ofNoise(x * 0.1f, y * 0.1f, nowSeconds * 0.01f);
            unsigned char noiseAlpha = static_cast<unsigned char>(alpha * noise * 8.0f);
            
            glm::vec2 p0(x * cellW, y * cellH);
            glm::vec2 p1((x + 1) * cellW, y * cellH);
            glm::vec2 p2((x + 1) * cellW, (y + 1) * cellH);
            glm::vec2 p3(x * cellW, (y + 1) * cellH);
            
            ofColor c(5, 4, 10, noiseAlpha);
            
            // Triangle 1
            mesh.addVertex(glm::vec3(p0, 0));
            mesh.addColor(c);
            mesh.addVertex(glm::vec3(p1, 0));
            mesh.addColor(c);
            mesh.addVertex(glm::vec3(p2, 0));
            mesh.addColor(c);
            
            // Triangle 2
            mesh.addVertex(glm::vec3(p0, 0));
            mesh.addColor(c);
            mesh.addVertex(glm::vec3(p2, 0));
            mesh.addColor(c);
            mesh.addVertex(glm::vec3(p3, 0));
            mesh.addColor(c);
        }
    }
    
    mesh.draw();
}

void ofApp::drawHeartbeatRipples(float alpha, double nowSeconds) {
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(3.0f);
    
    const float speed = 150.0f;  // pixels per second
    const float maxAge = 2.0f;   // seconds
    
    for (const auto& ripple : ripples_) {
        float age = static_cast<float>(nowSeconds - ripple.birthTime);
        if (age > maxAge) continue;
        
        float radius = age * speed;
        float rippleAlpha = (1.0f - age / maxAge) * alpha;
        
        ofColor warmWhite(251, 245, 231);
        warmWhite.a = static_cast<unsigned char>(rippleAlpha * 180.0f);
        ofSetColor(warmWhite);
        
        ofDrawCircle(ripple.position, radius);
    }
    
    ofPopStyle();
}

void ofApp::drawEnhancedHeartbeatRipples(float alpha, double nowSeconds, float riseUpFactor, 
                                          const glm::vec2& leftLightPos, const glm::vec2& rightLightPos) {
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(3.0f);
    
    float speed = 150.0f + 50.0f * riseUpFactor;  // 150 -> 200 px/s
    float maxRadius = 300.0f + 200.0f * riseUpFactor;  // 300 -> 500 px
    float maxAge = maxRadius / speed;
    
    for (const auto& ripple : ripples_) {
        float age = static_cast<float>(nowSeconds - ripple.birthTime);
        if (age > maxAge) continue;
        
        float radius = age * speed;
        float rippleAlpha = (1.0f - age / maxAge) * alpha;
        
        ofColor warmWhite(251, 245, 231);
        warmWhite.a = static_cast<unsigned char>(rippleAlpha * 180.0f);
        ofSetColor(warmWhite);
        
        // ripple.positionを使用（各光の位置から波紋が広がる）
        ofDrawCircle(ripple.position, radius);
    }
    
    ofPopStyle();
}

void ofApp::drawExchangeRipplesFromLights(float alpha, double nowSeconds, 
                                          const glm::vec2& leftLightPos, const glm::vec2& rightLightPos,
                                          float phase1, float phase2) {
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.5f);
    
    const float speed = 200.0f;  // pixels per second
    const float maxAge = 2.5f;   // seconds
    const float minBeatInterval = 0.4f;  // 最小ビート間隔（秒）
    
    // 心拍に基づいて波紋を生成
    // 各ビートごとに波紋を生成（位相が0.0に近い時）
    // 位相が0.3未満の場合、新しいビートとして検出
    if (phase1 < 0.3f && (nowSeconds - lastExchangeRippleTime1_) >= minBeatInterval) {
        lastExchangeRippleTime1_ = nowSeconds;
        // 右側の光から波紋を生成
        ripples_.push_back({
            nowSeconds,
            rightLightPos,
            0.0f
        });
    }
    
    if (phase2 < 0.3f && (nowSeconds - lastExchangeRippleTime2_) >= minBeatInterval) {
        lastExchangeRippleTime2_ = nowSeconds;
        // 左側の光から波紋を生成
        ripples_.push_back({
            nowSeconds,
            leftLightPos,
            0.0f
        });
    }
    
    // 既存の波紋を描画
    for (const auto& ripple : ripples_) {
        float age = static_cast<float>(nowSeconds - ripple.birthTime);
        if (age > maxAge) continue;
        
        float radius = age * speed;
        float rippleAlpha = (1.0f - age / maxAge) * alpha;
        
        // 波紋のフェードアウト
        float fadeStart = maxAge * 0.6f;
        if (age > fadeStart) {
            float fadeProgress = (age - fadeStart) / (maxAge - fadeStart);
            rippleAlpha *= (1.0f - fadeProgress);
        }
        
        ofColor warmWhite(251, 245, 231);
        warmWhite.a = static_cast<unsigned char>(rippleAlpha * 200.0f);
        ofSetColor(warmWhite);
        
        // 複数のリングで波紋を表現
        ofDrawCircle(ripple.position, radius);
        if (radius > 20.0f) {
            ofDrawCircle(ripple.position, radius * 0.7f);
        }
    }
    
    ofPopStyle();
}

void ofApp::drawStageText(double timeInState, float alpha) {
    if (!guideFont_.isLoaded()) return;
    
    std::string text;
    float textY;
    float textSize = 42.0f;
    float textAlpha = 0.0f;
    
    if (timeInState < 3.5) {
        // Stage 1: 意識の誘導
        text = "自分の心臓に、意識を向けてください";
        textY = ofGetHeight() * 0.3f;
        textAlpha = static_cast<float>(smoothstep(0.5, 1.0, timeInState)) * alpha;
    } else if (timeInState < 8.5) {
        // Stage 2: 同期の認識
        text = "光の点滅が、あなたの心拍に同期しています";
        textY = ofGetHeight() * 0.3f;
        float fadeIn = static_cast<float>(smoothstep(3.5, 4.0, timeInState));
        float fadeOut = 1.0f - static_cast<float>(smoothstep(8.0, 8.5, timeInState));
        textAlpha = fadeIn * fadeOut * alpha;
    } else if (timeInState < 11.0) {
        // Stage 3: 所有感の確立
        text = "この光は、あなたの心臓です";
        textY = ofGetHeight() * 0.25f;
        textSize = 48.0f;
        float fadeIn = static_cast<float>(smoothstep(8.5, 9.0, timeInState));
        float fadeOut = 1.0f - static_cast<float>(smoothstep(10.0, 11.0, timeInState));
        textAlpha = fadeIn * fadeOut * alpha;
    }
    
    if (textAlpha > 0.01f) {
        ofSetColor(220, 215, 210, static_cast<int>(textAlpha * 255.0f));
        float textWidth = guideFont_.stringWidth(text);
        guideFont_.drawString(text, (ofGetWidth() - textWidth) * 0.5f, textY);
    }
}
