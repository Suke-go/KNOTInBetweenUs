#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
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

void ensureDirectory(const std::filesystem::path& filePath) {
    const auto parent = filePath.parent_path();
    if (parent.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        ofLogWarning("ofApp") << "Failed to create directory " << parent << ": " << ec.message();
    }
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

    startButton_.addListener(this, &ofApp::onStartButtonPressed);
    endButton_.addListener(this, &ofApp::onEndButtonPressed);
    resetButton_.addListener(this, &ofApp::onResetButtonPressed);

    sampleRate_ = 48000.0;
    bufferSize_ = 512;
    audioPipeline_.setup(sampleRate_, bufferSize_);
    audioPipeline_.loadCalibrationFile(calibrationFilePath_);
    calibrationSaved_ = audioPipeline_.calibrationReady();
    calibrationSaveAttempted_ = calibrationSaved_;

    if (!calibrationSaved_) {
        ensureDirectory(calibrationFilePath_);
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
    } else if (audioPipeline_.calibrationReady() && !calibrationSaved_) {
        if (!calibrationSaveAttempted_) {
            ensureDirectory(calibrationFilePath_);
            if (audioPipeline_.saveCalibrationFile(calibrationFilePath_)) {
                calibrationSaved_ = true;
                ofLogNotice("ofApp") << "Calibration saved to " << calibrationFilePath_;
            } else {
                ofLogWarning("ofApp") << "Failed to save calibration to " << calibrationFilePath_;
            }
            calibrationSaveAttempted_ = true;
        }
    }

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
    drawCalibrationStatus();
    drawBeatDebug();
}

void ofApp::exit() {
    startButton_.removeListener(this, &ofApp::onStartButtonPressed);
    endButton_.removeListener(this, &ofApp::onEndButtonPressed);
    resetButton_.removeListener(this, &ofApp::onResetButtonPressed);

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
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::FirstPhase, nowSeconds)) {
        ofLogNotice("ofApp") << "Start request ignored.";
    }
}

void ofApp::onEndButtonPressed() {
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::End, nowSeconds)) {
        ofLogNotice("ofApp") << "End request ignored.";
    }
}

void ofApp::onResetButtonPressed() {
    const double nowSeconds = ofGetElapsedTimef();
    if (!sceneController_.requestState(SceneState::Idle, nowSeconds)) {
        ofLogNotice("ofApp") << "Reset request ignored.";
    }
}

void ofApp::updateSceneGui(double nowSeconds) {
    const SceneState current = sceneController_.currentState();
    const SceneState target = sceneController_.targetState();
    const bool transitioning = sceneController_.isTransitioning();

    if (transitioning) {
        std::ostringstream oss;
        oss << sceneToString(current) << " → " << sceneToString(target);
        oss << " (" << static_cast<int>(sceneController_.transitionBlend() * 100.0f) << "%)";
        sceneParam_.set(oss.str());
    } else {
        sceneParam_.set(sceneToString(current));
    }

    bpmParam_.set(latestMetrics_.bpm);
    envelopeParam_.set(latestMetrics_.envelope);
    hapticCountParam_.set(static_cast<std::uint32_t>(hapticLog_.entries().size()));

    const double horizon = std::clamp(sceneController_.timeInState(nowSeconds) * 1.2, 10.0, 45.0);
    envelopeHistory_.setHorizon(horizon);
    envelopeHistory_.prune(nowSeconds);
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

void ofApp::drawHapticLog(const ofRectangle& area, double nowSeconds) const {
    ofPushStyle();
    ofNoFill();
    ofSetColor(255, 255, 255, 140);
    ofDrawRectangle(area);

    const auto& entries = hapticLog_.entries();
    if (entries.empty()) {
        ofSetColor(220, 220, 220);
        ofDrawBitmapString("ハプティクスイベントなし", area.x + 6.0f, area.y + 20.0f);
        ofPopStyle();
        return;
    }

    float y = area.y + 20.0f;
    ofSetColor(255, 230, 150);
    ofDrawBitmapString("ハプティクスイベント (最新順)", area.x + 6.0f, y);
    y += 16.0f;

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

    if (audioPipeline_.isCalibrationActive()) {
        lines.push_back({"キャリブレーション: 実行中…", ofColor(255, 210, 120)});
    } else if (!calibrationSaved_) {
        lines.push_back({"キャリブレーション: 未保存 (" + calibrationFilePath_.filename().string() + ")",
                         ofColor(255, 180, 120)});
    } else {
        lines.push_back({"キャリブレーション: OK (" + calibrationFilePath_.filename().string() + ")",
                         ofColor(160, 240, 160)});
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
        << " / Envelope: " << std::setprecision(2) << latestMetrics_.envelope;
    ofSetColor(210, 210, 220);
    ofDrawBitmapString(oss.str(), margin, ofGetHeight() - margin);
    ofPopStyle();
}
