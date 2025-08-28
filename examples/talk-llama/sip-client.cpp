#include "sip-client.h"
#include "common.h"
#include "common-whisper.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

// AudioBuffer implementation
void AudioBuffer::add_samples(const std::vector<float>& new_samples) {
    std::lock_guard<std::mutex> lock(mutex);
    samples.insert(samples.end(), new_samples.begin(), new_samples.end());
    has_data = true;
    cv.notify_one();
}

bool AudioBuffer::get_samples(std::vector<float>& output, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex);
    
    if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return has_data; })) {
        return false; // Timeout
    }
    
    if (samples.empty()) {
        has_data = false;
        return false;
    }
    
    // Get available samples (or up to 1 second worth)
    const size_t max_samples = 16000; // 1 second at 16kHz
    size_t samples_to_take = std::min(samples.size(), max_samples);
    
    output.assign(samples.begin(), samples.begin() + samples_to_take);
    samples.erase(samples.begin(), samples.begin() + samples_to_take);
    
    if (samples.empty()) {
        has_data = false;
    }
    
    return true;
}

void AudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    samples.clear();
    has_data = false;
}

// SipClient implementation
SipClient::SipClient(const SipClientConfig& config) : config_(config) {
    printf("Created SIP client: %s (%s@%s:%d)\n", 
           config_.client_id.c_str(), config_.username.c_str(), 
           config_.server_ip.c_str(), config_.server_port);
}

SipClient::~SipClient() {
    stop();
}

bool SipClient::start() {
    if (is_running_.load()) {
        return false;
    }
    
    printf("Starting SIP client: %s\n", config_.client_id.c_str());
    
    is_running_.store(true);
    
    // Start SIP worker thread
    sip_thread_ = std::thread(&SipClient::sip_worker, this);
    
    // Start audio processing thread
    audio_thread_ = std::thread(&SipClient::audio_worker, this);
    
    return true;
}

bool SipClient::stop() {
    if (!is_running_.load()) {
        return false;
    }
    
    printf("Stopping SIP client: %s\n", config_.client_id.c_str());
    
    is_running_.store(false);
    is_registered_.store(false);
    
    // End all active calls
    {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        for (auto& [call_id, session] : active_calls_) {
            session->is_active.store(false);
        }
        active_calls_.clear();
    }
    
    // Wait for threads to finish
    if (sip_thread_.joinable()) {
        sip_thread_.join();
    }
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
    
    return true;
}

void SipClient::update_config(const SipClientConfig& config) {
    bool was_running = is_running_.load();
    
    if (was_running) {
        stop();
    }
    
    config_ = config;
    
    if (was_running) {
        start();
    }
}

std::vector<std::string> SipClient::get_active_calls() const {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    std::vector<std::string> call_ids;
    
    for (const auto& [call_id, session] : active_calls_) {
        if (session->is_active.load()) {
            call_ids.push_back(call_id);
        }
    }
    
    return call_ids;
}

bool SipClient::hangup_call(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    
    auto it = active_calls_.find(call_id);
    if (it != active_calls_.end()) {
        it->second->is_active.store(false);
        printf("Hanging up call: %s\n", call_id.c_str());
        return true;
    }
    
    return false;
}

bool SipClient::answer_call(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    
    auto it = active_calls_.find(call_id);
    if (it != active_calls_.end()) {
        printf("Answering call: %s\n", call_id.c_str());
        
        // Send greeting via TTS
        if (config_.use_tts && !config_.greeting.empty()) {
            auto tts_audio = generate_tts_audio(config_.greeting);
            it->second->outgoing_audio.add_samples(tts_audio);
        }
        
        return true;
    }
    
    return false;
}

SipClient::Stats SipClient::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    Stats current_stats = stats_;
    
    // Update active calls count
    {
        std::lock_guard<std::mutex> calls_lock(calls_mutex_);
        current_stats.active_calls = 0;
        for (const auto& [call_id, session] : active_calls_) {
            if (session->is_active.load()) {
                current_stats.active_calls++;
            }
        }
    }
    
    return current_stats;
}

