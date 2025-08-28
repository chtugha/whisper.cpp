#include "whisper-service.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace fs = std::filesystem;

WhisperService::WhisperService() 
    : whisper_ctx_(nullptr), running_(false), database_(nullptr),
      default_language_("en"), models_directory_("./models"),
      total_processed_audio_(0) {
    
    // Create models directory if it doesn't exist
    if (!fs::exists(models_directory_)) {
        fs::create_directories(models_directory_);
    }
    
    // Scan for existing models
    refresh_model_list();
}

WhisperService::~WhisperService() {
    stop();
    unload_model();
}

bool WhisperService::start() {
    if (running_.load()) return true;
    
    std::cout << "🎤 Starting Whisper Service..." << std::endl;
    
    running_.store(true);
    
    // Start cleanup thread for inactive sessions
    cleanup_thread_ = std::thread(&WhisperService::cleanup_loop, this);
    
    std::cout << "✅ Whisper Service started" << std::endl;
    return true;
}

void WhisperService::stop() {
    if (!running_.load()) return;
    
    std::cout << "🛑 Stopping Whisper Service..." << std::endl;
    
    running_.store(false);
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // Stop audio processor
    if (audio_processor_) {
        audio_processor_->stop();
        audio_processor_.reset();
    }
    
    // Clear all sessions
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }
    
    std::cout << "✅ Whisper Service stopped" << std::endl;
}

bool WhisperService::load_model(const std::string& model_path, const std::string& model_name) {
    std::lock_guard<std::mutex> lock(whisper_mutex_);
    
    if (!fs::exists(model_path)) {
        std::cout << "❌ Model file not found: " << model_path << std::endl;
        return false;
    }
    
    std::cout << "📥 Loading Whisper model: " << model_path << std::endl;
    
    // Unload existing model
    if (whisper_ctx_) {
        whisper_free(whisper_ctx_);
        whisper_ctx_ = nullptr;
    }
    
    // Load new model
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    
    whisper_ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    
    if (!whisper_ctx_) {
        std::cout << "❌ Failed to load Whisper model: " << model_path << std::endl;
        return false;
    }
    
    current_model_path_ = model_path;
    current_model_name_ = model_name.empty() ? fs::path(model_path).filename().string() : model_name;
    
    // Initialize audio processor with new model
    if (audio_processor_) {
        audio_processor_->stop();
    }
    
    auto whisper_stt = std::make_unique<WhisperServiceSTT>(this);
    audio_processor_ = std::make_unique<AudioStreamProcessor>(whisper_stt.release());
    
    // Set transcription callback
    audio_processor_->set_transcription_callback([this](const std::string& session_id, const std::string& text) {
        on_transcription_ready(session_id, text);
    });
    
    // Configure for phone calls
    audio_processor_->set_min_chunk_duration_ms(1000);
    audio_processor_->set_max_chunk_duration_ms(8000);
    
    if (!audio_processor_->start()) {
        std::cout << "❌ Failed to start audio processor" << std::endl;
        return false;
    }
    
    std::cout << "✅ Whisper model loaded successfully: " << current_model_name_ << std::endl;
    return true;
}

void WhisperService::unload_model() {
    std::lock_guard<std::mutex> lock(whisper_mutex_);
    
    if (audio_processor_) {
        audio_processor_->stop();
        audio_processor_.reset();
    }
    
    if (whisper_ctx_) {
        whisper_free(whisper_ctx_);
        whisper_ctx_ = nullptr;
        std::cout << "📤 Whisper model unloaded" << std::endl;
    }
    
    current_model_name_.clear();
    current_model_path_.clear();
}

bool WhisperService::init_session(const std::string& session_id) {
    if (session_id.empty()) return false;

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Check if session already exists
    if (sessions_.find(session_id) != sessions_.end()) {
        std::cout << "⚠️ Whisper session already exists: " << session_id << std::endl;
        return true;
    }

    auto session = std::make_unique<WhisperSession>(session_id);

    // Create per-session Whisper state if model is loaded
    if (whisper_ctx_) {
        session->state = whisper_init_state(whisper_ctx_);
        if (!session->state) {
            std::cout << "❌ Failed to create Whisper state for session: " << session_id << std::endl;
            return false;
        }
    }

    sessions_[session_id] = std::move(session);

    std::cout << "🎵 Initialized Whisper session: " << session_id << std::endl;
    return true;
}

