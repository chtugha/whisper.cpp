#include "audio-pipeline.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>

// G.711 μ-law and A-law lookup tables for fast decoding
static const int16_t ulaw_table[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

AudioPipeline::AudioPipeline() 
    : target_sample_rate_(AudioFormat::WHISPER_SAMPLE_RATE),
      target_channels_(AudioFormat::WHISPER_CHANNELS),
      vad_threshold_(0.01f),
      silence_duration_ms_(500),
      min_chunk_duration_ms_(1000),
      max_chunk_duration_ms_(8000) {
    
    initialize_codec_mappings();
}

AudioPipeline::~AudioPipeline() = default;

void AudioPipeline::initialize_codec_mappings() {
    // Standard RTP payload type mappings
    codec_map_[0] = AudioCodec::G711_ULAW;   // PCMU
    codec_map_[8] = AudioCodec::G711_ALAW;   // PCMA  
    codec_map_[9] = AudioCodec::G722;        // G722
    codec_map_[10] = AudioCodec::PCM_16;     // L16 stereo
    codec_map_[11] = AudioCodec::PCM_16;     // L16 mono
    // Dynamic payload types 96-127 would be negotiated via SDP
}

std::vector<AudioChunk> AudioPipeline::process_rtp_packet(const std::string& session_id, const RTPPacket& packet) {
    std::vector<AudioChunk> output_chunks;
    
    // Step 1: Extract audio from RTP packet
    AudioChunk raw_audio = extract_audio_from_rtp(packet);
    if (raw_audio.is_empty()) {
        return output_chunks;
    }
    
    raw_audio.session_id = session_id;
    
    // Step 2: Convert to standard format (16kHz, mono, float)
    AudioChunk standardized = convert_to_standard_format(raw_audio);
    if (standardized.is_empty()) {
        return output_chunks;
    }
    
    // Step 3: Add to session buffer and check for speech segments
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    auto& buffer = session_buffers_[session_id];
    
    // Append audio to session buffer
    buffer.accumulated_audio.insert(buffer.accumulated_audio.end(), 
                                   standardized.samples.begin(), 
                                   standardized.samples.end());
    buffer.sample_rate = standardized.sample_rate;
    
    // Step 4: Check if we have speech
    if (has_speech(standardized.samples)) {
        buffer.has_speech = true;
        buffer.last_speech_time = std::chrono::steady_clock::now();
    }
    
    // Step 5: Check if we should create a chunk
    if (should_create_chunk(buffer)) {
        std::vector<float> chunk_audio = extract_chunk_from_buffer(buffer);
        
        if (!chunk_audio.empty()) {
            // Step 6: Prepare Whisper-compatible chunk
            AudioChunk whisper_chunk;
            whisper_chunk.samples = prepare_whisper_chunk({AudioChunk(chunk_audio, target_sample_rate_, target_channels_, session_id)}).samples;
            whisper_chunk.sample_rate = target_sample_rate_;
            whisper_chunk.channels = target_channels_;
            whisper_chunk.session_id = session_id;
            whisper_chunk.timestamp = std::chrono::steady_clock::now();
            
            output_chunks.push_back(whisper_chunk);
            
            std::cout << "🎵 Created Whisper chunk: " << whisper_chunk.samples.size() 
                      << " samples (" << whisper_chunk.duration_seconds() << "s) for session " << session_id << std::endl;
        }
    }
    
    return output_chunks;
}

AudioChunk AudioPipeline::extract_audio_from_rtp(const RTPPacket& packet) {
    AudioChunk chunk;
    
    if (packet.payload.empty()) {
        return chunk;
    }
    
    // Detect codec from payload type
    AudioCodec codec = detect_codec(packet.payload_type);
    
    // Decode based on codec
    std::vector<float> samples;
    int sample_rate = AudioFormat::SIP_SAMPLE_RATE_8K; // Default
    
    switch (codec) {
        case AudioCodec::G711_ULAW:
            samples = decode_g711_ulaw(packet.payload);
            sample_rate = AudioFormat::SIP_SAMPLE_RATE_8K;
            break;
            
        case AudioCodec::G711_ALAW:
            samples = decode_g711_alaw(packet.payload);
            sample_rate = AudioFormat::SIP_SAMPLE_RATE_8K;
            break;
            
        case AudioCodec::G722:
            samples = decode_g722(packet.payload);
            sample_rate = AudioFormat::SIP_SAMPLE_RATE_16K;
            break;
            
        case AudioCodec::PCM_16:
            samples = decode_pcm16(packet.payload);
            sample_rate = AudioFormat::SIP_SAMPLE_RATE_16K;
            break;
            
        case AudioCodec::PCM_8:
            samples = decode_pcm8(packet.payload);
            sample_rate = AudioFormat::SIP_SAMPLE_RATE_8K;
            break;
            
        default:
            std::cout << "⚠️ Unsupported codec for payload type: " << (int)packet.payload_type << std::endl;
            return chunk;
    }
    
    if (!samples.empty()) {
        chunk.samples = samples;
        chunk.sample_rate = sample_rate;
        chunk.channels = 1; // SIP is typically mono
        chunk.timestamp = packet.received_time;
    }
    
    return chunk;
}

AudioChunk AudioPipeline::convert_to_standard_format(const AudioChunk& input) {
    AudioChunk output = input;
    
    // Convert to mono if needed
    if (input.channels > 1) {
        output.samples = convert_to_mono(input.samples, input.channels);
        output.channels = 1;
    }
    
    // Resample to target sample rate if needed
    if (input.sample_rate != target_sample_rate_) {
        output.samples = resample_audio(output.samples, input.sample_rate, target_sample_rate_);
        output.sample_rate = target_sample_rate_;
    }
    
    return output;
}

std::vector<float> AudioPipeline::decode_g711_ulaw(const std::vector<uint8_t>& payload) {
    std::vector<float> samples;
    samples.reserve(payload.size());
    
    for (uint8_t byte : payload) {
        int16_t sample = ulaw_table[byte];
        samples.push_back(sample / 32768.0f); // Normalize to [-1.0, 1.0]
    }
    
    return samples;
}

std::vector<float> AudioPipeline::decode_g711_alaw(const std::vector<uint8_t>& payload) {
    std::vector<float> samples;
    samples.reserve(payload.size());
    
    // A-law decoding (simplified - would need full A-law table)
    for (uint8_t byte : payload) {
        // A-law to linear conversion (simplified)
        int16_t sample = (byte & 0x80) ? -(((byte & 0x7F) << 4) + 8) : (((byte & 0x7F) << 4) + 8);
        samples.push_back(sample / 32768.0f);
    }
    
    return samples;
}

std::vector<float> AudioPipeline::decode_pcm16(const std::vector<uint8_t>& payload) {
    std::vector<float> samples;
    samples.reserve(payload.size() / 2);
    
    for (size_t i = 0; i < payload.size(); i += 2) {
        if (i + 1 < payload.size()) {
            int16_t sample = (payload[i + 1] << 8) | payload[i]; // Little-endian
            samples.push_back(sample / 32768.0f);
        }
    }
    
    return samples;
}

std::vector<float> AudioPipeline::decode_pcm8(const std::vector<uint8_t>& payload) {
    std::vector<float> samples;
    samples.reserve(payload.size());
    
    for (uint8_t byte : payload) {
        int8_t sample = byte - 128; // Convert unsigned to signed
        samples.push_back(sample / 128.0f);
    }
    
    return samples;
}

std::vector<float> AudioPipeline::decode_g722(const std::vector<uint8_t>& payload) {
    // G.722 decoding is complex - this is a placeholder
    // In real implementation, you'd use a G.722 decoder library
    std::vector<float> samples;
    samples.reserve(payload.size() * 2); // G.722 typically expands
    
    for (uint8_t byte : payload) {
        // Simplified placeholder - real G.722 decoding needed
        samples.push_back((byte - 128) / 128.0f);
        samples.push_back((byte - 128) / 128.0f); // Duplicate for 16kHz
    }
    
    return samples;
}

bool AudioPipeline::has_speech(const std::vector<float>& samples) {
    if (samples.empty()) return false;
    
    float energy = calculate_rms_energy(samples);
    return energy > vad_threshold_;
}

float AudioPipeline::calculate_rms_energy(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float sum = 0.0f;
    for (float sample : samples) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / samples.size());
}

