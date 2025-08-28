#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

// Abstract STT Engine Interface - can be Whisper, Google STT, Azure STT, etc.
class STTEngine {
public:
    virtual ~STTEngine() = default;
    
    // Engine lifecycle
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;
    
    // Session management
    virtual bool create_session(const std::string& session_id) = 0;
    virtual void end_session(const std::string& session_id) = 0;
    virtual bool has_session(const std::string& session_id) const = 0;
    
    // Audio processing
    virtual void add_audio(const std::string& session_id, const std::vector<float>& audio_samples) = 0;
    
    // Configuration
    virtual void set_language(const std::string& language) = 0;
    virtual std::string get_engine_name() const = 0;
    virtual std::string get_engine_version() const = 0;
    
    // Status
    struct EngineStatus {
        bool is_running;
        std::string engine_name;
        std::string engine_version;
        int active_sessions;
        std::string current_language;
        bool model_loaded;
        std::string model_name;
    };
    virtual EngineStatus get_status() const = 0;
};

// STT Engine Factory - creates different STT engines
class STTEngineFactory {
public:
    enum class EngineType {
        WHISPER_LOCAL,
        WHISPER_SERVER,
        GOOGLE_CLOUD,
        AZURE_COGNITIVE,
        AWS_TRANSCRIBE
    };
    
    static std::unique_ptr<STTEngine> create_engine(EngineType type, const std::string& config_path = "");
    static std::vector<std::string> get_available_engines();
};

// Whisper-specific configuration
struct WhisperConfig {
    std::string model_path;
    std::string language = "auto";
    bool use_gpu = true;
    int n_threads = 4;
    bool translate = false;
    
    // Server mode (if running as separate service)
    std::string server_host = "localhost";
    int server_port = 8082;
    bool server_mode = false;
};

// Google Cloud STT configuration
struct GoogleSTTConfig {
    std::string credentials_path;
    std::string language_code = "en-US";
    int sample_rate = 16000;
    bool enable_automatic_punctuation = true;
};

// Azure Cognitive Services STT configuration
struct AzureSTTConfig {
    std::string subscription_key;
    std::string region;
    std::string language = "en-US";
    int sample_rate = 16000;
};

// Configuration loader
class STTConfigLoader {
public:
    static WhisperConfig load_whisper_config(const std::string& config_file);
    static GoogleSTTConfig load_google_config(const std::string& config_file);
    static AzureSTTConfig load_azure_config(const std::string& config_file);
    
    static bool save_whisper_config(const WhisperConfig& config, const std::string& config_file);
    static bool save_google_config(const GoogleSTTConfig& config, const std::string& config_file);
    static bool save_azure_config(const AzureSTTConfig& config, const std::string& config_file);
};
