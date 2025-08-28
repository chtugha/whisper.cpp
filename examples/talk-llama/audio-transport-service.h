#pragma once

#include "stt-interface.h"
#include "database.h"
#include "audio-stream-processor.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

// Audio Transport Service - manages sessions and routes audio to STT engines
// This service is the bridge between SIP clients and STT engines
class AudioTransportService {
public:
    using TranscriptionCallback = std::function<void(const std::string& session_id, const std::string& text)>;
    
    AudioTransportService();
    ~AudioTransportService();
    
    // Service lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // STT Engine management
    bool set_stt_engine(std::unique_ptr<STTEngine> engine);
    STTEngine* get_stt_engine() const { return stt_engine_.get(); }
    bool switch_stt_engine(STTEngineFactory::EngineType type, const std::string& config_path = "");
    
    // Session management (uses database sessions)
    bool init_session(const std::string& session_id);
    void end_session(const std::string& session_id);
    bool has_session(const std::string& session_id) const;
    std::vector<std::string> get_active_sessions() const;
    
    // Audio processing
    void add_audio(const std::string& session_id, const std::vector<float>& audio_samples);
    
    // Configuration
    void set_database(Database* database) { database_ = database; }
    void set_transcription_callback(TranscriptionCallback callback) { transcription_callback_ = callback; }
    
    // Audio processing configuration
    void set_vad_threshold(float threshold);
    void set_chunk_duration_limits(int min_ms, int max_ms);
    void set_language(const std::string& language);
    
    // Status and monitoring
    struct ServiceStatus {
        bool is_running;
        std::string stt_engine_name;
        std::string stt_engine_version;
        int active_sessions;
        size_t total_audio_processed;
        std::string current_language;
        bool stt_engine_ready;
    };
    ServiceStatus get_status() const;
    
    // Statistics
    struct SessionStats {
        std::string session_id;
        size_t audio_samples_processed;
        int transcription_count;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_activity;
        bool is_active;
    };
    std::vector<SessionStats> get_session_stats() const;
    
private:
    // STT Engine
    std::unique_ptr<STTEngine> stt_engine_;
    std::mutex stt_mutex_;
    
    // Audio processing
    std::unique_ptr<AudioStreamProcessor> audio_processor_;
    
    // Session tracking
    struct SessionInfo {
        std::string session_id;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_activity;
        size_t audio_samples_processed;
        int transcription_count;
        bool is_active;
        
        SessionInfo(const std::string& id) 
            : session_id(id), start_time(std::chrono::steady_clock::now()),
              last_activity(std::chrono::steady_clock::now()),
              audio_samples_processed(0), transcription_count(0), is_active(true) {}
    };
    
    std::unordered_map<std::string, SessionInfo> sessions_;
    mutable std::mutex sessions_mutex_;
    
    // Service state
    std::atomic<bool> running_;
    std::thread cleanup_thread_;
    
    // Configuration
    Database* database_;
    TranscriptionCallback transcription_callback_;
    std::string current_language_;
    
    // Statistics
    std::atomic<size_t> total_audio_processed_;
    
    // Internal methods
    void cleanup_loop();
    void cleanup_inactive_sessions();
    void on_transcription_ready(const std::string& session_id, const std::string& text);
    
    // STT Engine adapter
    class STTEngineAdapter : public STTInterface {
    public:
        STTEngineAdapter(AudioTransportService* service);
        std::string transcribe(const std::vector<float>& audio_samples) override;
        bool is_ready() override;
        void set_current_session(const std::string& session_id) { current_session_id_ = session_id; }
        
    private:
        AudioTransportService* service_;
        std::string current_session_id_;
    };
    
    std::unique_ptr<STTEngineAdapter> stt_adapter_;
};

// Utility functions for audio format conversion
namespace AudioUtils {
    std::vector<float> convert_pcm16_to_float(const std::vector<uint8_t>& pcm16_data);
    std::vector<uint8_t> convert_float_to_pcm16(const std::vector<float>& float_data);
    std::vector<float> resample_audio(const std::vector<float>& input, int input_rate, int output_rate);
    float calculate_rms_energy(const std::vector<float>& audio_samples);
}