void SipClient::sip_worker() {
    printf("SIP worker started for client: %s\n", config_.client_id.c_str());
    
    // Simulate SIP registration
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    is_registered_.store(true);
    printf("SIP client registered: %s@%s\n", config_.username.c_str(), config_.server_ip.c_str());
    
    // Main SIP processing loop
    while (is_running_.load()) {
        // TODO: Implement actual SIP protocol handling
        // For now, simulate incoming calls for testing
        
        if (is_registered_.load()) {
            // Simulate an incoming call every 30 seconds for testing
            static auto last_test_call = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_test_call).count() > 30) {
                // Simulate incoming call
                std::string call_id = "test_call_" + std::to_string(std::time(nullptr));
                std::string caller = "+1234567890";
                
                handle_incoming_call(call_id, caller);
                last_test_call = now;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    printf("SIP worker stopped for client: %s\n", config_.client_id.c_str());
}

void SipClient::audio_worker() {
    printf("Audio worker started for client: %s\n", config_.client_id.c_str());
    
    while (is_running_.load()) {
        // Process audio for all active calls
        std::vector<std::shared_ptr<SipCallSession>> active_sessions;
        
        {
            std::lock_guard<std::mutex> lock(calls_mutex_);
            for (auto& [call_id, session] : active_calls_) {
                if (session->is_active.load()) {
                    active_sessions.push_back(session);
                }
            }
        }
        
        for (auto& session : active_sessions) {
            process_audio_for_call(session);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    printf("Audio worker stopped for client: %s\n", config_.client_id.c_str());
}

void SipClient::handle_incoming_call(const std::string& call_id, const std::string& caller) {
    printf("Incoming call: %s from %s on client %s\n", 
           call_id.c_str(), caller.c_str(), config_.client_id.c_str());
    
    // Create whisper state for this call
    // Note: This would need to be provided by the SipClientManager
    struct whisper_state* whisper_state = nullptr; // TODO: Get from manager
    
    // Get unique sequence ID for this call
    llama_seq_id seq_id = next_seq_id_.fetch_add(1);
    
    // Create call session
    auto session = std::make_shared<SipCallSession>(call_id, caller, whisper_state, seq_id);
    session->is_active.store(true);
    
    {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        active_calls_[call_id] = session;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_calls++;
        stats_.last_call_time = std::chrono::steady_clock::now();
    }
    
    // Auto-answer if configured
    if (config_.auto_answer) {
        answer_call(call_id);
    }
}

void SipClient::handle_call_ended(const std::string& call_id) {
    printf("Call ended: %s\n", call_id.c_str());
    
    std::lock_guard<std::mutex> lock(calls_mutex_);
    auto it = active_calls_.find(call_id);
    if (it != active_calls_.end()) {
        auto session = it->second;
        
        // Update statistics
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            auto call_duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - session->call_start_time);
            stats_.total_call_duration += call_duration;
        }
        
        active_calls_.erase(it);
    }
}

void SipClient::process_audio_for_call(std::shared_ptr<SipCallSession> session) {
    if (session->is_processing.load()) {
        return; // Already processing
    }
    
    // Get incoming audio
    std::vector<float> audio_data;
    if (session->incoming_audio.get_samples(audio_data, 100)) {
        session->is_processing.store(true);
        
        // Process in separate thread to avoid blocking
        std::thread([this, session, audio_data]() {
            process_incoming_audio(session, audio_data);
            session->is_processing.store(false);
        }).detach();
    }
}

void SipClient::process_incoming_audio(std::shared_ptr<SipCallSession> session, 
                                      const std::vector<float>& audio_data) {
    // TODO: Implement actual audio processing
    // This would integrate with the whisper/llama pipeline
    printf("Processing audio for call: %s (%zu samples)\n", 
           session->call_id.c_str(), audio_data.size());
    
    // Simulate processing delay
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Generate a simple response
    std::string response = "I heard you speak. This is a test response.";
    
    if (config_.use_tts) {
        auto tts_audio = generate_tts_audio(response);
        session->outgoing_audio.add_samples(tts_audio);
    }
}

std::vector<float> SipClient::generate_tts_audio(const std::string& text) {
    // This would need access to the shared TTS manager
    // For now, generate simple placeholder audio
    printf("TTS: %s\n", text.c_str());

    // Generate simple beep pattern for each word
    std::vector<float> audio;
    std::istringstream words(text);
    std::string word;

    while (words >> word) {
        // Generate a short beep for each word
        int beep_samples = 8000; // 0.5 seconds at 16kHz
        for (int i = 0; i < beep_samples; ++i) {
            float t = static_cast<float>(i) / 16000.0f;
            float beep = 0.2f * std::sin(2.0f * M_PI * 800.0f * t) * std::exp(-t * 5.0f);
            audio.push_back(beep);
        }

        // Add silence between words
        for (int i = 0; i < 4000; ++i) {
            audio.push_back(0.0f);
        }
    }

    return audio;
}

// SipClientManager implementation
SipClientManager::SipClientManager() : whisper_ctx_(nullptr), llama_ctx_(nullptr), shared_sampler_(nullptr) {
}

SipClientManager::~SipClientManager() {
    stop_all_clients();
    cleanup_llama_resources();
}

bool SipClientManager::init(struct whisper_context* whisper_ctx,
                           struct llama_context* llama_ctx,
                           const whisper_params& params) {
    whisper_ctx_ = whisper_ctx;
    llama_ctx_ = llama_ctx;
    params_ = params;

    // Initialize shared LLaMA resources only if LLaMA context is available
    if (llama_ctx_) {
        shared_batch_ = llama_batch_init(llama_n_ctx(llama_ctx_), 0, 1);

        // Initialize shared sampler
        auto sparams = llama_sampler_chain_default_params();
        shared_sampler_ = llama_sampler_chain_init(sparams);

        if (params_.temp > 0.0f) {
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_top_k(params_.top_k));
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_top_p(params_.top_p, params_.min_keep));
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_temp(params_.temp));
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_dist(params_.seed));
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_min_p(params_.min_p, params_.min_keep));
        } else {
            llama_sampler_chain_add(shared_sampler_, llama_sampler_init_greedy());
        }
    } else {
        // LLaMA not available - initialize with safe default values
        shared_batch_ = llama_batch_init(512, 0, 1);  // Use default context size
        shared_sampler_ = nullptr;
        printf("Warning: LLaMA model not available. AI responses will be disabled.\n");
    }

    // Initialize TTS engine
    tts_manager_ = std::make_unique<TtsManager>();
    auto engine_type = TtsEngineFactory::detect_best_engine();
    if (!tts_manager_->init(engine_type)) {
        printf("Warning: TTS initialization failed, using fallback\n");
        tts_manager_->init(TtsEngineFactory::EngineType::SIMPLE);
    }

    // Initialize database
    if (!database_.init()) {
        printf("Warning: Database initialization failed\n");
        return false;
    }

    is_initialized_ = true;
    printf("SipClientManager initialized with TTS and database\n");
    return true;
}

