#pragma once

#include "audio-processor-interface.h"
#include "simple-audio-processor.h"
#include "database.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>

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
    
    // Configuration
    void set_database(Database* database) { database_ = database; }
    void set_whisper_endpoint(const std::string& endpoint) { whisper_endpoint_ = endpoint; }
    
    // Audio processing interface for SIP clients
    bool create_session(const AudioSessionParams& params);
    void end_session(const std::string& session_id);
    void process_audio(const std::string& session_id, const RTPAudioPacket& packet);
    
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
    
    // Service state
    std::atomic<bool> running_;
    int service_port_;
    
    // Configuration
    Database* database_;
    std::string whisper_endpoint_;
    
    // Statistics
    std::atomic<size_t> total_packets_processed_;
    std::atomic<int> active_sessions_;
    
    // Internal methods
    void handle_whisper_transcription(const std::string& session_id, const std::vector<float>& audio_samples);
    void update_database_transcription(const std::string& session_id, const std::string& text);
    std::string simulate_whisper_transcription(const std::vector<float>& audio_samples);
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
