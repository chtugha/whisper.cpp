#pragma once

#include "whisper.h"
#include "llama.h"
#include "tts-engine.h"
#include "common-whisper.h"
#include "database.h"
#include "jitter-buffer.h"
#include "rtp-packet.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <chrono>
#include <condition_variable>
#include <unordered_map>

// command-line parameters
struct whisper_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t voice_ms   = 10000;
    int32_t capture_id = -1;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;
    int32_t n_gpu_layers = 999;
    int32_t seed = 0;
    int32_t top_k = 5;
    int32_t min_keep = 1;
    float top_p = 0.80f;
    float min_p = 0.01f;
    float temp  = 0.30f;

    float vad_thold  = 0.6f;
    float freq_thold = 100.0f;

    bool translate      = false;
    bool print_special  = false;
    bool print_energy   = false;
    bool no_timestamps  = true;
    bool verbose_prompt = false;
    bool use_gpu        = true;
    bool flash_attn     = false;

    int32_t http_port = 8081;            // HTTP API server port

    std::string person      = "Georgi";
    std::string bot_name    = "LLaMA";
    std::string wake_cmd    = "";
    std::string heard_ok    = "";
    std::string language    = "en";
    std::string model_wsp   = "models/ggml-base.en.bin";
    std::string model_llama = "models/ggml-llama-7B.bin";
    std::string speak       = "./examples/talk-llama/speak";
    std::string speak_file  = "./examples/talk-llama/to_speak.txt";
    std::string prompt      = "";
    std::string fname_out;
    std::string path_session = "";       // path to file for saving/loading model eval state
};

// Helper functions for LLaMA tokenization (defined in talk-llama.cpp)
std::vector<llama_token> llama_tokenize(struct llama_context * ctx, const std::string & text, bool add_bos);
std::string llama_token_to_piece(const struct llama_context * ctx, llama_token token);

// SIP client configuration
struct SipClientConfig {
    std::string client_id;
    std::string username;
    std::string password;
    std::string server_ip;
    int server_port = 5060;
    std::string display_name;
    bool auto_answer = true;
    int expires = 3600; // Registration expiry in seconds
    
    // AI configuration
    std::string ai_persona = "helpful assistant";
    std::string greeting = "Hello! How can I help you today?";
    bool use_tts = true;
    std::string tts_voice = "default";
};

// Audio buffer for RTP processing
struct AudioBuffer {
    std::vector<float> samples;
    std::mutex mutex;
    std::condition_variable cv;
    bool has_data = false;
    
    void add_samples(const std::vector<float>& new_samples);
    bool get_samples(std::vector<float>& output, int timeout_ms = 1000);
    void clear();
};

// SIP call session
struct SipCallSession {
    std::string call_id;
    std::string caller_number;
    std::string caller_name;
    
    // Whisper state for this call
    struct whisper_state* whisper_state;
    
    // LLaMA sequence ID for this call's conversation
    llama_seq_id llama_seq_id;
    
    // Call-specific conversation state
    std::vector<llama_token> conversation_tokens;
    std::string conversation_history;
    int n_past;
    bool need_to_save_session;
    
    // Audio processing with jitter buffers
    AudioBuffer incoming_audio;
    AudioBuffer outgoing_audio;

    // Jitter buffers for stable RTP processing
    TimedRTPBuffer incoming_rtp_buffer;  // Incoming RTP packets
    RTPPacketBuffer outgoing_rtp_buffer; // Outgoing RTP packets

    // RFC 3550 compliant RTP session management
    std::unique_ptr<RTPSession> rtp_session;

    // Internal session data (NEVER transmitted)
    InternalSessionData internal_data;
    
    // Call state
    std::atomic<bool> is_active{false};
    std::atomic<bool> is_processing{false};
    std::chrono::steady_clock::time_point call_start_time;
    std::chrono::steady_clock::time_point last_activity;
    
    SipCallSession(const std::string& id, const std::string& caller,
                   struct whisper_state* w_state, ::llama_seq_id seq_id)
        : call_id(id), caller_number(caller), whisper_state(w_state), 
          llama_seq_id(seq_id), n_past(0), need_to_save_session(false),
          call_start_time(std::chrono::steady_clock::now()),
          last_activity(std::chrono::steady_clock::now()) {}
    
    ~SipCallSession() {
        if (whisper_state) {
            whisper_free_state(whisper_state);
        }
    }
};

// Individual SIP client (represents one phone number/extension)
class SipClient {
public:
    SipClient(const SipClientConfig& config);
    ~SipClient();
    
