#include "whisper-connector.h"
#include <iostream>
#include <chrono>
#include <curl/curl.h>
#include <cstring>

// Helper function for HTTP response handling
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

// Helper function to convert float audio to WAV format
static std::vector<uint8_t> convert_to_wav(const std::vector<float>& audio_data) {
    std::vector<uint8_t> wav_data;

    // WAV header (44 bytes)
    const int sample_rate = 16000;
    const int channels = 1;
    const int bits_per_sample = 16;
    const int data_size = audio_data.size() * sizeof(int16_t);
    const int file_size = 36 + data_size;

    // RIFF header
    wav_data.insert(wav_data.end(), {'R', 'I', 'F', 'F'});
    wav_data.insert(wav_data.end(), (uint8_t*)&file_size, (uint8_t*)&file_size + 4);
    wav_data.insert(wav_data.end(), {'W', 'A', 'V', 'E'});

    // fmt chunk
    wav_data.insert(wav_data.end(), {'f', 'm', 't', ' '});
    int fmt_size = 16;
    wav_data.insert(wav_data.end(), (uint8_t*)&fmt_size, (uint8_t*)&fmt_size + 4);
    int16_t format = 1; // PCM
    wav_data.insert(wav_data.end(), (uint8_t*)&format, (uint8_t*)&format + 2);
    int16_t num_channels = channels;
    wav_data.insert(wav_data.end(), (uint8_t*)&num_channels, (uint8_t*)&num_channels + 2);
    wav_data.insert(wav_data.end(), (uint8_t*)&sample_rate, (uint8_t*)&sample_rate + 4);
    int byte_rate = sample_rate * channels * bits_per_sample / 8;
    wav_data.insert(wav_data.end(), (uint8_t*)&byte_rate, (uint8_t*)&byte_rate + 4);
    int16_t block_align = channels * bits_per_sample / 8;
    wav_data.insert(wav_data.end(), (uint8_t*)&block_align, (uint8_t*)&block_align + 2);
    int16_t bits = bits_per_sample;
    wav_data.insert(wav_data.end(), (uint8_t*)&bits, (uint8_t*)&bits + 2);

    // data chunk
    wav_data.insert(wav_data.end(), {'d', 'a', 't', 'a'});
    wav_data.insert(wav_data.end(), (uint8_t*)&data_size, (uint8_t*)&data_size + 4);

    // Convert float samples to 16-bit PCM
    for (float sample : audio_data) {
        int16_t pcm_sample = (int16_t)(sample * 32767.0f);
        wav_data.insert(wav_data.end(), (uint8_t*)&pcm_sample, (uint8_t*)&pcm_sample + 2);
    }

    return wav_data;
}

// WhisperConnector Implementation
WhisperConnector::WhisperConnector() : running_(false), connected_(false) {}

WhisperConnector::~WhisperConnector() {
    stop();
}

bool WhisperConnector::start(const std::string& whisper_endpoint) {
    if (running_.load()) return true;
    
    whisper_endpoint_ = whisper_endpoint;
    running_.store(true);
    
    // Start background worker
    worker_thread_ = std::thread(&WhisperConnector::worker_loop, this);
    
    // Start connection monitor
    connection_monitor_thread_ = std::thread(&WhisperConnector::connection_monitor_loop, this);
    
    std::cout << "🔗 WhisperConnector started, endpoint: " << whisper_endpoint_ << std::endl;
    return true;
}

void WhisperConnector::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) worker_thread_.join();
    if (connection_monitor_thread_.joinable()) connection_monitor_thread_.join();
    
    std::cout << "🔗 WhisperConnector stopped" << std::endl;
}

void WhisperConnector::send_chunk(const std::string& session_id, const std::vector<float>& audio_chunk) {
    if (!running_.load()) return;
    
    if (connected_.load()) {
        // Route to Whisper service
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            chunk_queue_.push({session_id, audio_chunk});
        }
        queue_cv_.notify_one();
    }
    // else: Route to null (drop chunk silently)
}

