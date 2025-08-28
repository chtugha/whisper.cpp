#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

// Standard audio formats for the pipeline
namespace AudioFormat {
    // Whisper requirements
    constexpr int WHISPER_SAMPLE_RATE = 16000;  // 16kHz
    constexpr int WHISPER_CHANNELS = 1;         // Mono
    constexpr int WHISPER_CHUNK_SAMPLES = WHISPER_SAMPLE_RATE * 30; // 30 seconds max
    constexpr int WHISPER_MIN_CHUNK_SAMPLES = WHISPER_SAMPLE_RATE * 1; // 1 second min
    
    // SIP/RTP common formats
    constexpr int SIP_SAMPLE_RATE_8K = 8000;   // G.711 (μ-law, A-law)
    constexpr int SIP_SAMPLE_RATE_16K = 16000; // G.722
    constexpr int SIP_SAMPLE_RATE_48K = 48000; // Opus
}

// Audio codec types commonly used in SIP
enum class AudioCodec {
    UNKNOWN,
    G711_ULAW,    // μ-law (8kHz, 8-bit)
    G711_ALAW,    // A-law (8kHz, 8-bit)
    G722,         // G.722 (16kHz, 7-bit)
    OPUS,         // Opus (variable rate)
    PCM_16,       // Linear PCM 16-bit
    PCM_8         // Linear PCM 8-bit
};

// Raw audio data container
struct AudioChunk {
    std::vector<float> samples;           // Normalized float samples [-1.0, 1.0]
    int sample_rate;                      // Sample rate in Hz
    int channels;                         // Number of channels
    std::chrono::steady_clock::time_point timestamp;
    std::string session_id;
    
    AudioChunk() : sample_rate(0), channels(0), timestamp(std::chrono::steady_clock::now()) {}
    
    AudioChunk(const std::vector<float>& data, int rate, int ch, const std::string& sid)
        : samples(data), sample_rate(rate), channels(ch), session_id(sid),
          timestamp(std::chrono::steady_clock::now()) {}
    
    // Utility methods
    float duration_seconds() const {
        return sample_rate > 0 ? (float)samples.size() / (sample_rate * channels) : 0.0f;
    }
    
    size_t duration_ms() const {
        return sample_rate > 0 ? (samples.size() * 1000) / (sample_rate * channels) : 0;
    }
    
    bool is_empty() const { return samples.empty(); }
    void clear() { samples.clear(); }
};

// SIP/RTP packet structure (simplified)
struct RTPPacket {
    uint8_t payload_type;                 // RTP payload type (codec identifier)
    uint16_t sequence_number;             // RTP sequence number
    uint32_t timestamp;                   // RTP timestamp
    std::vector<uint8_t> payload;         // Raw audio payload
    std::chrono::steady_clock::time_point received_time;
    
    RTPPacket() : payload_type(0), sequence_number(0), timestamp(0),
                  received_time(std::chrono::steady_clock::now()) {}
};

// Audio processing pipeline stages
class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();
    
    // Pipeline configuration
    void set_target_sample_rate(int rate) { target_sample_rate_ = rate; }
    void set_target_channels(int channels) { target_channels_ = channels; }
    void set_vad_threshold(float threshold) { vad_threshold_ = threshold; }
    void set_silence_duration_ms(int ms) { silence_duration_ms_ = ms; }
    void set_chunk_duration_limits(int min_ms, int max_ms);
    
    // Main processing method
    std::vector<AudioChunk> process_rtp_packet(const std::string& session_id, const RTPPacket& packet);
    
    // Individual pipeline stages (can be used separately)
    AudioChunk extract_audio_from_rtp(const RTPPacket& packet);
    AudioChunk convert_to_standard_format(const AudioChunk& input);
    std::vector<AudioChunk> detect_speech_segments(const AudioChunk& input);
    AudioChunk prepare_whisper_chunk(const std::vector<AudioChunk>& segments);
    
    // Codec detection and handling
    AudioCodec detect_codec(uint8_t payload_type);
    void set_codec_mapping(uint8_t payload_type, AudioCodec codec);
    
    // Session management
    void init_session(const std::string& session_id);
    void end_session(const std::string& session_id);
    void flush_session(const std::string& session_id, std::vector<AudioChunk>& output);
    
private:
    // Configuration
    int target_sample_rate_;
    int target_channels_;
    float vad_threshold_;
    int silence_duration_ms_;
    int min_chunk_duration_ms_;
    int max_chunk_duration_ms_;
    
    // Codec mapping
    std::unordered_map<uint8_t, AudioCodec> codec_map_;
    
    // Per-session audio buffers
    struct SessionBuffer {
        std::vector<float> accumulated_audio;
        std::chrono::steady_clock::time_point last_speech_time;
        std::chrono::steady_clock::time_point chunk_start_time;
        bool has_speech;
        int sample_rate;
        
        SessionBuffer() : has_speech(false), sample_rate(AudioFormat::WHISPER_SAMPLE_RATE),
                         last_speech_time(std::chrono::steady_clock::now()),
                         chunk_start_time(std::chrono::steady_clock::now()) {}
    };
    
    std::unordered_map<std::string, SessionBuffer> session_buffers_;
    std::mutex buffers_mutex_;
    
    // Audio processing methods
    std::vector<float> decode_g711_ulaw(const std::vector<uint8_t>& payload);
    std::vector<float> decode_g711_alaw(const std::vector<uint8_t>& payload);
    std::vector<float> decode_g722(const std::vector<uint8_t>& payload);
    std::vector<float> decode_pcm16(const std::vector<uint8_t>& payload);
    std::vector<float> decode_pcm8(const std::vector<uint8_t>& payload);
    
    // Format conversion
    std::vector<float> resample_audio(const std::vector<float>& input, int input_rate, int output_rate);
    std::vector<float> convert_to_mono(const std::vector<float>& input, int channels);
    
    // Voice Activity Detection (VAD)
    bool has_speech(const std::vector<float>& samples);
    bool is_silence_boundary(const std::vector<float>& samples);
    float calculate_rms_energy(const std::vector<float>& samples);
    
    // Chunk preparation
    std::vector<float> pad_with_silence(const std::vector<float>& input, size_t target_samples);
    std::vector<float> trim_to_size(const std::vector<float>& input, size_t max_samples);
    
    // Utility methods
    void initialize_codec_mappings();
    bool should_create_chunk(const SessionBuffer& buffer);
    std::vector<float> extract_chunk_from_buffer(SessionBuffer& buffer);
};
