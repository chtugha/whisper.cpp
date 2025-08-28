#pragma once

#include "audio-processor-interface.h"
#include "simple-audio-processor.h"
#include "whisper-connector.h"
#include "database.h"
#include "jitter-buffer.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <unordered_map>

// Standalone Audio Processor Service
// Runs independently, SIP client connects to it via interface
class AudioProcessorService {
public:
    AudioProcessorService();
    ~AudioProcessorService();
    
    // Service lifecycle
    bool start(int port = 8083);
    void stop();
    bool is_running() const { return running_.load(); }

    // Sleep/Wake mechanism
    void activate_for_call();   // Wake up processor for incoming call
    void deactivate_after_call(); // Put processor to sleep after call ends
    bool is_active() const { return active_.load(); }
    
    // Configuration
    void set_database(Database* database);
    void set_whisper_endpoint(const std::string& endpoint) { whisper_endpoint_ = endpoint; }
    
    // Audio processing interface for SIP clients
    bool create_session(const AudioSessionParams& params);
    void end_session(const std::string& session_id);
    void process_audio(const std::string& session_id, const RTPAudioPacket& packet);

    // Connection management
    void set_sip_client_callback(std::function<void(const std::string&, const std::vector<uint8_t>&)> callback);
    void handle_outgoing_audio(const std::string& session_id, const std::vector<uint8_t>& audio_data);
    
    // Status
    struct ServiceStatus {
        bool is_running;
        int active_sessions;
        std::string processor_type;
        size_t total_packets_processed;
        std::string whisper_endpoint;
    };
    ServiceStatus get_status() const;
    
private:
    // Audio processor implementation
    class ServiceAudioInterface : public SipAudioInterface {
    public:
        ServiceAudioInterface(AudioProcessorService* service);
        
        void send_to_whisper(const std::string& session_id, const std::vector<float>& audio_samples) override;
        void on_audio_processing_error(const std::string& session_id, const std::string& error) override;
        void on_audio_chunk_ready(const std::string& session_id, size_t chunk_size_samples) override;
        
    private:
        AudioProcessorService* service_;
    };
    
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<ServiceAudioInterface> audio_interface_;

    // Background connectors
    std::unique_ptr<WhisperConnector> whisper_connector_;
    std::unique_ptr<PiperConnector> piper_connector_;

    // Service state
    std::atomic<bool> running_;
    std::atomic<bool> active_;  // true = processing calls, false = sleeping
    int service_port_;
    
    // Configuration
    Database* database_;
    std::string whisper_endpoint_;
    
    // Statistics
    std::atomic<size_t> total_packets_processed_;
    std::atomic<int> active_sessions_;

    // Connection to SIP client
    std::function<void(const std::string&, const std::vector<uint8_t>&)> sip_client_callback_;

    // Jitter buffers for stable audio processing
    std::unordered_map<std::string, std::unique_ptr<AudioChunkBuffer>> incoming_audio_buffers_;
    std::unordered_map<std::string, std::unique_ptr<RTPPacketBuffer>> outgoing_audio_buffers_;
    std::mutex buffers_mutex_;
    
    // Internal methods
    void handle_whisper_transcription(const std::string& session_id, const std::vector<float>& audio_samples);
    bool check_sip_client_connection();
    void update_database_transcription(const std::string& session_id, const std::string& text);
    std::string simulate_whisper_transcription(const std::vector<float>& audio_samples);

    // Jitter buffer processing
    void process_buffered_audio(const std::string& session_id);
    void process_outgoing_buffer(const std::string& session_id);
    void cleanup_session_buffers(const std::string& session_id);
    std::vector<float> convert_g711_ulaw_to_float(const std::vector<uint8_t>& data);
    std::vector<float> convert_g711_alaw_to_float(const std::vector<uint8_t>& data);
    std::vector<uint8_t> convert_float_to_g711_ulaw(const std::vector<float>& samples);
};

// Audio Processor Service Factory
class AudioProcessorServiceFactory {
public:
    enum class ProcessorType {
        SIMPLE,
        FAST,
        DEBUG
    };
    
    static std::unique_ptr<AudioProcessorService> create(ProcessorType type = ProcessorType::SIMPLE);
};
