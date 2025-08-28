#include "whisper-service.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <sys/sysctl.h>
#include <mach/mach.h>

namespace fs = std::filesystem;

WhisperService::WhisperService()
    : whisper_ctx_(nullptr), running_(false), loading_(false), database_(nullptr),
      default_language_("en"), models_directory_("./models"), last_chosen_model_(""),
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
    
    // Audio processing removed - handled by external AudioProcessorService
    
    // Clear all sessions
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }
    
    std::cout << "✅ Whisper Service stopped" << std::endl;
}

bool WhisperService::load_model(const std::string& model_path, const std::string& model_name) {
    std::lock_guard<std::mutex> lock(whisper_mutex_);

    loading_.store(true);
    std::cout << "🎤 Loading Whisper model: " << model_path << std::endl;

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
        loading_.store(false);
        std::cout << "❌ Failed to load Whisper model: " << model_path << std::endl;
        return false;
    }
    
    current_model_path_ = model_path;
    current_model_name_ = model_name.empty() ? fs::path(model_path).filename().string() : model_name;
    
    // WhisperService is now a pure transcription service
    // Audio processing (chunking, VAD) is handled by AudioProcessorService
    
    loading_.store(false);
    std::cout << "✅ Whisper model loaded successfully: " << current_model_name_ << std::endl;
    return true;
}

void WhisperService::unload_model() {
    std::lock_guard<std::mutex> lock(whisper_mutex_);
    
    // Audio processing removed - handled by external AudioProcessorService
    
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

std::string WhisperService::transcribe_chunk(const std::string& session_id, const std::vector<float>& audio_chunk) {
    if (!whisper_ctx_ || audio_chunk.empty()) return "";

    // Validate chunk size (Whisper expects 16kHz, 1-30 seconds)
    const size_t min_samples = 16000;      // 1 second at 16kHz
    const size_t max_samples = 480000;     // 30 seconds at 16kHz

    if (audio_chunk.size() < min_samples || audio_chunk.size() > max_samples) {
        std::cout << "⚠️  Invalid chunk size: " << audio_chunk.size()
                  << " samples (expected " << min_samples << "-" << max_samples << ")" << std::endl;
        return "";
    }

    // Verify session exists in database
    if (!database_) {
        std::cout << "❌ No database connection for session: " << session_id << std::endl;
        return "";
    }

    CallSession db_session = database_->get_session(session_id);
    if (db_session.session_id.empty()) {
        std::cout << "❌ Session not found in database: " << session_id << std::endl;
        return "";
    }

    // Get or create session state for this database session
    struct whisper_state* session_state = nullptr;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            session_state = it->second->state;
            it->second->last_activity = std::chrono::steady_clock::now();
        } else {
            // Create new session state for existing database session
            if (init_session(session_id)) {
                auto new_it = sessions_.find(session_id);
                if (new_it != sessions_.end()) {
                    session_state = new_it->second->state;
                }
            }
        }
    }

    if (!session_state) {
        std::cout << "❌ Failed to get session state for: " << session_id << std::endl;
        return "";
    }

    // Transcribe with session isolation
    std::lock_guard<std::mutex> lock(whisper_mutex_);

    // Configure for phone calls with sentence separation
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.translate = false;
    wparams.no_context = false;        // Enable context for better sentences
    wparams.single_segment = false;    // Allow multiple segments
    wparams.max_tokens = 64;           // Allow complete sentences
    wparams.language = default_language_.c_str();
    wparams.n_threads = 4;

    // Enable sentence-level segmentation for better LLaMA input
    wparams.split_on_word = true;
    wparams.max_len = 1;               // Split into sentences

    // Process with session state
    if (whisper_full_with_state(whisper_ctx_, session_state, wparams,
                                audio_chunk.data(), audio_chunk.size()) != 0) {
        return "";
    }

    // Extract transcription with sentence separation
    std::string result;
    const int n_segments = whisper_full_n_segments_from_state(session_state);

    for (int i = 0; i < n_segments; i++) {
        const char* text = whisper_full_get_segment_text_from_state(session_state, i);
        if (text && strlen(text) > 0) {
            std::string segment_text = text;

            // Clean up whitespace
            if (!segment_text.empty() && segment_text[0] == ' ') {
                segment_text = segment_text.substr(1);
            }

            if (!segment_text.empty()) {
                if (!result.empty()) result += " ";
                result += segment_text;

                // Add sentence boundary for LLaMA
                if (segment_text.back() == '.' || segment_text.back() == '!' || segment_text.back() == '?') {
                    result += "\n";
                }
            }
        }
    }

    total_processed_audio_.fetch_add(audio_chunk.size());

    // Accumulate transcription in database session
    if (database_ && !result.empty()) {
        // Get current whisper_data from database
        CallSession current_session = database_->get_session(session_id);
        std::string accumulated_text = current_session.whisper_data;

        // Append new transcription
        if (!accumulated_text.empty()) {
            accumulated_text += " ";
        }
        accumulated_text += result;

        // Update database with accumulated text
        database_->update_session_whisper(session_id, accumulated_text);

        std::cout << "📝 Accumulated transcription for session " << session_id
                  << ": \"" << result << "\"" << std::endl;
        std::cout << "💾 Total whisper_data length: " << accumulated_text.length() << " chars" << std::endl;
    }

    return result;
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

    // Accumulate text in database session (same logic as transcribe_chunk)
    if (database_ && !text.empty()) {
        // Get current whisper_data from database
        CallSession current_session = database_->get_session(session_id);
        std::string accumulated_text = current_session.whisper_data;

        // Append new transcription
        if (!accumulated_text.empty()) {
            accumulated_text += " ";
        }
        accumulated_text += text;

        // Update database with accumulated text
        database_->update_session_whisper(session_id, accumulated_text);

        std::cout << "💾 Updated accumulated whisper_data for session " << session_id
                  << " (total: " << accumulated_text.length() << " chars)" << std::endl;
    }

    // Update internal session context for Whisper state continuity
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
    status.is_loading = loading_.load();
    status.model_name = current_model_name_;
    status.model_language = default_language_;
    status.total_processed_audio = total_processed_audio_.load();
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        status.active_sessions = sessions_.size();
    }
    
    // Add memory information
    status.available_memory_mb = get_available_memory_mb();
    if (!current_model_path_.empty()) {
        status.required_memory_mb = estimate_model_memory_mb(current_model_path_);
        status.memory_sufficient = status.available_memory_mb >= status.required_memory_mb;

        if (status.memory_sufficient) {
            status.memory_status = "Sufficient memory available";
        } else {
            status.memory_status = "Insufficient memory for current model";
        }
    } else {
        status.required_memory_mb = 0;
        status.memory_sufficient = true;
        status.memory_status = "No model loaded";
    }

    return status;
}

