#include "tts-engine.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <regex>
#include <unordered_map>

#ifdef __APPLE__
#include <AVFoundation/AVFoundation.h>
#include <Foundation/Foundation.h>
#endif

// PiperTtsEngine implementation
PiperTtsEngine::PiperTtsEngine() {
}

PiperTtsEngine::~PiperTtsEngine() {
    cleanup();
}

bool PiperTtsEngine::init(const std::string& model_path) {
    model_path_ = model_path;
    
    if (model_path_.empty()) {
        // Try to find default Piper model
        model_path_ = "models/piper/en_US-lessac-medium.onnx";
    }
    
    printf("Initializing Piper TTS with model: %s\n", model_path_.c_str());
    
    // TODO: Initialize actual Piper TTS
    // For now, simulate successful initialization
    is_initialized_ = true;
    
    printf("Piper TTS initialized successfully\n");
    return true;
}

std::vector<float> PiperTtsEngine::synthesize(const std::string& text) {
    if (!is_initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(synthesis_mutex_);
    
    printf("Piper TTS: Synthesizing '%s'\n", text.c_str());
    
    // TODO: Implement actual Piper synthesis
    // For now, generate placeholder audio
    return run_piper_synthesis(text);
}

void PiperTtsEngine::cleanup() {
    if (is_initialized_) {
        // TODO: Cleanup Piper resources
        is_initialized_ = false;
        printf("Piper TTS cleaned up\n");
    }
}

bool PiperTtsEngine::load_piper_model(const std::string& model_path) {
    // TODO: Load actual Piper model
    // This would use the Piper C++ API
    return true;
}

std::vector<float> PiperTtsEngine::run_piper_synthesis(const std::string& text) {
    // TODO: Run actual Piper synthesis
    // For now, generate silence proportional to text length
    size_t duration_samples = text.length() * 1000; // ~1000 samples per character
    std::vector<float> audio(duration_samples, 0.0f);
    
    // Add some simple tone to indicate TTS is working
    for (size_t i = 0; i < audio.size(); ++i) {
        float t = static_cast<float>(i) / sample_rate_;
        audio[i] = 0.1f * std::sin(2.0f * M_PI * 440.0f * t) * std::exp(-t * 2.0f);
    }
    
    return audio;
}

// MacOsTtsEngine implementation
MacOsTtsEngine::MacOsTtsEngine() {
}

MacOsTtsEngine::~MacOsTtsEngine() {
    cleanup();
}

bool MacOsTtsEngine::init(const std::string& voice_name) {
    voice_name_ = voice_name.empty() ? "Alex" : voice_name;
    
    printf("Initializing macOS TTS with voice: %s\n", voice_name_.c_str());
    
#ifdef __APPLE__
    // Check if macOS TTS is available
    is_initialized_ = true;
    printf("macOS TTS initialized successfully\n");
    return true;
#else
    printf("macOS TTS not available on this platform\n");
    return false;
#endif
}

std::vector<float> MacOsTtsEngine::synthesize(const std::string& text) {
    if (!is_initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(synthesis_mutex_);
    
    printf("macOS TTS: Synthesizing '%s'\n", text.c_str());
    
    return run_macos_synthesis(text);
}

void MacOsTtsEngine::cleanup() {
    if (is_initialized_) {
        is_initialized_ = false;
        printf("macOS TTS cleaned up\n");
    }
}

std::vector<float> MacOsTtsEngine::run_macos_synthesis(const std::string& text) {
#ifdef __APPLE__
    // TODO: Implement actual macOS TTS using AVSpeechSynthesizer
    // For now, generate placeholder audio
    size_t duration_samples = text.length() * 800; // ~800 samples per character
    std::vector<float> audio(duration_samples, 0.0f);
    
    // Add some simple tone pattern
    for (size_t i = 0; i < audio.size(); ++i) {
        float t = static_cast<float>(i) / 22050.0f;
        audio[i] = 0.1f * std::sin(2.0f * M_PI * 300.0f * t) * std::exp(-t * 1.5f);
    }
    
    return audio;
#else
    return {};
#endif
}

// SimpleTtsEngine implementation
SimpleTtsEngine::SimpleTtsEngine() {
}

bool SimpleTtsEngine::init(const std::string& unused) {
    printf("Simple TTS initialized (placeholder mode)\n");
    return true;
}

std::vector<float> SimpleTtsEngine::synthesize(const std::string& text) {
    printf("Simple TTS: '%s'\n", text.c_str());
    return generate_placeholder_audio(text);
}

std::vector<float> SimpleTtsEngine::generate_placeholder_audio(const std::string& text) {
    // Generate beeps for each word
    std::vector<float> audio;
    std::istringstream words(text);
    std::string word;
    
    while (words >> word) {
        // Generate a short beep for each word
        int beep_samples = 8000; // 0.5 seconds at 16kHz
        for (int i = 0; i < beep_samples; ++i) {
            float t = static_cast<float>(i) / 16000.0f;
            float beep = 0.2f * std::sin(2.0f * M_PI * 800.0f * t) * std::exp(-t * 5.0f);
            audio.push_back(beep);
        }
        
        // Add silence between words
        for (int i = 0; i < 4000; ++i) {
            audio.push_back(0.0f);
        }
    }
    
    return audio;
}

// TtsEngineFactory implementation
std::unique_ptr<TtsEngine> TtsEngineFactory::create_engine(EngineType type) {
    switch (type) {
        case EngineType::PIPER:
            return std::make_unique<PiperTtsEngine>();
        case EngineType::MACOS:
            return std::make_unique<MacOsTtsEngine>();
        case EngineType::SIMPLE:
            return std::make_unique<SimpleTtsEngine>();
        default:
            return std::make_unique<SimpleTtsEngine>();
    }
}

TtsEngineFactory::EngineType TtsEngineFactory::detect_best_engine() {
    // Check for Piper availability first
    if (is_piper_available()) {
        return EngineType::PIPER;
    }
    
    // Check for macOS TTS
    if (is_macos_tts_available()) {
        return EngineType::MACOS;
    }
    
    // Fallback to simple engine
    return EngineType::SIMPLE;
}

std::vector<std::string> TtsEngineFactory::get_available_engines() {
    std::vector<std::string> engines;
    
    if (is_piper_available()) {
        engines.push_back("Piper (Neural TTS)");
    }
    
    if (is_macos_tts_available()) {
        engines.push_back("macOS System TTS");
    }
    
    engines.push_back("Simple (Placeholder)");
    
    return engines;
}

// TtsManager implementation
TtsManager::TtsManager() {
}

TtsManager::~TtsManager() {
    clear_cache();
}

bool TtsManager::init(TtsEngineFactory::EngineType engine_type, const std::string& model_path) {
    engine_ = TtsEngineFactory::create_engine(engine_type);
    
    if (!engine_) {
        printf("Failed to create TTS engine\n");
        return false;
    }
    
    if (!engine_->init(model_path)) {
        printf("Failed to initialize TTS engine\n");
        engine_.reset();
        return false;
    }
    
    printf("TTS Manager initialized successfully\n");
    return true;
}

std::vector<float> TtsManager::text_to_speech(const std::string& text) {
    if (!engine_ || !engine_->is_ready()) {
        return {};
    }
    
    // Clean text for TTS
    std::string clean_text = clean_text_for_tts(text);
    if (clean_text.empty()) {
        return {};
    }
    
    // Check cache first
    if (cache_enabled_) {
        std::string cache_key = generate_cache_key(clean_text);
        std::vector<float> cached_audio;
        if (get_cached_audio(cache_key, cached_audio)) {
            return apply_audio_effects(cached_audio);
        }
    }
    
    // Generate audio
    std::vector<float> audio = engine_->synthesize(clean_text);
    
    if (audio.empty()) {
        return {};
    }
    
    // Cache the result
    if (cache_enabled_) {
        std::string cache_key = generate_cache_key(clean_text);
        cache_audio(cache_key, audio);
    }
    
    // Apply effects and return
    return apply_audio_effects(audio);
}

bool TtsManager::is_ready() const {
    return engine_ && engine_->is_ready();
}

int TtsManager::get_sample_rate() const {
    return engine_ ? engine_->get_sample_rate() : 16000;
}

void TtsManager::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    audio_cache_.clear();
}

std::vector<float> TtsManager::apply_audio_effects(const std::vector<float>& audio) {
    std::vector<float> result = audio;
    
    // Apply volume
    if (volume_ != 1.0f) {
        for (float& sample : result) {
            sample *= volume_;
        }
    }
    
    // TODO: Apply speed and pitch effects
    // This would require more complex audio processing
    
    // Normalize audio
    result = normalize_audio(result);
    
    return result;
}

std::string TtsManager::generate_cache_key(const std::string& text) {
    // Simple hash-like key generation
    std::hash<std::string> hasher;
    return std::to_string(hasher(text + std::to_string(speed_) + std::to_string(pitch_)));
}

bool TtsManager::get_cached_audio(const std::string& key, std::vector<float>& audio) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = audio_cache_.find(key);
    if (it != audio_cache_.end()) {
        audio = it->second;
        return true;
    }
    return false;
}

