#include "audio-processor-service.h"
#include "simple-audio-processor.h"
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
    : running_(false), active_(false), service_port_(8083), database_(nullptr),
      whisper_endpoint_("http://localhost:8082"), total_packets_processed_(0), active_sessions_(0) {
    
    // Create audio interface
    audio_interface_ = std::make_unique<ServiceAudioInterface>(this);
    
    // Create simple audio processor
    audio_processor_ = std::make_unique<SimpleAudioProcessor>(audio_interface_.get());

    // Connect database to processor for system speed configuration
    if (database_) {
        auto simple_processor = dynamic_cast<SimpleAudioProcessor*>(audio_processor_.get());
        if (simple_processor) {
            simple_processor->set_database(database_);
        }
    }
}

AudioProcessorService::~AudioProcessorService() {
    stop();
}

bool AudioProcessorService::start(int port) {
    if (running_.load()) return true;

    service_port_ = port;

    // Start audio processor (but keep it sleeping)
    if (!audio_processor_->start()) {
        std::cout << "❌ Failed to start audio processor" << std::endl;
        return false;
    }

    running_.store(true);
    active_.store(false); // Start in sleeping state

    std::cout << "😴 Audio Processor Service started (SLEEPING) on port " << port << std::endl;
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
    if (!running_.load() || !active_.load() || !audio_processor_) {
        // Processor sleeping - drop audio packets silently
        return;
    }

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

        // Add sleep/active state info
        if (running_.load()) {
            if (active_.load()) {
                status.processor_type += " (ACTIVE)";
            } else {
                status.processor_type += " (SLEEPING)";
            }
        }
    } else {
        status.processor_type = "None";
    }

    return status;
}

void AudioProcessorService::handle_outgoing_audio(const std::string& session_id, const std::vector<uint8_t>& audio_data) {
    // Simple background routing: SIP client vs null
    bool sip_client_connected = check_sip_client_connection();

    if (sip_client_connected) {
        // Route to SIP client for RTP transmission
        if (sip_client_callback_) {
            sip_client_callback_(session_id, audio_data);
        }
    }
    // else: Route to null (drop Piper stream silently)
}

bool AudioProcessorService::check_sip_client_connection() {
    // Simple connection check - if callback is set, assume connected
    return sip_client_callback_ != nullptr;
}

void AudioProcessorService::set_sip_client_callback(std::function<void(const std::string&, const std::vector<uint8_t>&)> callback) {
    sip_client_callback_ = callback;
}

void AudioProcessorService::set_database(Database* database) {
    database_ = database;

    // Also set database for the audio processor
    if (audio_processor_) {
        auto simple_processor = dynamic_cast<SimpleAudioProcessor*>(audio_processor_.get());
        if (simple_processor) {
            simple_processor->set_database(database);
        }
    }
}

void AudioProcessorService::activate_for_call() {
    if (!running_.load()) return;

    if (!active_.load()) {
        std::cout << "🚀 ACTIVATING Audio Processor - Call incoming!" << std::endl;

        // Start background connectors
        if (!whisper_connector_) {
            whisper_connector_ = std::make_unique<WhisperConnector>();
            whisper_connector_->start(whisper_endpoint_);
        }

        if (!piper_connector_) {
            piper_connector_ = std::make_unique<PiperConnector>();
            piper_connector_->start();
            piper_connector_->set_sip_client_callback(sip_client_callback_);
        }

        active_.store(true);
        std::cout << "✅ Audio Processor ACTIVE - Processing threads started" << std::endl;
    }
}

void AudioProcessorService::deactivate_after_call() {
    if (active_.load()) {
        std::cout << "😴 DEACTIVATING Audio Processor - Call ended" << std::endl;

        // Stop background connectors to free resources
        if (whisper_connector_) {
            whisper_connector_->stop();
            whisper_connector_.reset();
        }

        if (piper_connector_) {
            piper_connector_->stop();
            piper_connector_.reset();
        }

        active_.store(false);
        std::cout << "💤 Audio Processor SLEEPING - Processing threads stopped" << std::endl;
    }
}

// Factory Implementation
std::unique_ptr<AudioProcessorService> AudioProcessorServiceFactory::create(ProcessorType type) {
    // For now, always create simple processor
    // In future, could create different types based on parameter
    return std::make_unique<AudioProcessorService>();
}