bool SipClientManager::add_client(const SipClientConfig& config) {
    if (!is_initialized_) {
        fprintf(stderr, "SipClientManager not initialized\n");
        return false;
    }

    if (!is_valid_sip_config(config)) {
        fprintf(stderr, "Invalid SIP client configuration\n");
        return false;
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);

    if (clients_.find(config.client_id) != clients_.end()) {
        fprintf(stderr, "SIP client already exists: %s\n", config.client_id.c_str());
        return false;
    }

    auto client = std::make_unique<SipClient>(config);
    clients_[config.client_id] = std::move(client);

    printf("Added SIP client: %s\n", config.client_id.c_str());
    return true;
}

bool SipClientManager::remove_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return false;
    }

    it->second->stop();
    clients_.erase(it);

    printf("Removed SIP client: %s\n", client_id.c_str());
    return true;
}

bool SipClientManager::update_client(const std::string& client_id, const SipClientConfig& config) {
    if (!is_valid_sip_config(config)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return false;
    }

    it->second->update_config(config);
    printf("Updated SIP client: %s\n", client_id.c_str());
    return true;
}

bool SipClientManager::start_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    bool all_started = true;
    for (auto& [client_id, client] : clients_) {
        if (!client->start()) {
            all_started = false;
            fprintf(stderr, "Failed to start SIP client: %s\n", client_id.c_str());
        }
    }

    printf("Started %zu SIP clients\n", clients_.size());
    return all_started;
}

bool SipClientManager::stop_all_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    for (auto& [client_id, client] : clients_) {
        client->stop();
    }

    printf("Stopped all SIP clients\n");
    return true;
}

bool SipClientManager::start_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return false;
    }

    return it->second->start();
}

bool SipClientManager::stop_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return false;
    }

    return it->second->stop();
}

std::vector<SipClientConfig> SipClientManager::get_all_clients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    std::vector<SipClientConfig> configs;
    for (const auto& [client_id, client] : clients_) {
        configs.push_back(client->get_config());
    }

    return configs;
}

std::vector<std::string> SipClientManager::get_active_clients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    std::vector<std::string> active_clients;
    for (const auto& [client_id, client] : clients_) {
        if (client->is_running() && client->is_registered()) {
            active_clients.push_back(client_id);
        }
    }

    return active_clients;
}

SipClient::Stats SipClientManager::get_client_stats(const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        return it->second->get_stats();
    }

    return SipClient::Stats{};
}

