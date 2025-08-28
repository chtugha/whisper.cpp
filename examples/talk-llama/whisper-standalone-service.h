#pragma once

#include "stt-interface.h"
#include "whisper.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <queue>
#include <condition_variable>

// Standalone Whisper STT Engine - can run as separate process
class WhisperSTTEngine : public STTEngine {
public:
    WhisperSTTEngine(const WhisperConfig& config);
    ~WhisperSTTEngine();
    
    // STTEngine interface implementation
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_.load(); }
    
    bool create_session(const std::string& session_id) override;
    void end_session(const std::string& session_id) override;
    bool has_session(const std::string& session_id) const override;
    
    void add_audio(const std::string& session_id, const std::vector<float>& audio_samples) override;
    
    void set_language(const std::string& language) override { config_.language = language; }
    std::string get_engine_name() const override { return "Whisper Local"; }
    std::string get_engine_version() const override;
    
    EngineStatus get_status() const override;
    
    // Whisper-specific methods
    bool load_model(const std::string& model_path);
    void unload_model();
    bool is_model_loaded() const { return whisper_ctx_ != nullptr; }
    std::string get_loaded_model_path() const { return current_model_path_; }
    
    // Model management
    std::vector<std::string> get_available_models() const;
    bool validate_model(const std::string& model_path) const;
    
    // Configuration
    void set_transcription_callback(std::function<void(const std::string&, const std::string&)> callback);
    void configure_processing(int min_chunk_ms, int max_chunk_ms, float vad_threshold);
    
private:
    // Whisper context (shared across sessions)
    struct whisper_context* whisper_ctx_;
    std::string current_model_path_;
    std::mutex whisper_mutex_;
    
    // Per-session Whisper state
    struct WhisperSession {
        std::string session_id;
        struct whisper_state* state;
        std::vector<float> audio_buffer;
        std::chrono::steady_clock::time_point last_activity;
        std::mutex buffer_mutex;
        bool is_active;
        
        WhisperSession(const std::string& id) 
            : session_id(id), state(nullptr), is_active(true),
              last_activity(std::chrono::steady_clock::now()) {}
        
        ~WhisperSession() {
            if (state) {
                whisper_free_state(state);
            }
        }
    };
    
    std::unordered_map<std::string, std::unique_ptr<WhisperSession>> sessions_;
    std::mutex sessions_mutex_;
    
    // Audio processing queue
    struct AudioChunk {
        std::string session_id;
        std::vector<float> audio_data;
        std::chrono::steady_clock::time_point timestamp;
        
        AudioChunk(const std::string& id, const std::vector<float>& data)
            : session_id(id), audio_data(data), timestamp(std::chrono::steady_clock::now()) {}
    };
    
    std::queue<AudioChunk> audio_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Processing threads
    std::vector<std::thread> worker_threads_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_;
    
    // Configuration
    WhisperConfig config_;
    std::function<void(const std::string&, const std::string&)> transcription_callback_;
    
    // Processing parameters
    int min_chunk_duration_ms_;
    int max_chunk_duration_ms_;
    float vad_threshold_;
    
    // Statistics
    std::atomic<size_t> total_processed_chunks_;
    std::atomic<size_t> total_processed_samples_;
    
    // Internal methods
    void worker_loop();
    void cleanup_loop();
    void process_audio_chunk(const AudioChunk& chunk);
    std::string transcribe_audio(const std::string& session_id, const std::vector<float>& audio_samples);
    void cleanup_inactive_sessions();
    bool should_process_audio(const WhisperSession& session) const;
    std::vector<float> extract_processable_audio(WhisperSession& session);
    
    // VAD and audio processing
    bool has_speech(const std::vector<float>& audio_samples) const;
    bool is_silence_boundary(const std::vector<float>& audio_samples) const;
    float calculate_energy(const std::vector<float>& audio_samples) const;
};

// Whisper Server Mode - runs as HTTP service
class WhisperServerEngine : public STTEngine {
public:
    WhisperServerEngine(const WhisperConfig& config);
    ~WhisperServerEngine();
    
    // STTEngine interface implementation
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_.load(); }
    
    bool create_session(const std::string& session_id) override;
    void end_session(const std::string& session_id) override;
    bool has_session(const std::string& session_id) const override;
    
    void add_audio(const std::string& session_id, const std::vector<float>& audio_samples) override;
    
    void set_language(const std::string& language) override { config_.language = language; }
    std::string get_engine_name() const override { return "Whisper Server"; }
    std::string get_engine_version() const override { return "1.0.0"; }
    
    EngineStatus get_status() const override;
    
private:
    WhisperConfig config_;
    std::atomic<bool> running_;
    
    // HTTP client for communication with Whisper server
    bool send_http_request(const std::string& endpoint, const std::string& data, std::string& response);
    std::string encode_audio_base64(const std::vector<float>& audio_samples);
    std::vector<float> decode_audio_base64(const std::string& base64_data);
};

// Whisper Service Main - for standalone executable
class WhisperServiceMain {
public:
    WhisperServiceMain();
    ~WhisperServiceMain();
    
    int run(int argc, char** argv);
    
private:
    std::unique_ptr<WhisperSTTEngine> whisper_engine_;
    WhisperConfig config_;
    
    bool parse_arguments(int argc, char** argv);
    void print_usage(const char* program_name);
    bool load_config_file(const std::string& config_path);
    void setup_signal_handlers();
    void run_service_loop();
    
    static void signal_handler(int signal);
    static std::atomic<bool> should_exit_;
};