std::string WhisperConnector::transcribe_chunk(const std::string& session_id, const std::vector<float>& audio_chunk) {
    if (!connected_.load()) {
        std::cout << "⚠️  Whisper service not connected" << std::endl;
        return "";
    }

    std::cout << "📤 Transcribing " << audio_chunk.size() << " samples for session: " << session_id << std::endl;

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    // Convert audio to WAV format
    std::vector<uint8_t> wav_data = convert_to_wav(audio_chunk);

    // Prepare HTTP request
    std::string response_data;
    std::string url = whisper_endpoint_ + "/api/whisper/transcribe";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // Set headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: audio/wav");
    std::string session_header = "X-Session-ID: " + session_id;
    headers = curl_slist_append(headers, session_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Set audio data
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, wav_data.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, wav_data.size());

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && response_code == 200) {
        std::cout << "✅ Whisper transcription received: \"" << response_data << "\"" << std::endl;
        return response_data;
    } else {
        std::cout << "❌ Whisper transcription failed: " << res << " (HTTP " << response_code << ")" << std::endl;
        return "";
    }
}

void WhisperConnector::worker_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !chunk_queue_.empty() || !running_.load(); });
        
        if (!running_.load()) break;
        
        if (!chunk_queue_.empty()) {
            AudioChunk chunk = chunk_queue_.front();
            chunk_queue_.pop();
            lock.unlock();
            
            // Send to Whisper service
            send_chunk_to_whisper(chunk.session_id, chunk.audio_data);
        }
    }
}

void WhisperConnector::connection_monitor_loop() {
    while (running_.load()) {
        bool was_connected = connected_.load();
        bool is_connected = check_whisper_connection();
        
        if (was_connected != is_connected) {
            connected_.store(is_connected);
            if (connection_callback_) {
                connection_callback_(is_connected);
            }
            std::cout << "🔗 Whisper connection: " << (is_connected ? "CONNECTED" : "DISCONNECTED") << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

bool WhisperConnector::check_whisper_connection() {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // Simple health check to Whisper service
    std::string health_url = whisper_endpoint_ + "/health";
    curl_easy_setopt(curl, CURLOPT_URL, health_url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](void*, size_t, size_t, void*) -> size_t { return 0; });

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && response_code == 200);
}

bool WhisperConnector::send_chunk_to_whisper(const std::string& session_id, const std::vector<float>& audio_data) {
    std::cout << "📤 Sending " << audio_data.size() << " samples to Whisper for session: " << session_id << std::endl;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // Convert float audio to WAV format for Whisper
    std::vector<uint8_t> wav_data = ::convert_to_wav(audio_data);

    // Prepare HTTP POST to Whisper service
    std::string transcribe_url = whisper_endpoint_ + "/api/whisper/transcribe";

    // Create multipart form data
    curl_mime* form = curl_mime_init(curl);
    curl_mimepart* field;

    // Add session_id field
    field = curl_mime_addpart(form);
    curl_mime_name(field, "session_id");
    curl_mime_data(field, session_id.c_str(), CURL_ZERO_TERMINATED);

    // Add audio file field
    field = curl_mime_addpart(form);
    curl_mime_name(field, "audio");
    curl_mime_filename(field, "audio.wav");
    curl_mime_type(field, "audio/wav");
    curl_mime_data(field, (char*)wav_data.data(), wav_data.size());

    // Set up response handling
    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_URL, transcribe_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

    // Send request
    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_mime_free(form);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && response_code == 200) {
        std::cout << "✅ Whisper transcription response: " << response_data << std::endl;
        return true;
    } else {
        std::cout << "❌ Whisper request failed: " << res << " (HTTP " << response_code << ")" << std::endl;
        return false;
    }
}

// PiperConnector Implementation
PiperConnector::PiperConnector() : running_(false), sip_connected_(false),
    outgoing_jitter_buffer_(std::make_unique<RTPPacketBuffer>(8, 2)), // 8 max, 2 min
    rtp_session_(std::make_unique<RTPSession>()) {} // RFC 3550 compliant RTP session

PiperConnector::~PiperConnector() {
    stop();
    if (outgoing_jitter_buffer_) {
        outgoing_jitter_buffer_->stop();
    }
}