    // SIP operations
    bool start();
    bool stop();
    bool is_registered() const { return is_registered_.load(); }
    bool is_running() const { return is_running_.load(); }
    
    // Configuration
    const SipClientConfig& get_config() const { return config_; }
    void update_config(const SipClientConfig& config);
    
    // Call management
    std::vector<std::string> get_active_calls() const;
    bool hangup_call(const std::string& call_id);
    bool answer_call(const std::string& call_id);
    
    // Statistics
    struct Stats {
        int total_calls = 0;
        int active_calls = 0;
        std::chrono::steady_clock::time_point last_call_time;
        std::chrono::seconds total_call_duration{0};
    };
    Stats get_stats() const;

private:
    SipClientConfig config_;
    
    // SIP state
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_registered_{false};
    
    // Call management
    std::unordered_map<std::string, std::shared_ptr<SipCallSession>> active_calls_;
    mutable std::mutex calls_mutex_;
    
    // Sequence ID management for LLaMA sessions
    std::atomic<llama_seq_id> next_seq_id_{1};
    
    // Threading
    std::thread sip_thread_;
    std::thread audio_thread_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    // Internal methods
    void sip_worker();
    void audio_worker();
    void handle_incoming_call(const std::string& call_id, const std::string& caller);
    void handle_call_ended(const std::string& call_id);
    void process_audio_for_call(std::shared_ptr<SipCallSession> session);
    
    // Audio processing
    void process_incoming_audio(std::shared_ptr<SipCallSession> session,
                               const std::vector<float>& audio_data);
    std::vector<float> generate_tts_audio(const std::string& text);

    // RTP jitter buffer processing
    void handle_incoming_rtp(std::shared_ptr<SipCallSession> session,
                            const std::vector<uint8_t>& rtp_data,
                            uint32_t sequence_number, uint32_t timestamp);
    void handle_outgoing_audio_with_jitter_buffer(std::shared_ptr<SipCallSession> session,
                                                  const std::vector<uint8_t>& audio_data);

    // RFC 3550 compliant RTP transmission
    void send_rtp_packet_to_network(std::shared_ptr<SipCallSession> session,
                                   const std::vector<uint8_t>& rtp_packet);
    void process_incoming_rtp_packet(std::shared_ptr<SipCallSession> session,
                                   const std::vector<uint8_t>& raw_packet);
};

// SIP client manager - manages multiple SIP clients
class SipClientManager {
public:
    SipClientManager();
    ~SipClientManager();
    
    // Initialize with shared AI resources
    bool init(struct whisper_context* whisper_ctx, 
              struct llama_context* llama_ctx,
              const whisper_params& params);
    
    // Client management
    bool add_client(const SipClientConfig& config);
    bool remove_client(const std::string& client_id);
    bool update_client(const std::string& client_id, const SipClientConfig& config);
    
    // Operations
    bool start_all_clients();
    bool stop_all_clients();
    bool start_client(const std::string& client_id);
    bool stop_client(const std::string& client_id);
    
    // Information
    std::vector<SipClientConfig> get_all_clients() const;
    std::vector<std::string> get_active_clients() const;
    SipClient::Stats get_client_stats(const std::string& client_id) const;
    
    // AI processing (shared across all clients)
    std::string process_with_llama(llama_seq_id seq_id, const std::string& input_text);

    // TTS processing (shared across all clients)
    std::vector<float> text_to_speech(const std::string& text);

private:
    // Shared AI resources
    struct whisper_context* whisper_ctx_;
    struct llama_context* llama_ctx_;
    whisper_params params_;
    
    // Shared LLaMA resources (thread-safe with sequence IDs)
    llama_batch shared_batch_;
    llama_sampler* shared_sampler_;
    std::mutex llama_mutex_;

    // Shared TTS engine
    std::unique_ptr<TtsManager> tts_manager_;
    std::mutex tts_mutex_;

    // Database for session management
    Database database_;
    
    // Client management
    std::unordered_map<std::string, std::unique_ptr<SipClient>> clients_;
    mutable std::mutex clients_mutex_;
    
    // Initialization state
    bool is_initialized_ = false;
    
    // Internal methods
    void cleanup_llama_resources();
};

// Utility functions
std::vector<float> convert_rtp_to_float(const uint8_t* rtp_data, size_t length);
std::vector<uint8_t> convert_float_to_rtp(const std::vector<float>& audio_data);
bool is_valid_sip_config(const SipClientConfig& config);
