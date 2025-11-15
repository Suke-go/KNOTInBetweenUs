#include "BloomRenderer.h"

void BloomRenderer::setup(int width, int height, float bloomRadius) {
    // Validate dimensions
    if (width <= 0 || height <= 0) {
        ofLogWarning("BloomRenderer") << "Invalid dimensions: " << width << "x" << height << ". Skipping initialization.";
        initialized_ = false;
        shaderLoaded_ = false;
        return;
    }

    width_ = width;
    height_ = height;
    bloomRadius_ = bloomRadius;
    bloomIntensity_ = 1.5f;
    exposure_ = 1.0f;

    try {
        ofFboSettings settings;
        settings.width = width;
        settings.height = height;
        settings.internalformat = GL_RGBA16F;
        settings.textureTarget = GL_TEXTURE_2D;
        settings.minFilter = GL_LINEAR;
        settings.maxFilter = GL_LINEAR;
        settings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
        settings.wrapModeVertical = GL_CLAMP_TO_EDGE;

        lightSourceFbo_.allocate(settings);

        settings.width = width / 2;
        settings.height = height / 2;
        blurPassXFbo_.allocate(settings);
        blurPassYFbo_.allocate(settings);

        settings.width = width;
        settings.height = height;
        settings.internalformat = GL_RGBA;
        compositeFbo_.allocate(settings);

        // Load shaders with error checking
        shaderLoaded_ = false;
        bool blurLoaded = blurShader_.load("shaders/gaussian_blur");
        bool compositeLoaded = compositeShader_.load("shaders/light_composite");
        
        if (blurLoaded && compositeLoaded) {
            shaderLoaded_ = true;
            ofLogNotice("BloomRenderer") << "Initialized: " << width << "x" << height << " (shaders loaded)";
        } else {
            ofLogWarning("BloomRenderer") << "Initialized: " << width << "x" << height << " (shaders failed to load - bloom disabled)";
            if (!blurLoaded) {
                ofLogWarning("BloomRenderer") << "Failed to load gaussian_blur shader";
            }
            if (!compositeLoaded) {
                ofLogWarning("BloomRenderer") << "Failed to load light_composite shader";
            }
        }

        initialized_ = true;
    } catch (const std::exception& ex) {
        ofLogError("BloomRenderer") << "Exception during setup: " << ex.what();
        initialized_ = false;
        shaderLoaded_ = false;
    }
}

void BloomRenderer::begin() {
    if (!isInitialized()) {
        ofLogWarning("BloomRenderer") << "begin() called but not initialized";
        return;
    }
    lightSourceFbo_.begin();
    ofClear(0, 0, 0, 255);
}

void BloomRenderer::end() {
    if (!isInitialized()) {
        return;
    }
    lightSourceFbo_.end();
    if (shaderLoaded_) {
        applyGaussianBlur();
        composite();
    }
}

void BloomRenderer::applyGaussianBlur() {
    if (!shaderLoaded_ || !isInitialized()) {
        return;
    }
    blurPassXFbo_.begin();
    ofClear(0, 0, 0, 255);
    blurShader_.begin();
    blurShader_.setUniformTexture("tex", lightSourceFbo_.getTexture(), 0);
    blurShader_.setUniform2f("direction", 1.0f, 0.0f);
    blurShader_.setUniform1f("blurSize", bloomRadius_);

    blurShader_.setUniform2f("resolution",
                             static_cast<float>(lightSourceFbo_.getWidth()),
                             static_cast<float>(lightSourceFbo_.getHeight()));
    lightSourceFbo_.draw(0, 0, blurPassXFbo_.getWidth(), blurPassXFbo_.getHeight());
    blurShader_.end();
    blurPassXFbo_.end();

    blurPassYFbo_.begin();
    ofClear(0, 0, 0, 255);
    blurShader_.begin();
    blurShader_.setUniformTexture("tex", blurPassXFbo_.getTexture(), 0);
    blurShader_.setUniform2f("direction", 0.0f, 1.0f);
    blurShader_.setUniform1f("blurSize", bloomRadius_);
    blurShader_.setUniform2f("resolution",
                             static_cast<float>(blurPassXFbo_.getWidth()),
                             static_cast<float>(blurPassXFbo_.getHeight()));
    blurPassXFbo_.draw(0, 0, blurPassYFbo_.getWidth(), blurPassYFbo_.getHeight());
    blurShader_.end();
    blurPassYFbo_.end();
}

void BloomRenderer::composite() {
    if (!shaderLoaded_ || !isInitialized()) {
        return;
    }
    compositeFbo_.begin();
    ofClear(0, 0, 0, 255);
    compositeShader_.begin();
    compositeShader_.setUniformTexture("baseTex", lightSourceFbo_.getTexture(), 0);
    compositeShader_.setUniformTexture("bloomTex", blurPassYFbo_.getTexture(), 1);
    compositeShader_.setUniform1f("bloomIntensity", bloomIntensity_);
    compositeShader_.setUniform1f("exposure", exposure_);
    lightSourceFbo_.draw(0, 0, width_, height_);
    compositeShader_.end();
    compositeFbo_.end();
}

void BloomRenderer::draw(float x, float y) {
    if (!isInitialized()) {
        return;
    }
    if (shaderLoaded_) {
        compositeFbo_.draw(x, y);
    } else {
        // Fallback: draw light source directly without bloom
        lightSourceFbo_.draw(x, y);
    }
}
