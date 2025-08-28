#include "audio-processor-service.h"
#include <iostream>
#include <sstream>
#include <iomanip>

// ServiceAudioInterface Implementation
AudioProcessorService::ServiceAudioInterface::ServiceAudioInterface(AudioProcessorService* service)
    : service_(service) {}

void AudioProcessorService::ServiceAudioInterface::send_to_whisper(const std::string& session_id, const std::vector<float>& audio_samples) {
    if (service_) {
        service_->handle_whisper_transcription(session_id, audio_samples);
    }
}

void AudioProcessorService::ServiceAudioInterface::on_audio_processing_error(const std::string& session_id, const std::string& error) {
    std::cout << "❌ Audio processing error for session " << session_id << ": " << error << std::endl;
}

void AudioProcessorService::ServiceAudioInterface::on_audio_chunk_ready(const std::string& session_id, size_t chunk_size_samples) {
    std::cout << "✅ Audio chunk ready for session " << session_id << ": " << chunk_size_samples << " samples" << std::endl;
}

// AudioProcessorService Implementation
AudioProcessorService::AudioProcessorService()
    : running_(false), service_port_(8083), database_(nullptr),
      whisper_endpoint_("http://localhost:8082"), total_packets_processed_(0), active_sessions_(0) {
    
    // Create audio interface
    audio_interface_ = std::make_unique<ServiceAudioInterface>(this);
    
    // Create simple audio processor
    audio_processor_ = std::make_unique<SimpleAudioProcessor>(audio_interface_.get());
}

AudioProcessorService::~AudioProcessorService() {
    stop();
}

bool AudioProcessorService::start(int port) {
    if (running_.load()) return true;
    
    service_port_ = port;
    
    // Start audio processor
    if (!audio_processor_->start()) {
        std::cout << "❌ Failed to start audio processor" << std::endl;
        return false;
    }
    
    running_.store(true);
    
    std::cout << "🎵 Audio Processor Service started on port " << port << std::endl;
    std::cout << "📡 Whisper endpoint: " << whisper_endpoint_ << std::endl;
    
    return true;
}

void AudioProcessorService::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    if (audio_processor_) {
        audio_processor_->stop();
    }
    
    std::cout << "🛑 Audio Processor Service stopped" << std::endl;
}

bool AudioProcessorService::create_session(const AudioSessionParams& params) {
    if (!running_.load() || !audio_processor_) return false;
    
    audio_processor_->start_session(params);
    active_sessions_.fetch_add(1);
    
    std::cout << "🎵 Audio processing session created: " << params.session_id 
              << " (line " << params.line_id << ", caller: " << params.caller_phone << ")" << std::endl;
    
    return true;
}

void AudioProcessorService::end_session(const std::string& session_id) {
    if (!audio_processor_) return;
    
    audio_processor_->end_session(session_id);
    active_sessions_.fetch_sub(1);
    
    std::cout << "🔚 Audio processing session ended: " << session_id << std::endl;
}

void AudioProcessorService::process_audio(const std::string& session_id, const RTPAudioPacket& packet) {
    if (!running_.load() || !audio_processor_) return;
    
    audio_processor_->process_audio(session_id, packet);
    total_packets_processed_.fetch_add(1);
}

void AudioProcessorService::handle_whisper_transcription(const std::string& session_id, const std::vector<float>& audio_samples) {
    std::cout << "📤 Sending " << audio_samples.size() << " samples to Whisper for session: " << session_id << std::endl;
    
    // TODO: Send to actual Whisper service at whisper_endpoint_
    // For now, simulate transcription
    std::string transcription = simulate_whisper_transcription(audio_samples);
    
    // Update database
    update_database_transcription(session_id, transcription);
}

std::string AudioProcessorService::simulate_whisper_transcription(const std::vector<float>& audio_samples) {
    // Simple simulation based on audio characteristics
    float energy = 0.0f;
    for (float sample : audio_samples) {
        energy += sample * sample;
    }
    energy = std::sqrt(energy / audio_samples.size());
    
    std::ostringstream oss;
    oss << "Audio chunk processed (";
    oss << std::fixed << std::setprecision(1) << (audio_samples.size() / 16000.0f) << "s, ";
    oss << std::fixed << std::setprecision(3) << energy << " energy)";
    
    return oss.str();
}

void AudioProcessorService::update_database_transcription(const std::string& session_id, const std::string& text) {
    if (database_) {
        database_->update_session_whisper(session_id, text + " ");
        std::cout << "✅ Transcription saved to database: \"" << text << "\"" << std::endl;
    } else {
        std::cout << "⚠️ No database connection, transcription not saved: \"" << text << "\"" << std::endl;
    }
}

AudioProcessorService::ServiceStatus AudioProcessorService::get_status() const {
    ServiceStatus status;
    status.is_running = running_.load();
    status.active_sessions = active_sessions_.load();
    status.total_packets_processed = total_packets_processed_.load();
    status.whisper_endpoint = whisper_endpoint_;
    
    if (audio_processor_) {
        status.processor_type = audio_processor_->get_processor_name();
    } else {
        status.processor_type = "None";
    }
    
    return status;
}

// Factory Implementation
std::unique_ptr<AudioProcessorService> AudioProcessorServiceFactory::create(ProcessorType type) {
    // For now, always create simple processor
    // In future, could create different types based on parameter
    return std::make_unique<AudioProcessorService>();
}