void WhisperService::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->is_active = false;
        std::cout << "🔚 Ended Whisper session: " << session_id << std::endl;
        sessions_.erase(it);
    }
}

bool WhisperService::has_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.find(session_id) != sessions_.end();
}

void WhisperService::add_audio(const std::string& session_id, const std::vector<float>& audio_samples) {
    if (!audio_processor_ || audio_samples.empty()) return;
    
    // Update session activity
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->last_activity = std::chrono::steady_clock::now();
        }
    }
    
    // Send to audio processor
    audio_processor_->add_audio(session_id, audio_samples);
    total_processed_audio_.fetch_add(audio_samples.size());
}

std::vector<WhisperModel> WhisperService::get_available_models() {
    std::lock_guard<std::mutex> lock(models_mutex_);
    refresh_model_list();
    return discovered_models_;
}

void WhisperService::refresh_model_list() {
    discovered_models_.clear();
    
    if (!fs::exists(models_directory_)) return;
    
    for (const auto& entry : fs::directory_iterator(models_directory_)) {
        if (entry.is_regular_file()) {
            std::string path = entry.path().string();
            std::string name = entry.path().filename().string();
            
            // Check if it's a Whisper model file (common extensions)
            if (name.find(".bin") != std::string::npos || 
                name.find(".ggml") != std::string::npos) {
                
                WhisperModel model(name, path);
                model.file_size = entry.file_size();
                model.is_loaded = (path == current_model_path_);
                
                // Try to extract language from filename
                if (name.find(".en.") != std::string::npos) model.language = "en";
                else if (name.find(".de.") != std::string::npos) model.language = "de";
                else if (name.find(".fr.") != std::string::npos) model.language = "fr";
                else model.language = "auto";
                
                discovered_models_.push_back(model);
            }
        }
    }
    
    std::cout << "📋 Found " << discovered_models_.size() << " Whisper models" << std::endl;
}

std::string WhisperService::generate_session_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    
    return "whisper-session-" + std::to_string(dis(gen));
}

void WhisperService::cleanup_loop() {
    while (running_.load()) {
        cleanup_inactive_sessions();
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void WhisperService::cleanup_inactive_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        auto idle_time = std::chrono::duration_cast<std::chrono::minutes>(now - it->second->last_activity).count();
        
        if (idle_time > 5 || !it->second->is_active) { // 5 minutes idle
            std::cout << "🧹 Cleaning up inactive Whisper session: " << it->first << std::endl;
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void WhisperService::on_transcription_ready(const std::string& session_id, const std::string& text) {
    std::cout << "📝 Transcription ready for session " << session_id << ": \"" << text << "\"" << std::endl;
    
    // Update database if available
    if (database_) {
        database_->update_session_whisper(session_id, text);
    }
    
    // Update session context
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second->conversation_context += text + " ";
    }
}

WhisperService::ServiceStatus WhisperService::get_status() {
    ServiceStatus status;
    status.is_running = running_.load();
    status.model_loaded = (whisper_ctx_ != nullptr);
    status.model_name = current_model_name_;
    status.model_language = default_language_;
    status.total_processed_audio = total_processed_audio_.load();
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        status.active_sessions = sessions_.size();
    }
    
    return status;
}

// WhisperServiceSTT Implementation
WhisperServiceSTT::WhisperServiceSTT(WhisperService* service) 
    : whisper_service_(service) {}

std::string WhisperServiceSTT::transcribe(const std::vector<float>& audio_samples) {
    if (!whisper_service_ || current_session_id_.empty()) return "";
    
    // This is handled by the service's internal transcription
    // The audio processor will call this, but actual transcription
    // happens in the service with proper session context
    return "";
}

bool WhisperServiceSTT::is_ready() {
    return whisper_service_ && whisper_service_->is_model_loaded();
}
