#pragma once

#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>

// Simple audio chunk for processing
struct AudioChunk {
    std::vector<float> samples;
    std::string session_id;
    std::chrono::steady_clock::time_point timestamp;
    
    AudioChunk(const std::vector<float>& audio, const std::string& sid) 
        : samples(audio), session_id(sid), timestamp(std::chrono::steady_clock::now()) {}
};

// STT Interface - pluggable for different models
class STTInterface {
public:
    virtual ~STTInterface() = default;
    virtual std::string transcribe(const std::vector<float>& audio_samples) = 0;
    virtual bool is_ready() = 0;
};

// Simple VAD - detects speech/silence boundaries
class SimpleVAD {
public:
    SimpleVAD(float silence_threshold = 0.01f, int silence_duration_ms = 500);
    
    // Returns true if this audio chunk ends with silence (good cut point)
    bool is_silence_boundary(const std::vector<float>& audio_samples);
    
    // Returns true if audio contains speech
    bool has_speech(const std::vector<float>& audio_samples);
    
private:
    float silence_threshold_;
    int silence_duration_samples_;
    std::vector<float> recent_energy_;
    size_t energy_window_size_;
};

// Audio Stream Processor - main coordinator
class AudioStreamProcessor {
public:
    using TranscriptionCallback = std::function<void(const std::string& session_id, const std::string& text)>;
    
    AudioStreamProcessor(STTInterface* stt_engine);
    ~AudioStreamProcessor();
    
    bool start();
    void stop();
    
    // Add audio from RTP stream
    void add_audio(const std::string& session_id, const std::vector<float>& audio_samples);
    
    // Set callback for transcription results
    void set_transcription_callback(TranscriptionCallback callback);
    
    // Configuration
    void set_max_chunk_duration_ms(int ms) { max_chunk_duration_ms_ = ms; }
    void set_min_chunk_duration_ms(int ms) { min_chunk_duration_ms_ = ms; }
    
private:
    struct SessionBuffer {
        std::vector<float> audio_buffer;
        std::chrono::steady_clock::time_point last_activity;
        std::chrono::steady_clock::time_point chunk_start;
        bool has_speech = false;
    };
    
    STTInterface* stt_engine_;
    SimpleVAD vad_;
    
    // Session management
    std::unordered_map<std::string, SessionBuffer> session_buffers_;
    std::mutex buffers_mutex_;
    
    // Processing thread
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
    // Configuration
    int max_chunk_duration_ms_ = 10000;  // Max 10 seconds
    int min_chunk_duration_ms_ = 1000;   // Min 1 second
    int sample_rate_ = 16000;            // 16kHz
    
    // Callback
    TranscriptionCallback transcription_callback_;
    
    // Internal methods
    void processing_loop();
    void process_session_buffer(const std::string& session_id, SessionBuffer& buffer);
    bool should_process_chunk(const SessionBuffer& buffer);
    void cleanup_old_sessions();
};

// Whisper STT Implementation
class WhisperSTT : public STTInterface {
public:
    WhisperSTT(struct whisper_context* ctx);
    
    std::string transcribe(const std::vector<float>& audio_samples) override;
    bool is_ready() override;
    
private:
    struct whisper_context* whisper_ctx_;
    std::mutex whisper_mutex_;
};