bool PiperConnector::start() {
    if (running_.load()) return true;
    
    running_.store(true);
    
    // Start background worker
    worker_thread_ = std::thread(&PiperConnector::worker_loop, this);
    
    std::cout << "🔊 PiperConnector started" << std::endl;
    return true;
}

void PiperConnector::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    queue_cv_.notify_all();
    
    if (worker_thread_.joinable()) worker_thread_.join();
    
    std::cout << "🔊 PiperConnector stopped" << std::endl;
}

void PiperConnector::send_piper_audio(const std::string& session_id, const std::vector<uint8_t>& audio_data) {
    if (!running_.load() || !rtp_session_) return;

    if (sip_connected_.load()) {
        // Create RFC 3550 compliant RTP packet from Piper audio
        RTPPacket rtp_packet = create_rtp_packet_from_audio(audio_data);

        // Serialize to wire format (NO session_id or internal data)
        std::vector<uint8_t> wire_packet = rtp_packet.serialize();

        // Add to jitter buffer for smooth transmission
        if (outgoing_jitter_buffer_) {
            outgoing_jitter_buffer_->push(wire_packet);
        }

        // Process jitter buffer
        process_jitter_buffer();

        std::cout << "🎵 Created RFC 3550 RTP packet from Piper audio: seq="
                  << rtp_packet.get_sequence_number() << ", ts=" << rtp_packet.get_timestamp()
                  << ", payload=" << rtp_packet.get_payload().size() << " bytes" << std::endl;
    }
    // else: Route to null (drop Piper stream silently)
}

void PiperConnector::set_sip_client_callback(std::function<void(const std::string&, const std::vector<uint8_t>&)> callback) {
    sip_callback_ = callback;
    sip_connected_.store(callback != nullptr);
}

void PiperConnector::worker_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !audio_queue_.empty() || !running_.load(); });
        
        if (!running_.load()) break;
        
        if (!audio_queue_.empty()) {
            PiperAudio audio = audio_queue_.front();
            audio_queue_.pop();
            lock.unlock();
            
            // Convert to G.711 RTP and send to SIP client
            std::vector<uint8_t> g711_rtp = convert_to_g711_rtp(audio.audio_data);
            if (sip_callback_) {
                sip_callback_(audio.session_id, g711_rtp);
            }
        }
    }
}

void PiperConnector::process_jitter_buffer() {
    if (!outgoing_jitter_buffer_) return;

    // Try to get buffered audio for smooth transmission
    std::vector<uint8_t> buffered_audio;
    if (outgoing_jitter_buffer_->try_pop(buffered_audio)) {
        // Send buffered audio to SIP client
        if (sip_callback_) {
            sip_callback_("", buffered_audio); // Empty session_id for now
            std::cout << "📤 Sent jitter-buffered audio: " << buffered_audio.size() << " bytes" << std::endl;
        }
    }
}

RTPPacket PiperConnector::create_rtp_packet_from_audio(const std::vector<uint8_t>& audio_data) {
    // Convert Piper audio to G.711 μ-law payload
    // Assuming Piper outputs 16-bit PCM, convert to float first
    std::vector<float> float_samples;
    float_samples.reserve(audio_data.size() / 2);

    for (size_t i = 0; i < audio_data.size(); i += 2) {
        if (i + 1 < audio_data.size()) {
            int16_t sample = static_cast<int16_t>(audio_data[i] | (audio_data[i + 1] << 8));
            float_samples.push_back(static_cast<float>(sample) / 32768.0f);
        }
    }

    // Convert to G.711 μ-law
    std::vector<uint8_t> g711_payload = RTPCodec::float_to_g711_ulaw(float_samples);

    // Create RFC 3550 compliant RTP packet
    // NO session_id or internal data is included
    return rtp_session_->create_packet(
        RTPPayloadType::PCMU,           // G.711 μ-law
        g711_payload,                   // Audio payload only
        RTPTiming::G711_TIMESTAMP_INCREMENT, // Standard 160 samples increment
        false                           // No marker bit
    );
}

std::vector<uint8_t> PiperConnector::convert_to_g711_rtp(const std::vector<uint8_t>& piper_audio) {
    // Legacy method - now creates full RTP packet
    RTPPacket packet = create_rtp_packet_from_audio(piper_audio);
    return packet.serialize();
}
