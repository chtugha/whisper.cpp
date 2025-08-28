#include "simple-audio-processor.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// G.711 μ-law decode table (fast lookup)
std::vector<float> G711Tables::ulaw_table_;
std::vector<float> G711Tables::alaw_table_;
bool G711Tables::tables_initialized_ = false;

void G711Tables::initialize_tables() {
    if (tables_initialized_) return;
    
    ulaw_table_.resize(256);
    alaw_table_.resize(256);
    
    // μ-law table
    const int16_t ulaw_decode[256] = {
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
    
    for (int i = 0; i < 256; ++i) {
        ulaw_table_[i] = ulaw_decode[i] / 32768.0f;
        // Simplified A-law (for demo - real A-law needs proper table)
        alaw_table_[i] = ((i & 0x80) ? -1.0f : 1.0f) * ((i & 0x7F) / 127.0f);
    }
    
    tables_initialized_ = true;
}

const std::vector<float>& G711Tables::get_ulaw_table() {
    initialize_tables();
    return ulaw_table_;
}

const std::vector<float>& G711Tables::get_alaw_table() {
    initialize_tables();
    return alaw_table_;
}

// SimpleAudioProcessor Implementation
SimpleAudioProcessor::SimpleAudioProcessor(SipAudioInterface* sip_interface)
    : sip_interface_(sip_interface), running_(false),
      chunk_duration_ms_(3000), vad_threshold_(0.01f), silence_timeout_ms_(500) {
    
    G711Tables::initialize_tables(); // Initialize lookup tables
}

SimpleAudioProcessor::~SimpleAudioProcessor() {
    stop();
}

bool SimpleAudioProcessor::start() {
    if (running_.load()) return true;
    
    running_.store(true);
    std::cout << "🎵 SimpleAudioProcessor started" << std::endl;
    return true;
}

void SimpleAudioProcessor::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Clear all sessions
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.clear();
    
    std::cout << "🛑 SimpleAudioProcessor stopped" << std::endl;
}

void SimpleAudioProcessor::start_session(const AudioSessionParams& params) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    sessions_.emplace(params.session_id, SessionState(params.session_id));
    
    std::cout << "🎵 Audio session started: " << params.session_id 
              << " (line " << params.line_id << ", caller: " << params.caller_phone << ")" << std::endl;
}

void SimpleAudioProcessor::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    // Send any remaining audio before ending
    auto it = sessions_.find(session_id);
    if (it != sessions_.end() && !it->second.audio_buffer.empty()) {
        send_audio_chunk(session_id, it->second);
    }
    
    sessions_.erase(session_id);
    std::cout << "🔚 Audio session ended: " << session_id << std::endl;
}

void SimpleAudioProcessor::process_audio(const std::string& session_id, const RTPAudioPacket& packet) {
    if (!running_.load()) return;
    
    // Fast audio decoding
    std::vector<float> audio_samples = decode_rtp_audio(packet);
    if (audio_samples.empty()) return;
    
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    
    SessionState& session = it->second;
    
    // Append audio to buffer
    session.audio_buffer.insert(session.audio_buffer.end(), audio_samples.begin(), audio_samples.end());
    
    // Fast VAD check
    if (has_speech(audio_samples)) {
        session.has_speech = true;
        session.last_speech_time = std::chrono::steady_clock::now();
    }
    
    // Check if we should send chunk
    if (should_send_chunk(session)) {
        send_audio_chunk(session_id, session);
    }
}

std::vector<float> SimpleAudioProcessor::decode_rtp_audio(const RTPAudioPacket& packet) {
    if (packet.audio_data.empty()) return {};
    
    // Fast codec detection and decoding
    switch (packet.payload_type) {
        case 0:  // G.711 μ-law
            return convert_g711_ulaw(packet.audio_data);
        case 8:  // G.711 A-law
            return convert_g711_alaw(packet.audio_data);
        case 10: // PCM 16-bit
        case 11:
            return convert_pcm16(packet.audio_data);
        default:
            return {}; // Unsupported codec
    }
}