AudioCodec AudioPipeline::detect_codec(uint8_t payload_type) {
    auto it = codec_map_.find(payload_type);
    return (it != codec_map_.end()) ? it->second : AudioCodec::UNKNOWN;
}

void AudioPipeline::init_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    session_buffers_[session_id] = SessionBuffer();
    std::cout << "🎵 Initialized audio pipeline session: " << session_id << std::endl;
}

void AudioPipeline::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    session_buffers_.erase(session_id);
    std::cout << "🔚 Ended audio pipeline session: " << session_id << std::endl;
}

bool AudioPipeline::should_create_chunk(const SessionBuffer& buffer) {
    if (!buffer.has_speech || buffer.accumulated_audio.empty()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto chunk_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer.chunk_start_time).count();
    auto silence_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer.last_speech_time).count();

    // Create chunk if:
    // 1. We have minimum duration AND silence gap detected
    // 2. OR we've reached maximum duration (timeout)
    return (chunk_duration >= min_chunk_duration_ms_ && silence_duration >= silence_duration_ms_) ||
           (chunk_duration >= max_chunk_duration_ms_);
}

std::vector<float> AudioPipeline::extract_chunk_from_buffer(SessionBuffer& buffer) {
    std::vector<float> chunk = buffer.accumulated_audio;

    // Reset buffer for next chunk
    buffer.accumulated_audio.clear();
    buffer.has_speech = false;
    buffer.chunk_start_time = std::chrono::steady_clock::now();

    return chunk;
}

