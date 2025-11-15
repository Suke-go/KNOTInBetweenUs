#pragma once

#include "ofMain.h"

class BloomRenderer {
public:
    void setup(int width, int height, float bloomRadius = 8.0f);
    void begin();
    void end();
    void draw(float x = 0, float y = 0);

    void setBloomIntensity(float intensity) { bloomIntensity_ = intensity; }
    void setBloomRadius(float radius) { bloomRadius_ = radius; }
    void setExposure(float exposure) { exposure_ = exposure; }
    float getBloomIntensity() const { return bloomIntensity_; }
    bool isInitialized() const { return initialized_ && width_ > 0 && height_ > 0; }
    bool isShaderLoaded() const { return shaderLoaded_; }

private:
    ofFbo lightSourceFbo_;
    ofFbo blurPassXFbo_;
    ofFbo blurPassYFbo_;
    ofFbo compositeFbo_;

    ofShader blurShader_;
    ofShader compositeShader_;

    int width_ = 0;
    int height_ = 0;
    float bloomRadius_ = 8.0f;
    float bloomIntensity_ = 1.5f;
    float exposure_ = 1.0f;
    bool initialized_ = false;
    bool shaderLoaded_ = false;

    void applyGaussianBlur();
    void composite();
};
