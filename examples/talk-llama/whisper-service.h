#pragma once

#include "whisper.h"
#include "database.h"
#include "audio-stream-processor.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

// Whisper model information
struct WhisperModel {
    std::string name;
    std::string path;
    std::string language;
    bool is_loaded;
    size_t file_size;
    std::string upload_date;
    
    WhisperModel(const std::string& n, const std::string& p, const std::string& lang = "auto") 
        : name(n), path(p), language(lang), is_loaded(false), file_size(0) {}
};

// Per-session Whisper state for context isolation
struct WhisperSession {
    std::string session_id;
    struct whisper_state* state;
    std::vector<float> audio_buffer;
    std::string conversation_context;
    std::chrono::steady_clock::time_point last_activity;
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

// Main Whisper Service - handles multiple sessions with one loaded model
class WhisperService {
public:
    WhisperService();
    ~WhisperService();
    
    // Service lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Model management
    bool load_model(const std::string& model_path, const std::string& model_name = "");
    void unload_model();
    bool is_model_loaded() const { return whisper_ctx_ != nullptr; }
    std::string get_loaded_model_name() const { return current_model_name_; }
    
    // Model discovery and management
    std::vector<WhisperModel> get_available_models();
    bool upload_model(const std::string& file_path, const std::string& model_name);
    bool delete_model(const std::string& model_name);
    
    // Session management (uses existing database sessions)
    bool init_session(const std::string& session_id);
    void end_session(const std::string& session_id);
    bool has_session(const std::string& session_id);
    
    // Audio processing
    void add_audio(const std::string& session_id, const std::vector<float>& audio_samples);
    std::string transcribe_session_audio(const std::string& session_id);
    
    // Configuration
    void set_language(const std::string& language) { default_language_ = language; }
    void set_database(Database* db) { database_ = db; }
    
    // Status and statistics
    struct ServiceStatus {
        bool is_running;
        bool model_loaded;
        std::string model_name;
        int active_sessions;
        std::string model_language;
        size_t total_processed_audio;
    };
    ServiceStatus get_status();
    
private:
    // Whisper context (shared across sessions)
    struct whisper_context* whisper_ctx_;
    std::string current_model_name_;
    std::string current_model_path_;
    std::mutex whisper_mutex_;
    
    // Session management
    std::unordered_map<std::string, std::unique_ptr<WhisperSession>> sessions_;
    std::mutex sessions_mutex_;
    
    // Audio processing
    std::unique_ptr<AudioStreamProcessor> audio_processor_;
    
    // Service state
    std::atomic<bool> running_;
    std::thread cleanup_thread_;
    
    // Configuration
    std::string default_language_;
    std::string models_directory_;
    Database* database_;
    
    // Statistics
    std::atomic<size_t> total_processed_audio_;
    
    // Internal methods
    void cleanup_loop();
    void cleanup_inactive_sessions();
    std::string generate_session_id();
    bool validate_model_file(const std::string& path);
    void scan_models_directory();
    void on_transcription_ready(const std::string& session_id, const std::string& text);
    
    // Model file management
    std::vector<WhisperModel> discovered_models_;
    std::mutex models_mutex_;
    void refresh_model_list();
};

// Whisper STT implementation for the service
class WhisperServiceSTT : public STTInterface {
public:
    WhisperServiceSTT(WhisperService* service);
    
    std::string transcribe(const std::vector<float>& audio_samples) override;
    bool is_ready() override;
    
    void set_current_session(const std::string& session_id) { current_session_id_ = session_id; }
    
private:
    WhisperService* whisper_service_;
    std::string current_session_id_;
};
