#pragma once

#include "ofMain.h"
#include "ofxGui.h"

#include "BeatVisualizer.h"
#include "BloomRenderer.h"
#include "HapticLog.h"
#include "SceneController.h"
#include "SceneTimingConfig.h"
#include "audio/AudioPipeline.h"
#include "audio/AudioRouter.h"
#include "infra/SceneTransitionLogger.h"
#include "infra/TelemetryLogging.h"

#include <array>
#include <filesystem>
#include <glm/vec2.hpp>
#include <optional>
#include <memory>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void exit() override;

    void keyPressed(int key) override;
    void keyReleased(int key) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
    void mouseEntered(int x, int y) override;
    void mouseExited(int x, int y) override;
    void windowResized(int w, int h) override;
    void dragEvent(ofDragInfo dragInfo) override;
    void gotMessage(ofMessage msg) override;
    void audioIn(ofSoundBuffer& input) override;
    void audioOut(ofSoundBuffer& output) override;

private:
    // GUI callbacks
    void onStartButtonPressed();
    void onEndButtonPressed();
    void onResetButtonPressed();
    void onEnvelopeCalibrationButtonPressed();

    // Update helpers
    void updateSceneGui(double nowSeconds);
    void updateEnvelopeHistories(double nowSeconds);
    void updateFakeSignal(double nowSeconds);
    void applyBeatMetrics(knot::audio::ParticipantId participant,
                          const knot::audio::AudioPipeline::ChannelMetrics& metrics, double nowSeconds);
    void handleBeatEvents(knot::audio::ParticipantId participant,
                          const std::vector<knot::audio::BeatEvent>& events, double nowSeconds);
    void appendHapticEvent(double nowSeconds, float intensity, const std::string& label);
    void updateEnvelopeCalibrationUi(double nowSeconds);
    void initializeSessionSeed();
    static bool ensureParentDirectory(const std::filesystem::path& path);

    struct Ripple {
        double birthTime;
        glm::vec2 position;
        float initialRadius;
    };

    // Synthetic heartbeat generation
    struct SyntheticHeartbeatGenerator {
        double lastBeatSample_ = 0.0;  // Last beat sample position
        double totalSamples_ = 0.0;  // Total samples processed (for timing)
    };

    // Drawing helpers
    void drawScene(SceneState state, float alpha, double nowSeconds);
    void drawIdleScene(float alpha, double nowSeconds);
    void drawStartScene(float alpha, double nowSeconds);
    void drawFirstPhaseScene(float alpha, double nowSeconds);
    void drawExchangeScene(float alpha, double nowSeconds);
    void drawMixedScene(float alpha, double nowSeconds);
    void drawEndScene(float alpha, double nowSeconds);
    void drawAmoebaOrganism(float alpha, double nowSeconds);
    void drawEnvelopeGraph(const ofRectangle& area) const;
    void drawHapticLog(const ofRectangle& area, double nowSeconds) const;
    void drawHapticChart(const ofRectangle& area, double nowSeconds) const;
    void drawCalibrationStatus() const;
    void drawBeatDebug() const;
    void appendCalibrationReport(const std::array<knot::audio::ChannelCalibrationValue, 2>& values,
                                 const std::optional<knot::audio::EnvelopeCalibrationStats>& envelopeStats);
    std::string makeCalibrationStatusText() const;
    bool isInteractionLocked() const;
    std::string buildGuidanceMessage(double nowSeconds) const;
    float computeHapticRatePerMinute(double nowSeconds) const;
    void logEnvelopeCalibrationResult(const knot::audio::EnvelopeCalibrationStats& stats);
    void processSceneTransitionEvents();
    void handleTransitionEvent(const SceneController::TransitionEvent& event);
    bool shouldDrawControlPanel() const;
    bool shouldDrawStatusPanel() const;
    void updateCornerUnlock(double nowSeconds, int x, int y);
    void loadShaders();
    void drawStarfieldLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2, bool idleMode = false);
    void drawRippleLayer(float alpha, double nowSeconds, float envelopeP1, float envelopeP2);
    float blendedEnvelope() const;
    void refreshAudioDeviceList();
    void updateAudioDeviceLabels();
    void selectNextInputDevice(int delta);
    void selectNextOutputDevice(int delta);
    void onRefreshAudioDevices();
    void onPrevInputDevice();
    void onNextInputDevice();
    void onPrevOutputDevice();
    void onNextOutputDevice();
    void onApplyAudioDevices();
    void shutdownSoundStream();
    bool setupSoundStreamWithSelection();
    void applyNoiseControlParamsIfChanged();
    knot::audio::AudioPipeline::NoiseMode parseNoiseMode(const std::string& mode) const;

    // UI + state
    SceneController sceneController_;
    HapticLog hapticLog_{128};
    BeatEnvelopeHistory envelopeHistory_;
    BeatVisualMetrics latestMetrics_;
    std::array<BeatVisualMetrics, 2> participantMetrics_{};
    std::array<float, 2> participantEnvelopes_{0.0f, 0.0f};
    std::array<float, 2> participantBpms_{0.0f, 0.0f};
    std::array<BeatEnvelopeHistory, 2> participantEnvelopeHistory_;
    knot::audio::AudioPipeline::SignalHealth signalHealth_{};
    bool lastFallbackActive_ = false;
    float displayEnvelope_ = 0.0f;
    std::shared_ptr<SceneTimingConfig> sceneTimingConfig_;
    infra::SceneTransitionLogger sceneTransitionLogger_;

    ofxPanel controlPanel_;
    ofParameter<std::string> sceneParam_;
    ofParameter<float> bpmParam_;
    ofParameter<float> envelopeParam_;
    ofParameter<float> bpmP1Param_;
   ofParameter<float> bpmP2Param_;
   ofParameter<float> envelopeP1Param_;
   ofParameter<float> envelopeP2Param_;
    ofParameter<std::uint32_t> hapticCountParam_;
    ofParameter<bool> simulateSignalParam_;
    ofParameter<int> noiseModeParam_;
    ofParameter<float> noiseGateThresholdParam_;
    ofParameter<float> noiseGateAttenuationParam_;
    ofParameter<bool> noiseSpecSubEnabledParam_;
    ofParameter<float> noiseSpecSubAlphaParam_;
    ofParameter<float> noiseSpecSubFloorParam_;
    ofParameter<float> noiseSpecSubSmoothingParam_;
    ofxButton startButton_;
    ofxButton endButton_;
    ofxButton resetButton_;
    ofxButton envelopeCalibrationButton_;
    ofxButton refreshDevicesButton_;
    ofxButton prevInputDeviceButton_;
    ofxButton nextInputDeviceButton_;
    ofxButton prevOutputDeviceButton_;
    ofxButton nextOutputDeviceButton_;
    ofxButton applyAudioDevicesButton_;
    ofParameter<std::string> inputDeviceLabel_;
    ofParameter<std::string> outputDeviceLabel_;
    ofxPanel statusPanel_;
    ofParameter<std::string> sceneOverviewParam_;
    ofParameter<float> transitionProgressParam_;
    ofParameter<std::string> timeInStateParam_;
    ofParameter<float> envelopeMonitorParam_;
    ofParameter<float> hapticRateParam_;
    ofParameter<std::string> calibrationStateParam_;
    ofParameter<float> limiterReductionParam_;
    ofParameter<std::string> guidanceParam_;
    ofParameter<float> baselineEnvelopeParam_;
    ofParameter<float> envelopeCalibrationProgressParam_;

    ofTrueTypeFont displayFont_;
    ofTrueTypeFont guideFont_;

    double lastEnvelopeSampledAt_ = 0.0;
    std::array<double, 2> lastSimulatedBeatAt_{0.0, 0.0};

    infra::AppConfig appConfig_;
    std::unique_ptr<infra::SessionLogger> sessionLogger_;
    std::unique_ptr<infra::HapticEventLogger> hapticLogger_;
    std::uint64_t lastTelemetryMicros_ = 0;
    std::uint64_t sessionStartMicros_ = 0;
    std::uint64_t beatCounter_ = 0;
    std::uint64_t sessionSeed_ = 0;
    bool simulateTelemetry_ = false;

    ofSoundStream soundStream_;
    bool soundStreamActive_ = false;
    knot::audio::AudioPipeline audioPipeline_;
    std::filesystem::path calibrationFilePath_;
    std::filesystem::path calibrationReportPath_;
    std::filesystem::path sessionSeedPath_;
    double sampleRate_ = 48000.0;
    std::size_t bufferSize_ = 512;
    bool calibrationSaved_ = false;
    bool calibrationSaveAttempted_ = false;
    bool calibrationReportAppended_ = false;
    bool envelopeCalibrationRunning_ = false;
    std::optional<knot::audio::EnvelopeCalibrationStats> lastEnvelopeCalibrationStats_;
    float limiterReductionDbSmooth_ = 0.0f;
    double lastStrongSignalAt_ = 0.0;
    bool weakSignalWarning_ = false;
    std::string operationMode_;
    bool showControlPanel_ = true;
    bool showStatusPanel_ = true;
    bool guiOverrideVisible_ = false;
    bool allowKeyboardToggle_ = true;
    double guiToggleHoldTimeSec_ = 0.0;
    int guiToggleKey_ = 'g';
    double guiKeyPressedAtSec_ = 0.0;
    bool allowCornerUnlock_ = false;
    std::vector<std::pair<double, glm::vec2>> cornerTouches_;
    double cornerUnlockWindowSec_ = 5.0;
    ofShader starfieldShader_;
    ofShader torusShader_;
    ofShader rippleShader_;
    ofShader realisticLightShader_;
    ofShader amoebaOrganismShader_;
    bool starfieldShaderLoaded_ = false;
    bool torusShaderLoaded_ = false;
    bool rippleShaderLoaded_ = false;
    bool realisticLightShaderLoaded_ = false;
    bool amoebaOrganismShaderLoaded_ = false;
    ofSoundPlayer bellSound_;
    bool bellSoundLoaded_ = false;
    float audioFadeGain_ = 1.0f;
    float targetAudioFadeGain_ = 1.0f;
    double audioFadeStartTime_ = 0.0;
    double audioFadeDuration_ = 10.0;
    bool audioFading_ = false;
    knot::audio::AudioRouter audioRouter_;
    ofSoundBuffer stereoScratch_;
    std::array<float, 2> envelopeFrame_{0.0f, 0.0f};
    std::array<float, 2> headphoneFrame_{0.0f, 0.0f};
    std::array<float, 6> routedFrame_{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<ofSoundDevice> inputDevices_;
    std::vector<ofSoundDevice> outputDevices_;
    int selectedInputDevice_ = -1;
    int selectedOutputDevice_ = -1;
    int configuredOutputChannels_ = 6;
    int lastNoiseMode_ = 0;
    float lastNoiseGateThreshold_ = 0.2f;
    float lastNoiseGateAttenuation_ = 0.0f;
    bool lastSpecSubEnabled_ = true;
    float lastSpecSubAlpha_ = 1.5f;
    float lastSpecSubFloor_ = 0.01f;
    float lastSpecSubSmoothing_ = 0.6f;
    bool specSubAutoDisabled_ = false;

    // Bloom renderer
    BloomRenderer bloomRenderer_;

    // Heartbeat tracking
    std::array<float, 2> participantHeartbeatPhase_{0.0f, 0.0f};

    // Ripples
    std::vector<Ripple> ripples_;
    double lastExchangeRippleTime1_ = 0.0;
    double lastExchangeRippleTime2_ = 0.0;

    // Synthetic heartbeat generation
    std::array<SyntheticHeartbeatGenerator, 2> syntheticHeartbeatGenerators_;
    ofSoundBuffer syntheticHeartbeatBuffer_;

    // Drawing helpers for heartbeat visuals
    void drawHeartbeatLight(const glm::vec2& position, float phase, float alpha, float sizeScale = 1.0f);
    void drawHeartbeatLightRealistic(const glm::vec2& position, float phase, float alpha, float sizeScale, double nowSeconds);
    void drawSubtleNoise(float alpha, double nowSeconds);
    void drawHeartbeatRipples(float alpha, double nowSeconds);
    void drawEnhancedHeartbeatRipples(float alpha, double nowSeconds, float riseUpFactor, 
                                   const glm::vec2& leftLightPos = glm::vec2(0.0f), 
                                   const glm::vec2& rightLightPos = glm::vec2(0.0f));
    void drawExchangeRipplesFromLights(float alpha, double nowSeconds, 
                                       const glm::vec2& leftLightPos, const glm::vec2& rightLightPos,
                                       float phase1, float phase2);
    void drawStageText(double timeInState, float alpha);

    // Synthetic heartbeat generation
    float generateHeartbeatSample(double timeSinceBeatStart, double sampleRate);
    void generateSyntheticHeartbeatBuffer(ofSoundBuffer& buffer, float bpmP1, float bpmP2);

    // Debug flag
    bool debugMode_ = false;
};
