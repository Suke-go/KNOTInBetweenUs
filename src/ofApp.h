#pragma once

#include "ofMain.h"
#include "ofxGui.h"

#include "BeatVisualizer.h"
#include "HapticLog.h"
#include "SceneController.h"
#include "audio/AudioPipeline.h"
#include "infra/TelemetryLogging.h"

#include <filesystem>
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

    // Update helpers
    void updateSceneGui(double nowSeconds);
    void updateEnvelopeHistory(double nowSeconds, float envelopeValue);
    void updateFakeSignal(double nowSeconds);
    void applyBeatMetrics(const knot::audio::AudioPipeline::BeatMetrics& metrics, double nowSeconds);
    void handleBeatEvents(const std::vector<knot::audio::BeatEvent>& events, double nowSeconds);
    void appendHapticEvent(double nowSeconds, float intensity, const std::string& label);

    // Drawing helpers
    void drawScene(SceneState state, float alpha, double nowSeconds);
    void drawIdleScene(float alpha, double nowSeconds);
    void drawFirstPhaseScene(float alpha, double nowSeconds);
    void drawEndScene(float alpha);
    void drawEnvelopeGraph(const ofRectangle& area) const;
    void drawHapticLog(const ofRectangle& area, double nowSeconds) const;
    void drawCalibrationStatus() const;
    void drawBeatDebug() const;

    // UI + state
    SceneController sceneController_;
    HapticLog hapticLog_{128};
    BeatEnvelopeHistory envelopeHistory_;
    BeatVisualMetrics latestMetrics_;

    ofxPanel controlPanel_;
    ofParameter<std::string> sceneParam_;
    ofParameter<float> bpmParam_;
    ofParameter<float> envelopeParam_;
    ofParameter<std::uint32_t> hapticCountParam_;
    ofParameter<bool> simulateSignalParam_;
    ofxButton startButton_;
    ofxButton endButton_;
    ofxButton resetButton_;

    double lastEnvelopeSampledAt_ = 0.0;
    double lastSimulatedBeatAt_ = 0.0;

    infra::AppConfig appConfig_;
    std::unique_ptr<infra::SessionLogger> sessionLogger_;
    std::unique_ptr<infra::HapticEventLogger> hapticLogger_;
    std::uint64_t lastTelemetryMicros_ = 0;
    std::uint64_t sessionStartMicros_ = 0;
    std::uint64_t beatCounter_ = 0;
    bool simulateTelemetry_ = false;

    ofSoundStream soundStream_;
    bool soundStreamActive_ = false;
    knot::audio::AudioPipeline audioPipeline_;
    std::filesystem::path calibrationFilePath_;
    double sampleRate_ = 48000.0;
    std::size_t bufferSize_ = 512;
    bool calibrationSaved_ = false;
    bool calibrationSaveAttempted_ = false;
    float limiterReductionDbSmooth_ = 0.0f;
};