AudioChunk AudioPipeline::prepare_whisper_chunk(const std::vector<AudioChunk>& segments) {
    if (segments.empty()) {
        return AudioChunk();
    }

    // Combine all segments
    std::vector<float> combined_audio;
    for (const auto& segment : segments) {
        combined_audio.insert(combined_audio.end(), segment.samples.begin(), segment.samples.end());
    }

    // Ensure minimum length for Whisper
    size_t min_samples = AudioFormat::WHISPER_MIN_CHUNK_SAMPLES;
    if (combined_audio.size() < min_samples) {
        combined_audio = pad_with_silence(combined_audio, min_samples);
    }

    // Ensure maximum length for Whisper
    size_t max_samples = AudioFormat::WHISPER_CHUNK_SAMPLES;
    if (combined_audio.size() > max_samples) {
        combined_audio = trim_to_size(combined_audio, max_samples);
    }

    AudioChunk result;
    result.samples = combined_audio;
    result.sample_rate = target_sample_rate_;
    result.channels = target_channels_;
    result.session_id = segments[0].session_id;

    return result;
}

std::vector<float> AudioPipeline::pad_with_silence(const std::vector<float>& input, size_t target_samples) {
    if (input.size() >= target_samples) {
        return input;
    }

    std::vector<float> padded = input;
    padded.resize(target_samples, 0.0f); // Pad with silence (zeros)

    return padded;
}

std::vector<float> AudioPipeline::trim_to_size(const std::vector<float>& input, size_t max_samples) {
    if (input.size() <= max_samples) {
        return input;
    }

    return std::vector<float>(input.begin(), input.begin() + max_samples);
}

std::vector<float> AudioPipeline::resample_audio(const std::vector<float>& input, int input_rate, int output_rate) {
    if (input_rate == output_rate) {
        return input;
    }

    // Simple linear interpolation resampling (for production, use a proper resampler)
    double ratio = (double)output_rate / input_rate;
    size_t output_size = (size_t)(input.size() * ratio);

    std::vector<float> output;
    output.reserve(output_size);

    for (size_t i = 0; i < output_size; ++i) {
        double src_index = i / ratio;
        size_t index = (size_t)src_index;
        double frac = src_index - index;

        if (index + 1 < input.size()) {
            float sample = input[index] * (1.0 - frac) + input[index + 1] * frac;
            output.push_back(sample);
        } else if (index < input.size()) {
            output.push_back(input[index]);
        }
    }

    return output;
}

std::vector<float> AudioPipeline::convert_to_mono(const std::vector<float>& input, int channels) {
    if (channels <= 1) {
        return input;
    }

    std::vector<float> mono;
    mono.reserve(input.size() / channels);

    for (size_t i = 0; i < input.size(); i += channels) {
        float sum = 0.0f;
        for (int ch = 0; ch < channels && i + ch < input.size(); ++ch) {
            sum += input[i + ch];
        }
        mono.push_back(sum / channels);
    }

    return mono;
}