void TtsManager::cache_audio(const std::string& key, const std::vector<float>& audio) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Limit cache size
    if (audio_cache_.size() >= 100) {
        audio_cache_.clear();
    }
    
    audio_cache_[key] = audio;
}

// Utility functions
std::string clean_text_for_tts(const std::string& text) {
    std::string result = text;
    
    // Remove or replace problematic characters
    std::regex special_chars(R"([<>{}[\]|\\])");
    result = std::regex_replace(result, special_chars, "");
    
    // Normalize whitespace
    std::regex whitespace(R"(\s+)");
    result = std::regex_replace(result, whitespace, " ");
    
    // Trim
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return result;
}

bool is_piper_available() {
    // TODO: Check if Piper TTS is available
    // For now, assume it's available if model files exist
    return false; // Disabled for now until Piper is properly integrated
}

bool is_macos_tts_available() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

std::vector<float> normalize_audio(const std::vector<float>& audio) {
    if (audio.empty()) {
        return audio;
    }
    
    // Find peak amplitude
    float peak = 0.0f;
    for (float sample : audio) {
        peak = std::max(peak, std::abs(sample));
    }
    
    if (peak == 0.0f) {
        return audio;
    }
    
    // Normalize to 0.8 to prevent clipping
    float scale = 0.8f / peak;
    std::vector<float> result;
    result.reserve(audio.size());
    
    for (float sample : audio) {
        result.push_back(sample * scale);
    }
    
    return result;
}
