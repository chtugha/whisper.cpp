#pragma once

#include "audio-processor-interface.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

// Fast, simple audio processor - minimal overhead
class SimpleAudioProcessor : public AudioProcessor {
public:
    SimpleAudioProcessor(SipAudioInterface* sip_interface);
    ~SimpleAudioProcessor();
    
    // AudioProcessor interface
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_.load(); }
    
    void start_session(const AudioSessionParams& params) override;
    void end_session(const std::string& session_id) override;
    void process_audio(const std::string& session_id, const RTPAudioPacket& packet) override;
    
    void set_whisper_endpoint(const std::string& endpoint) override { whisper_endpoint_ = endpoint; }
    std::string get_processor_name() const override { return "SimpleAudioProcessor"; }
    
    // Configuration
    void set_chunk_duration_ms(int ms) { chunk_duration_ms_ = ms; }
    void set_vad_threshold(float threshold) { vad_threshold_ = threshold; }
    void set_silence_timeout_ms(int ms) { silence_timeout_ms_ = ms; }
    
private:
    // Session state - minimal data
    struct SessionState {
        std::string session_id;
        std::vector<float> audio_buffer;
        std::chrono::steady_clock::time_point last_speech_time;
        std::chrono::steady_clock::time_point chunk_start_time;
        bool has_speech;
        int sample_rate;
        
        SessionState(const std::string& id) 
            : session_id(id), has_speech(false), sample_rate(16000),
              last_speech_time(std::chrono::steady_clock::now()),
              chunk_start_time(std::chrono::steady_clock::now()) {}
    };
    
    SipAudioInterface* sip_interface_;
    std::atomic<bool> running_;
    
    // Session management
    std::unordered_map<std::string, SessionState> sessions_;
    std::mutex sessions_mutex_;
    
    // Configuration
    std::string whisper_endpoint_;
    int chunk_duration_ms_;
    float vad_threshold_;
    int silence_timeout_ms_;
    
    // Fast audio processing methods
    std::vector<float> decode_rtp_audio(const RTPAudioPacket& packet);
    std::vector<float> convert_g711_ulaw(const std::vector<uint8_t>& data);
    std::vector<float> convert_g711_alaw(const std::vector<uint8_t>& data);
    std::vector<float> convert_pcm16(const std::vector<uint8_t>& data);
    
    // Fast VAD
    bool has_speech(const std::vector<float>& samples);
    float calculate_energy(const std::vector<float>& samples);
    
    // Chunk management
    bool should_send_chunk(const SessionState& session);
    void send_audio_chunk(const std::string& session_id, SessionState& session);
    std::vector<float> prepare_whisper_chunk(const std::vector<float>& audio);
    
    // Utility
    std::vector<float> resample_8k_to_16k(const std::vector<float>& input);
};

// Debug audio processor - with logging
class DebugAudioProcessor : public AudioProcessor {
public:
    DebugAudioProcessor(SipAudioInterface* sip_interface);
    
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_.load(); }
    
    void start_session(const AudioSessionParams& params) override;
    void end_session(const std::string& session_id) override;
    void process_audio(const std::string& session_id, const RTPAudioPacket& packet) override;
    
    void set_whisper_endpoint(const std::string& endpoint) override { whisper_endpoint_ = endpoint; }
    std::string get_processor_name() const override { return "DebugAudioProcessor"; }
    
    // Debug configuration
    void set_log_audio_stats(bool enable) { log_audio_stats_ = enable; }
    void set_save_audio_files(bool enable) { save_audio_files_ = enable; }
    
private:
    SipAudioInterface* sip_interface_;
    std::atomic<bool> running_;
    std::string whisper_endpoint_;
    
    // Debug options
    bool log_audio_stats_;
    bool save_audio_files_;
    
    // Statistics
    std::unordered_map<std::string, size_t> session_packet_counts_;
    std::unordered_map<std::string, size_t> session_audio_bytes_;
    std::mutex stats_mutex_;
    
    void log_packet_info(const std::string& session_id, const RTPAudioPacket& packet);
    void log_session_stats(const std::string& session_id);
    void save_audio_chunk(const std::string& session_id, const std::vector<float>& audio);
};

// Fast lookup tables for G.711 decoding
class G711Tables {
public:
    static const std::vector<float>& get_ulaw_table();
    static const std::vector<float>& get_alaw_table();
    
public:
    static void initialize_tables();

private:
    static std::vector<float> ulaw_table_;
    static std::vector<float> alaw_table_;
    static bool tables_initialized_;
};