std::vector<float> SimpleAudioProcessor::convert_g711_ulaw(const std::vector<uint8_t>& data) {
    const auto& table = G711Tables::get_ulaw_table();
    std::vector<float> samples;
    samples.reserve(data.size() * 2); // Upsample 8kHz to 16kHz
    
    for (uint8_t byte : data) {
        float sample = table[byte];
        samples.push_back(sample); // Original sample
        samples.push_back(sample); // Duplicate for 16kHz
    }
    
    return samples;
}

std::vector<float> SimpleAudioProcessor::convert_g711_alaw(const std::vector<uint8_t>& data) {
    const auto& table = G711Tables::get_alaw_table();
    std::vector<float> samples;
    samples.reserve(data.size() * 2);
    
    for (uint8_t byte : data) {
        float sample = table[byte];
        samples.push_back(sample);
        samples.push_back(sample); // Upsample to 16kHz
    }
    
    return samples;
}

std::vector<float> SimpleAudioProcessor::convert_pcm16(const std::vector<uint8_t>& data) {
    std::vector<float> samples;
    samples.reserve(data.size() / 2);
    
    for (size_t i = 0; i < data.size(); i += 2) {
        if (i + 1 < data.size()) {
            int16_t sample = (data[i + 1] << 8) | data[i];
            samples.push_back(sample / 32768.0f);
        }
    }
    
    return samples;
}

bool SimpleAudioProcessor::has_speech(const std::vector<float>& samples) {
    return calculate_energy(samples) > vad_threshold_;
}

float SimpleAudioProcessor::calculate_energy(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0f;
    
    float sum = 0.0f;
    for (float sample : samples) {
        sum += sample * sample;
    }
    
    return std::sqrt(sum / samples.size());
}

bool SimpleAudioProcessor::should_send_chunk(const SessionState& session) {
    if (!session.has_speech || session.audio_buffer.empty()) return false;
    
    auto now = std::chrono::steady_clock::now();
    auto chunk_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.chunk_start_time).count();
    auto silence_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.last_speech_time).count();
    
    return (chunk_duration >= chunk_duration_ms_) || (silence_duration >= silence_timeout_ms_);
}

void SimpleAudioProcessor::send_audio_chunk(const std::string& session_id, SessionState& session) {
    if (session.audio_buffer.empty()) return;
    
    // Prepare chunk for Whisper (16kHz, proper size)
    std::vector<float> whisper_chunk = prepare_whisper_chunk(session.audio_buffer);
    
    // Send to SIP interface
    if (sip_interface_) {
        sip_interface_->send_to_whisper(session_id, whisper_chunk);
        sip_interface_->on_audio_chunk_ready(session_id, whisper_chunk.size());
    }
    
    // Reset session buffer
    session.audio_buffer.clear();
    session.has_speech = false;
    session.chunk_start_time = std::chrono::steady_clock::now();
    
    std::cout << "📤 Sent audio chunk: " << whisper_chunk.size() << " samples for session " << session_id << std::endl;
}

std::vector<float> SimpleAudioProcessor::prepare_whisper_chunk(const std::vector<float>& audio) {
    std::vector<float> chunk = audio;
    
    // Ensure minimum size (1 second at 16kHz)
    size_t min_samples = 16000;
    if (chunk.size() < min_samples) {
        chunk.resize(min_samples, 0.0f); // Pad with silence
    }
    
    // Ensure maximum size (30 seconds at 16kHz)
    size_t max_samples = 16000 * 30;
    if (chunk.size() > max_samples) {
        chunk.resize(max_samples);
    }
    
    return chunk;
}

// Factory Implementation
std::unique_ptr<AudioProcessor> AudioProcessorFactory::create(ProcessorType type) {
    switch (type) {
        case ProcessorType::SIMPLE_PIPELINE:
        case ProcessorType::FAST_PIPELINE:
            return nullptr; // Need SipAudioInterface parameter
        case ProcessorType::DEBUG_PIPELINE:
            return nullptr; // Need SipAudioInterface parameter
        default:
            return nullptr;
    }
}

std::vector<std::string> AudioProcessorFactory::get_available_types() {
    return {"SimpleAudioProcessor", "DebugAudioProcessor"};
}
