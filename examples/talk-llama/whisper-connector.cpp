#include "whisper-connector.h"
#include <iostream>
#include <chrono>
#include <curl/curl.h>

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
    // Simple HTTP ping to check if Whisper service is available
    // TODO: Implement actual HTTP check
    return true; // Assume connected for now
}

bool WhisperConnector::send_chunk_to_whisper(const std::string& session_id, const std::vector<float>& audio_data) {
    std::cout << "📤 Sending " << audio_data.size() << " samples to Whisper for session: " << session_id << std::endl;
    // TODO: Implement actual HTTP POST to Whisper service
    return true;
}

// PiperConnector Implementation
PiperConnector::PiperConnector() : running_(false), sip_connected_(false) {}

PiperConnector::~PiperConnector() {
    stop();
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
    if (!running_.load()) return;
    
    if (sip_connected_.load()) {
        // Route to SIP client
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            audio_queue_.push({session_id, audio_data});
        }
        queue_cv_.notify_one();
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

std::vector<uint8_t> PiperConnector::convert_to_g711_rtp(const std::vector<uint8_t>& piper_audio) {
    // TODO: Implement Piper audio to G.711 RTP conversion
    std::cout << "🔄 Converting " << piper_audio.size() << " bytes from Piper to G.711 RTP" << std::endl;
    return piper_audio; // Placeholder
}