std::string SipClientManager::process_with_llama(llama_seq_id seq_id, const std::string& input_text) {
    std::lock_guard<std::mutex> lock(llama_mutex_);

    try {
        // Format input for this sequence's conversation
        std::string formatted_input = " " + input_text + "\n" + params_.bot_name + ":";

        // Tokenize the input using the helper function from talk-llama.cpp
        auto input_tokens = llama_tokenize(llama_ctx_, formatted_input, false);
        if (input_tokens.empty()) {
            return "Sorry, I couldn't process that input.";
        }

        // TODO: Track n_past per sequence - this is simplified for now
        int n_past = 0; // This should be tracked per sequence

        // Prepare batch for this sequence
        shared_batch_.n_tokens = input_tokens.size();
        for (size_t i = 0; i < input_tokens.size(); ++i) {
            shared_batch_.token[i] = input_tokens[i];
            shared_batch_.pos[i] = n_past + i;
            shared_batch_.n_seq_id[i] = 1;
            shared_batch_.seq_id[i][0] = seq_id; // Use specific sequence ID
            shared_batch_.logits[i] = (i == input_tokens.size() - 1);
        }

        // Process the input
        if (llama_decode(llama_ctx_, shared_batch_) != 0) {
            return "Error processing input.";
        }

        n_past += input_tokens.size();

        // Generate response
        std::string response;
        const int max_tokens = 50; // Limit response length

        for (int i = 0; i < max_tokens; ++i) {
            // Sample next token for this sequence
            const llama_token token = llama_sampler_sample(shared_sampler_, llama_ctx_, -1);

            // Check for end of sequence
            if (token == llama_vocab_eos(llama_model_get_vocab(llama_get_model(llama_ctx_)))) {
                break;
            }

            // Convert token to text
            std::string token_text = llama_token_to_piece(llama_ctx_, token);
            response += token_text;

            // Check for end of response
            if (token_text.find('\n') != std::string::npos) {
                break;
            }

            // Prepare next batch with single token for this sequence
            shared_batch_.n_tokens = 1;
            shared_batch_.token[0] = token;
            shared_batch_.pos[0] = n_past;
            shared_batch_.n_seq_id[0] = 1;
            shared_batch_.seq_id[0][0] = seq_id;
            shared_batch_.logits[0] = true;

            if (llama_decode(llama_ctx_, shared_batch_) != 0) {
                break;
            }

            n_past++;
        }

        // Clean up response
        size_t newline_pos = response.find('\n');
        if (newline_pos != std::string::npos) {
            response = response.substr(0, newline_pos);
        }

        // Trim whitespace
        response = ::trim(response);

        return response.empty() ? "I'm not sure how to respond to that." : response;

    } catch (const std::exception& e) {
        fprintf(stderr, "Error in LLaMA processing for sequence %d: %s\n", seq_id, e.what());
        return "Sorry, I encountered an error processing your request.";
    }
}

std::vector<float> SipClientManager::text_to_speech(const std::string& text) {
    if (!tts_manager_ || !tts_manager_->is_ready()) {
        return {};
    }

    std::lock_guard<std::mutex> lock(tts_mutex_);
    return tts_manager_->text_to_speech(text);
}

void SipClientManager::cleanup_llama_resources() {
    if (shared_sampler_) {
        llama_sampler_free(shared_sampler_);
        shared_sampler_ = nullptr;
    }

    if (is_initialized_) {
        llama_batch_free(shared_batch_);
        is_initialized_ = false;
    }

    // Cleanup TTS
    tts_manager_.reset();
}

// Utility functions
std::vector<float> convert_rtp_to_float(const uint8_t* rtp_data, size_t length) {
    std::vector<float> result;

    // Assume 16-bit PCM for now
    if (length % 2 != 0) {
        return result; // Invalid data
    }

    result.reserve(length / 2);
    for (size_t i = 0; i < length; i += 2) {
        int16_t sample = static_cast<int16_t>(rtp_data[i] | (rtp_data[i + 1] << 8));
        result.push_back(static_cast<float>(sample) / 32768.0f);
    }

    return result;
}

std::vector<uint8_t> convert_float_to_rtp(const std::vector<float>& audio_data) {
    std::vector<uint8_t> result;
    result.reserve(audio_data.size() * 2);

    for (float sample : audio_data) {
        // Clamp to [-1.0, 1.0] and convert to 16-bit
        sample = std::max(-1.0f, std::min(1.0f, sample));
        int16_t int_sample = static_cast<int16_t>(sample * 32767.0f);

        result.push_back(static_cast<uint8_t>(int_sample & 0xFF));
        result.push_back(static_cast<uint8_t>((int_sample >> 8) & 0xFF));
    }

    return result;
}

bool is_valid_sip_config(const SipClientConfig& config) {
    if (config.client_id.empty() || config.username.empty() ||
        config.server_ip.empty() || config.server_port <= 0) {
        return false;
    }

    // Basic IP address validation
    std::regex ip_regex(R"(^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$)");
    if (!std::regex_match(config.server_ip, ip_regex)) {
        return false;
    }

    return true;
}
