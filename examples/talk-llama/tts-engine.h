#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

// TTS Engine interface for generating speech from text
class TtsEngine {
public:
    virtual ~TtsEngine() = default;
    
    // Initialize the TTS engine
    virtual bool init(const std::string& model_path = "") = 0;
    
    // Generate audio from text
    virtual std::vector<float> synthesize(const std::string& text) = 0;
    
    // Check if engine is ready
    virtual bool is_ready() const = 0;
    
    // Get supported sample rate
    virtual int get_sample_rate() const = 0;
    
    // Cleanup
    virtual void cleanup() = 0;
};

// Piper TTS implementation (fast, local neural TTS)
class PiperTtsEngine : public TtsEngine {
public:
    PiperTtsEngine();
    ~PiperTtsEngine() override;
    
    bool init(const std::string& model_path = "") override;
    std::vector<float> synthesize(const std::string& text) override;
    bool is_ready() const override { return is_initialized_; }
    int get_sample_rate() const override { return sample_rate_; }
    void cleanup() override;
    
private:
    bool is_initialized_ = false;
    int sample_rate_ = 22050; // Piper default sample rate
    std::string model_path_;
    std::mutex synthesis_mutex_;
    
    // Piper-specific implementation
    bool load_piper_model(const std::string& model_path);
    std::vector<float> run_piper_synthesis(const std::string& text);
};

// macOS native TTS implementation (using system TTS)
class MacOsTtsEngine : public TtsEngine {
public:
    MacOsTtsEngine();
    ~MacOsTtsEngine() override;
    
    bool init(const std::string& voice_name = "") override;
    std::vector<float> synthesize(const std::string& text) override;
    bool is_ready() const override { return is_initialized_; }
    int get_sample_rate() const override { return 22050; }
    void cleanup() override;
    
private:
    bool is_initialized_ = false;
    std::string voice_name_;
    std::mutex synthesis_mutex_;
    
    // macOS-specific implementation
    std::vector<float> run_macos_synthesis(const std::string& text);
};

// Simple TTS implementation (for testing/fallback)
class SimpleTtsEngine : public TtsEngine {
public:
    SimpleTtsEngine();
    ~SimpleTtsEngine() override = default;
    
    bool init(const std::string& unused = "") override;
    std::vector<float> synthesize(const std::string& text) override;
    bool is_ready() const override { return true; }
    int get_sample_rate() const override { return 16000; }
    void cleanup() override {}
    
private:
    // Generate simple beep tones or silence as placeholder
    std::vector<float> generate_placeholder_audio(const std::string& text);
};

// TTS Engine Factory
class TtsEngineFactory {
public:
    enum class EngineType {
        PIPER,      // Fast neural TTS (recommended)
        MACOS,      // macOS system TTS
        SIMPLE      // Simple placeholder (for testing)
    };
    
    static std::unique_ptr<TtsEngine> create_engine(EngineType type);
    static EngineType detect_best_engine();
    static std::vector<std::string> get_available_engines();
};

// TTS Manager - manages TTS engine with caching and optimization
class TtsManager {
public:
    TtsManager();
    ~TtsManager();
    
    // Initialize with specific engine type
    bool init(TtsEngineFactory::EngineType engine_type = TtsEngineFactory::EngineType::PIPER,
              const std::string& model_path = "");
    
    // Generate speech audio
    std::vector<float> text_to_speech(const std::string& text);
    
    // Configuration
    void set_speed(float speed) { speed_ = speed; }
    void set_pitch(float pitch) { pitch_ = pitch; }
    void set_volume(float volume) { volume_ = volume; }
    
    // Status
    bool is_ready() const;
    int get_sample_rate() const;
    
    // Cache management
    void clear_cache();
    void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
    
private:
    std::unique_ptr<TtsEngine> engine_;
    
    // Audio processing parameters
    float speed_ = 1.0f;
    float pitch_ = 1.0f;
    float volume_ = 1.0f;
    
    // Caching
    bool cache_enabled_ = true;
    std::unordered_map<std::string, std::vector<float>> audio_cache_;
    std::mutex cache_mutex_;
    
    // Audio post-processing
    std::vector<float> apply_audio_effects(const std::vector<float>& audio);
    std::vector<float> resample_audio(const std::vector<float>& audio, int from_rate, int to_rate);
    
    // Cache utilities
    std::string generate_cache_key(const std::string& text);
    bool get_cached_audio(const std::string& key, std::vector<float>& audio);
    void cache_audio(const std::string& key, const std::vector<float>& audio);
};

// Utility functions
std::string clean_text_for_tts(const std::string& text);
bool is_piper_available();
bool is_macos_tts_available();
std::vector<float> normalize_audio(const std::vector<float>& audio);
std::vector<float> convert_sample_rate(const std::vector<float>& audio, int from_rate, int to_rate);