// Memory management methods
size_t WhisperService::get_available_memory_mb() const {
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_stat) / sizeof(natural_t);

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) != KERN_SUCCESS) {
        return 0;
    }

    // Get page size
    vm_size_t page_size;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) {
        page_size = 4096; // Default page size
    }

    // Calculate free memory in MB
    uint64_t free_memory = (vm_stat.free_count + vm_stat.inactive_count) * page_size;
    return free_memory / (1024 * 1024);
}

size_t WhisperService::estimate_model_memory_mb(const std::string& model_path) const {
    if (!fs::exists(model_path)) {
        return 0;
    }

    // Get file size
    size_t file_size = fs::file_size(model_path);
    size_t file_size_mb = file_size / (1024 * 1024);

    // Estimate memory usage based on model type and size
    // Whisper models typically need 2-3x their file size in memory
    // Plus additional buffers for processing
    size_t estimated_memory = file_size_mb * 3 + 500; // 3x file size + 500MB buffer

    // Adjust based on model name patterns
    std::string filename = fs::path(model_path).filename().string();
    if (filename.find("tiny") != std::string::npos) {
        estimated_memory = file_size_mb * 2 + 200; // Smaller models are more efficient
    } else if (filename.find("large") != std::string::npos) {
        estimated_memory = file_size_mb * 3 + 800; // Large models need more overhead
    } else if (filename.find("q5_0") != std::string::npos || filename.find("q4_0") != std::string::npos) {
        estimated_memory = file_size_mb * 2 + 300; // Quantized models are more efficient
    }

    return estimated_memory;
}

bool WhisperService::check_memory_sufficient(const std::string& model_path) const {
    size_t available = get_available_memory_mb();
    size_t required = estimate_model_memory_mb(model_path);

    std::cout << "🧠 Memory check for " << fs::path(model_path).filename().string() << ":" << std::endl;
    std::cout << "   Available: " << available << " MB" << std::endl;
    std::cout << "   Required:  " << required << " MB" << std::endl;

    return available >= required;
}

std::string WhisperService::find_best_model_for_memory() const {
    std::vector<std::string> model_candidates = {
        "models/ggml-tiny.en.bin",
        "models/ggml-tiny.bin",
        "models/ggml-base.en.bin",
        "models/ggml-base.bin",
        "models/ggml-small.en.bin",
        "models/ggml-small.bin",
        "models/ggml-medium.en.bin",
        "models/ggml-medium.bin",
        "models/ggml-large-v3-q5_0.bin",
        "models/ggml-large-v3.bin"
    };

    // Try models from smallest to largest
    for (const auto& model : model_candidates) {
        if (fs::exists(model) && check_memory_sufficient(model)) {
            std::cout << "✅ Found suitable model: " << fs::path(model).filename().string() << std::endl;
            return model;
        }
    }

    std::cout << "❌ No suitable model found for available memory" << std::endl;
    return "";
}

bool WhisperService::load_model_with_memory_check(const std::string& model_path, const std::string& model_name) {
    std::cout << "🧠 Loading model with memory check: " << model_path << std::endl;

    // Check if specific model has enough memory
    if (check_memory_sufficient(model_path)) {
        if (load_model(model_path, model_name)) {
            last_chosen_model_ = model_path;
            return true;
        }
    }

    // If not enough memory or loading failed, find best alternative
    std::cout << "⚠️  Insufficient memory or failed to load requested model" << std::endl;
    std::cout << "🔍 Searching for alternative model..." << std::endl;

    std::string best_model = find_best_model_for_memory();
    if (!best_model.empty() && best_model != model_path) {
        std::string best_name = fs::path(best_model).filename().string();
        best_name = best_name.substr(0, best_name.find_last_of("."));

        std::cout << "🔄 Attempting to load alternative: " << best_name << std::endl;
        if (load_model(best_model, best_name)) {
            last_chosen_model_ = best_model;
            return true;
        }
    }

    std::cout << "❌ No suitable model could be loaded" << std::endl;
    return false;
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
