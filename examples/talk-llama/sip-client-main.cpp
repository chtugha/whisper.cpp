// Standalone SIP Client Module
// Handles incoming calls, creates database sessions, manages audio streams
// Manages SIP line connections and status updates

#include "database.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <openssl/evp.h>


// Simple SIP client configuration
struct SipConfig {
    std::string extension;
    std::string username;
    std::string password;
    std::string server_ip;
    int server_port;
    std::string display_name;
    bool auto_answer;
};

// Call session state (local to SIP client)
struct SipCallSession {
    std::string session_id;
    int caller_id;
    std::string phone_number;
    std::string status; // "ringing", "active", "ended"
    std::chrono::system_clock::time_point start_time;
    int internal_port; // Unique port for this call (10000 + caller_id)
};

// Helper function to calculate MD5 hash for SIP digest authentication
std::string calculate_md5(const std::string& input) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    if (EVP_DigestUpdate(ctx, input.c_str(), input.length()) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream ss;
    for (unsigned int i = 0; i < digest_len; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];
    }
    return ss.str();
}

class SimpleSipClient {
public:
    SimpleSipClient();
    ~SimpleSipClient();

    bool init(Database* database, int specific_line_id = -1);
    bool start();
    void stop();

    // Call handling
    void handle_incoming_call(const std::string& caller_number);
    void end_call(const std::string& session_id);

    // Audio streaming (placeholder for now)
    void stream_audio_to_whisper(const std::string& session_id, const std::vector<uint8_t>& audio_data);
    void stream_audio_from_piper(const std::string& session_id, const std::vector<uint8_t>& audio_data);

private:
    Database* database_;
    bool running_;
    int specific_line_id_; // -1 means process all enabled lines
    std::thread sip_thread_;
    std::thread connection_monitor_thread_;

    // Active calls
    std::map<std::string, SipCallSession> active_calls_;
    std::mutex calls_mutex_;

    // SIP line management
    std::vector<SipLineConfig> sip_lines_;
    std::mutex sip_lines_mutex_;

    // Main loops
    void sip_management_loop();
    void connection_monitor_loop();

    // SIP line connection management
    void load_sip_lines_from_database();
    bool test_sip_connection(const SipLineConfig& line);
    void update_line_status(int line_id, const std::string& status);

    // SIP Digest Authentication
    std::string create_digest_response(const std::string& username, const std::string& password,
                                     const std::string& realm, const std::string& nonce,
                                     const std::string& method, const std::string& uri);
    std::string create_digest_response_with_qop(const std::string& username, const std::string& password,
                                              const std::string& realm, const std::string& nonce,
                                              const std::string& method, const std::string& uri,
                                              const std::string& qop, const std::string& nc, const std::string& cnonce);
    bool parse_www_authenticate(const std::string& auth_header, std::string& realm, std::string& nonce);
    bool send_authenticated_register(const SipLineConfig& line, const std::string& realm, const std::string& nonce, bool supports_qop = false, const std::string& call_id = "");


    // Call simulation (for testing)
    void simulate_incoming_call();

    // Port management - use caller database ID directly
    int get_caller_port(int caller_id) const {
        // Each caller gets unique port: 10000 + caller_id
        // caller_id 1 -> port 10001, caller_id 2 -> port 10002, etc.
        return 10000 + caller_id;
    }
};

// Global variables for signal handling
static bool g_running = true;
static std::unique_ptr<SimpleSipClient> g_sip_client;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    if (g_sip_client) {
        g_sip_client->stop();
    }
}

void print_usage() {
    std::cout << "Usage: whisper-sip-client [options]\n"
              << "Options:\n"
              << "  --db PATH          Database file path (default: whisper_talk.db)\n"
              << "  --simulate         Run in simulation mode (no real SIP)\n"
              << "  --help             Show this help message\n"
              << "\nNote: SIP line configurations are read from the database.\n"
              << "      Use the web interface to configure SIP lines.\n"
              << "      Internal ports are auto-assigned as 10000 + caller_id\n";
}

int main(int argc, char** argv) {
    std::string db_path = "whisper_talk.db";
    bool simulate = false;
    int specific_line_id = -1; // -1 means process all enabled lines

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--simulate") {
            simulate = true;
        } else if (arg == "--line-id" && i + 1 < argc) {
            specific_line_id = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage();
            return 1;
        }
    }

    std::cout << "📞 Starting Whisper Talk LLaMA SIP Client..." << std::endl;
    std::cout << "   Mode: " << (simulate ? "Simulation" : "Database-driven SIP") << std::endl;
    std::cout << "   Database: " << db_path << std::endl;
    if (specific_line_id != -1) {
        std::cout << "   Target Line ID: " << specific_line_id << " (single line mode)" << std::endl;
    } else {
        std::cout << "   Target: All enabled lines" << std::endl;
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize database
    Database database;
    if (!database.init(db_path)) {
        std::cerr << "❌ Failed to initialize database!" << std::endl;
        return 1;
    }
    std::cout << "✅ Database initialized" << std::endl;
    
    // Create SIP client
    g_sip_client = std::make_unique<SimpleSipClient>();
    if (!g_sip_client->init(&database, specific_line_id)) {
        std::cerr << "❌ Failed to initialize SIP client!" << std::endl;
        return 1;
    }
    std::cout << "✅ SIP client initialized" << std::endl;
    
    // Start SIP client
    if (!g_sip_client->start()) {
        std::cerr << "❌ Failed to start SIP client!" << std::endl;
        return 1;
    }
    std::cout << "🚀 SIP client started and ready for calls" << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    
    // Main loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "🛑 Shutting down SIP client..." << std::endl;
    g_sip_client->stop();
    database.close();
    std::cout << "✅ SIP client stopped cleanly" << std::endl;
    
    return 0;
}

// SimpleSipClient implementation
SimpleSipClient::SimpleSipClient() : database_(nullptr), running_(false) {}

SimpleSipClient::~SimpleSipClient() {
    stop();
}

bool SimpleSipClient::init(Database* database, int specific_line_id) {
    database_ = database;
    specific_line_id_ = specific_line_id;

    if (!database_) {
        std::cerr << "❌ Database is required for SIP client" << std::endl;
        return false;
    }

    // Load SIP lines from database
    load_sip_lines_from_database();
    return true;
}

bool SimpleSipClient::start() {
    if (running_) return false;

    running_ = true;
    sip_thread_ = std::thread(&SimpleSipClient::sip_management_loop, this);
    connection_monitor_thread_ = std::thread(&SimpleSipClient::connection_monitor_loop, this);
    return true;
}

void SimpleSipClient::stop() {
    running_ = false;
    if (sip_thread_.joinable()) {
        sip_thread_.join();
    }
    if (connection_monitor_thread_.joinable()) {
        connection_monitor_thread_.join();
    }
}

void SimpleSipClient::handle_incoming_call(const std::string& caller_number) {
    std::cout << "📞 Incoming call from: " << caller_number << std::endl;

    // Step 1: Get or create caller in database
    int caller_id = database_->get_or_create_caller(caller_number);
    if (caller_id < 0) {
        std::cerr << "❌ Failed to create caller record" << std::endl;
        return;
    }

    // Step 2: Create new session
    std::string session_id = database_->create_session(caller_id, caller_number);
    if (session_id.empty()) {
        std::cerr << "❌ Failed to create session" << std::endl;
        return;
    }

    // Step 3: Assign unique port for this caller
    int caller_port = get_caller_port(caller_id);
    std::cout << "✅ Created session: " << session_id << " for caller_id: " << caller_id << " (port: " << caller_port << ")" << std::endl;

    // Step 4: Store call session
    SipCallSession call_session;
    call_session.session_id = session_id;
    call_session.caller_id = caller_id;
    call_session.phone_number = caller_number;
    call_session.status = "active";
    call_session.start_time = std::chrono::system_clock::now();
    call_session.internal_port = caller_port;

    {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        active_calls_[session_id] = call_session;
    }

    std::cout << "📱 Call answered automatically. Session active on port " << caller_port << std::endl;
    std::cout << "🎤 Ready to receive audio for session: " << session_id << " (port: " << caller_port << ")" << std::endl;

    // Simulate some audio processing
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Simulate audio data to Whisper
    std::vector<uint8_t> dummy_audio = {0x01, 0x02, 0x03, 0x04}; // Placeholder
    stream_audio_to_whisper(session_id, dummy_audio);

    // Simulate call duration
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // End call
    end_call(session_id);
}

void SimpleSipClient::end_call(const std::string& session_id) {
    std::cout << "📞 Ending call for session: " << session_id << std::endl;

    {
        std::lock_guard<std::mutex> lock(calls_mutex_);
        auto it = active_calls_.find(session_id);
        if (it != active_calls_.end()) {
            it->second.status = "ended";
            active_calls_.erase(it);
        }
    }

    std::cout << "✅ Call ended successfully" << std::endl;
}

void SimpleSipClient::stream_audio_to_whisper(const std::string& session_id, const std::vector<uint8_t>& audio_data) {
    std::cout << "🎤 Streaming " << audio_data.size() << " bytes of audio to Whisper for session: " << session_id << std::endl;

    // TODO: In real implementation, this would:
    // 1. Write audio data to a queue/file for Whisper module
    // 2. Signal Whisper module to process this session_id
    // 3. Whisper module would update database with transcribed text

    // For now, simulate by updating database directly
    std::string dummy_text = "Hello, this is a test transcription from caller.";
    database_->update_session_whisper(session_id, dummy_text);
    std::cout << "✅ Simulated Whisper transcription: \"" << dummy_text << "\"" << std::endl;
}

void SimpleSipClient::stream_audio_from_piper(const std::string& session_id, const std::vector<uint8_t>& audio_data) {
    std::cout << "🔊 Streaming " << audio_data.size() << " bytes of audio from Piper for session: " << session_id << std::endl;

    // TODO: In real implementation, this would:
    // 1. Receive audio data from Piper module
    // 2. Stream it back to the SIP call
    // 3. Handle real-time audio streaming
}

// SIP Line Management Implementation
void SimpleSipClient::load_sip_lines_from_database() {
    std::lock_guard<std::mutex> lock(sip_lines_mutex_);
    sip_lines_ = database_->get_all_sip_lines();

    std::cout << "📋 Loaded " << sip_lines_.size() << " SIP lines from database:" << std::endl;
    for (const auto& line : sip_lines_) {
        std::cout << "   Line " << line.line_id << ": " << line.extension
                  << " @ " << line.server_ip << ":" << line.server_port
                  << " (status: " << line.status << ")" << std::endl;
    }
}

bool SimpleSipClient::test_sip_connection(const SipLineConfig& line) {
    std::cout << "\n🔍 ===== TESTING SIP REGISTRATION =====" << std::endl;
    std::cout << "📋 Line Details:" << std::endl;
    std::cout << "   Line ID: " << line.line_id << std::endl;
    std::cout << "   Extension: " << line.extension << std::endl;
    std::cout << "   Username: " << line.username << std::endl;
    std::cout << "   Server: " << line.server_ip << ":" << line.server_port << std::endl;
    std::cout << "   Display Name: " << line.display_name << std::endl;
    std::cout << "   Enabled: " << (line.enabled ? "YES" : "NO") << std::endl;
    std::cout << "   Current Status: " << line.status << std::endl;

    if (!line.enabled) {
        std::cout << "⚠️  Line is disabled, skipping SIP registration" << std::endl;
        return false;
    }

    std::cout << "🔌 Creating UDP socket for SIP..." << std::endl;
    int sock = socket(AF_INET, SOCK_DGRAM, 0); // UDP for SIP
    if (sock < 0) {
        std::cout << "❌ Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "✅ UDP socket created successfully (fd: " << sock << ")" << std::endl;

    std::cout << "🌐 Setting up server address..." << std::endl;
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(line.server_port);
    std::cout << "   Family: AF_INET" << std::endl;
    std::cout << "   Port: " << line.server_port << " (network order: " << ntohs(server_addr.sin_port) << ")" << std::endl;

    // Convert IP address
    std::cout << "🔍 Converting IP address: " << line.server_ip << std::endl;
    int inet_result = inet_pton(AF_INET, line.server_ip.c_str(), &server_addr.sin_addr);
    if (inet_result <= 0) {
        if (inet_result == 0) {
            std::cout << "❌ Invalid IP address format: " << line.server_ip << std::endl;
        } else {
            std::cout << "❌ inet_pton failed: " << strerror(errno) << std::endl;
        }
        close(sock);
        return false;
    }
    std::cout << "✅ IP address converted successfully" << std::endl;

    // Set socket timeout
    std::cout << "⏱️  Setting socket timeouts (3 seconds)..." << std::endl;
    struct timeval timeout;
    timeout.tv_sec = 3;  // 3 second timeout
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cout << "⚠️  Failed to set receive timeout: " << strerror(errno) << std::endl;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cout << "⚠️  Failed to set send timeout: " << strerror(errno) << std::endl;
    }

    // Create SIP REGISTER message
    std::cout << "📝 Creating SIP REGISTER message..." << std::endl;

    std::string call_id = "whisper-talk-" + std::to_string(time(nullptr));
    std::string from_tag = "tag-" + std::to_string(rand() % 10000);

    std::ostringstream sip_register;
    sip_register << "REGISTER sip:" << line.server_ip << " SIP/2.0\r\n";
    sip_register << "Via: SIP/2.0/UDP 192.168.10.150:5060;branch=z9hG4bK-" << rand() % 10000 << "\r\n";
    sip_register << "From: <sip:" << line.username << "@" << line.server_ip << ">;tag=" << from_tag << "\r\n";
    sip_register << "To: <sip:" << line.username << "@" << line.server_ip << ">\r\n";
    sip_register << "Call-ID: " << call_id << "\r\n";
    sip_register << "CSeq: 1 REGISTER\r\n";
    sip_register << "Contact: <sip:" << line.username << "@192.168.10.150:5060>\r\n";
    sip_register << "Max-Forwards: 70\r\n";
    sip_register << "User-Agent: Generic SIP Client/1.0\r\n";
    sip_register << "Expires: 3600\r\n";
    sip_register << "Content-Length: 0\r\n";
    sip_register << "\r\n";

    std::string register_msg = sip_register.str();
    std::cout << "📤 SIP REGISTER message created (" << register_msg.length() << " bytes)" << std::endl;
    std::cout << "📋 Message preview:" << std::endl;
    std::cout << "   REGISTER sip:" << line.server_ip << " SIP/2.0" << std::endl;
    std::cout << "   From: " << line.username << "@" << line.server_ip << std::endl;
    std::cout << "   Call-ID: " << call_id << std::endl;

    // Send SIP REGISTER message
    std::cout << "📡 Sending SIP REGISTER to " << line.server_ip << ":" << line.server_port << "..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    ssize_t sent_bytes = sendto(sock, register_msg.c_str(), register_msg.length(), 0,
                               (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent_bytes < 0) {
        std::cout << "❌ Failed to send SIP REGISTER: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }

    std::cout << "✅ SIP REGISTER sent successfully (" << sent_bytes << " bytes)" << std::endl;

    // Wait for response
    std::cout << "⏳ Waiting for SIP response..." << std::endl;
    char response_buffer[4096];
    struct sockaddr_in response_addr;
    socklen_t addr_len = sizeof(response_addr);

    // Set receive timeout
    struct timeval recv_timeout;
    recv_timeout.tv_sec = 5;  // 5 second timeout
    recv_timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

    ssize_t received_bytes = recvfrom(sock, response_buffer, sizeof(response_buffer) - 1, 0,
                                     (struct sockaddr*)&response_addr, &addr_len);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    close(sock);

    if (received_bytes > 0) {
        response_buffer[received_bytes] = '\0';
        std::cout << "✅ SIP response received (" << received_bytes << " bytes, " << duration.count() << "ms)" << std::endl;

        // Parse response status
        std::string response(response_buffer);
        std::cout << "📥 SIP Response:" << std::endl;

        // Show first line of response
        size_t first_line_end = response.find("\r\n");
        if (first_line_end != std::string::npos) {
            std::string status_line = response.substr(0, first_line_end);
            std::cout << "   " << status_line << std::endl;

            // Check if it's a success response (2xx)
            if (status_line.find("SIP/2.0 2") != std::string::npos) {
                std::cout << "🎉 SIP REGISTRATION SUCCESSFUL!" << std::endl;
                std::cout << "===== SIP REGISTRATION COMPLETE =====\n" << std::endl;
                return true;
            } else if (status_line.find("SIP/2.0 401") != std::string::npos ||
                      status_line.find("SIP/2.0 407") != std::string::npos) {
                std::cout << "🔐 Authentication challenge received - implementing digest auth" << std::endl;

                // Find WWW-Authenticate header
                std::string www_auth_line;
                size_t auth_pos = response.find("WWW-Authenticate:");
                if (auth_pos == std::string::npos) {
                    auth_pos = response.find("Proxy-Authenticate:");
                }

                if (auth_pos != std::string::npos) {
                    size_t line_end = response.find("\r\n", auth_pos);
                    if (line_end != std::string::npos) {
                        www_auth_line = response.substr(auth_pos, line_end - auth_pos);
                    }
                }

                if (www_auth_line.empty()) {
                    std::cout << "❌ No WWW-Authenticate header found" << std::endl;
                    std::cout << "===== SIP REGISTRATION FAILED =====\n" << std::endl;
                    return false;
                }

                // Parse authentication parameters
                std::string realm, nonce;
                if (!parse_www_authenticate(www_auth_line, realm, nonce)) {
                    std::cout << "❌ Failed to parse authentication parameters" << std::endl;
                    std::cout << "===== SIP REGISTRATION FAILED =====\n" << std::endl;
                    return false;
                }

                // Check if PBX supports qop
                bool supports_qop = (www_auth_line.find("qop=") != std::string::npos);
                std::cout << "🔍 PBX supports qop: " << (supports_qop ? "YES" : "NO") << std::endl;

                // Send authenticated REGISTER with same Call-ID
                std::cout << "🔐 Sending authenticated REGISTER..." << std::endl;

                // Extract Call-ID from initial request for reuse
                std::string call_id = "whisper-talk-" + std::to_string(time(nullptr));
                return send_authenticated_register(line, realm, nonce, supports_qop, call_id);
            } else {
                std::cout << "❌ SIP registration failed" << std::endl;
                std::cout << "===== SIP REGISTRATION FAILED =====\n" << std::endl;
                return false;
            }
        }
    } else {
        std::cout << "❌ No SIP response received (timeout after " << duration.count() << "ms)" << std::endl;
        std::cout << "   Error: " << strerror(errno) << std::endl;
        std::cout << "===== SIP REGISTRATION TIMEOUT =====\n" << std::endl;
        return false;
    }

    return false;
}

void SimpleSipClient::update_line_status(int line_id, const std::string& status) {
    if (database_->update_sip_line_status(line_id, status)) {
        std::cout << "📊 Updated line " << line_id << " status to: " << status << std::endl;
    } else {
        std::cerr << "❌ Failed to update status for line " << line_id << std::endl;
    }
}

void SimpleSipClient::sip_management_loop() {
    std::cout << "📞 Starting SIP management loop (ready for real calls)..." << std::endl;

    while (running_) {
        // Just wait - no simulation
        // Real SIP calls will be handled when they come in
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!running_) break;
    }

    std::cout << "📞 SIP management loop stopped" << std::endl;
}

void SimpleSipClient::connection_monitor_loop() {
    std::cout << "🔍 Starting SIP connection monitor..." << std::endl;

    while (running_) {
        // Reload SIP lines from database (in case they were updated via web interface)
        load_sip_lines_from_database();

        // Test connections for enabled lines
        {
            std::lock_guard<std::mutex> lock(sip_lines_mutex_);
            for (const auto& line : sip_lines_) {
                // If specific line ID is set, only process that line
                if (specific_line_id_ != -1 && line.line_id != specific_line_id_) {
                    continue;
                }

                if (!line.enabled) {
                    // Update disabled lines to disconnected status
                    if (line.status != "disabled") {
                        update_line_status(line.line_id, "disabled");
                    }
                    continue;
                }

                // Test connection for enabled lines
                update_line_status(line.line_id, "connecting");

                bool connected = test_sip_connection(line);
                std::string new_status = connected ? "connected" : "error";
                update_line_status(line.line_id, new_status);
            }
        }

        // Wait 30 seconds before next connection test cycle
        for (int i = 0; i < 30 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

std::string SimpleSipClient::create_digest_response(const std::string& username, const std::string& password,
                                                   const std::string& realm, const std::string& nonce,
                                                   const std::string& method, const std::string& uri) {
    // Calculate HA1 = MD5(username:realm:password)
    std::string ha1_input = username + ":" + realm + ":" + password;
    std::string ha1 = calculate_md5(ha1_input);

    // Calculate HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = calculate_md5(ha2_input);

    // Calculate response = MD5(HA1:nonce:HA2)
    std::string response_input = ha1 + ":" + nonce + ":" + ha2;
    std::string response = calculate_md5(response_input);

    std::cout << "🔐 Digest Authentication:" << std::endl;
    std::cout << "   Username: " << username << std::endl;
    std::cout << "   Realm: " << realm << std::endl;
    std::cout << "   Nonce: " << nonce << std::endl;
    std::cout << "   HA1: " << ha1 << std::endl;
    std::cout << "   HA2: " << ha2 << std::endl;
    std::cout << "   Response: " << response << std::endl;

    return response;
}

bool SimpleSipClient::parse_www_authenticate(const std::string& auth_header, std::string& realm, std::string& nonce) {
    std::cout << "🔍 Parsing WWW-Authenticate header:" << std::endl;
    std::cout << "   " << auth_header << std::endl;

    // Extract realm
    size_t realm_pos = auth_header.find("realm=\"");
    if (realm_pos != std::string::npos) {
        realm_pos += 7; // Skip 'realm="'
        size_t realm_end = auth_header.find("\"", realm_pos);
        if (realm_end != std::string::npos) {
            realm = auth_header.substr(realm_pos, realm_end - realm_pos);
        }
    }

    // Extract nonce
    size_t nonce_pos = auth_header.find("nonce=\"");
    if (nonce_pos != std::string::npos) {
        nonce_pos += 7; // Skip 'nonce="'
        size_t nonce_end = auth_header.find("\"", nonce_pos);
        if (nonce_end != std::string::npos) {
            nonce = auth_header.substr(nonce_pos, nonce_end - nonce_pos);
        }
    }

    std::cout << "   Extracted realm: '" << realm << "'" << std::endl;
    std::cout << "   Extracted nonce: '" << nonce << "'" << std::endl;

    return !realm.empty() && !nonce.empty();
}

bool SimpleSipClient::send_authenticated_register(const SipLineConfig& line, const std::string& realm, const std::string& nonce, bool supports_qop, const std::string& call_id) {
    std::cout << "\n🔐 ===== SENDING AUTHENTICATED REGISTER =====" << std::endl;

    // Create new socket for authenticated request
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cout << "❌ Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(line.server_port);
    inet_pton(AF_INET, line.server_ip.c_str(), &server_addr.sin_addr);

    // Calculate digest response (with or without qop based on PBX support)
    std::string uri = "sip:" + line.server_ip;
    std::string digest_response;
    std::string cnonce, nc;

    if (supports_qop) {
        cnonce = std::to_string(rand() % 100000);
        nc = "00000001";
        digest_response = create_digest_response_with_qop(line.username, line.password, realm, nonce, "REGISTER", uri, "auth", nc, cnonce);
    } else {
        std::cout << "🔐 Using simple digest (no qop) for STARFACE PBX" << std::endl;
        digest_response = create_digest_response(line.username, line.password, realm, nonce, "REGISTER", uri);
    }

    // Create authenticated SIP REGISTER message
    std::string actual_call_id = call_id.empty() ? ("whisper-talk-auth-" + std::to_string(time(nullptr))) : call_id;
    std::string from_tag = "tag-auth-" + std::to_string(rand() % 10000);

    std::ostringstream sip_register;
    sip_register << "REGISTER sip:" << line.server_ip << " SIP/2.0\r\n";
    sip_register << "Via: SIP/2.0/UDP 192.168.10.150:5060;branch=z9hG4bK-auth-" << rand() % 10000 << "\r\n";
    sip_register << "From: <sip:" << line.username << "@" << line.server_ip << ">;tag=" << from_tag << "\r\n";
    sip_register << "To: <sip:" << line.username << "@" << line.server_ip << ">\r\n";
    sip_register << "Call-ID: " << actual_call_id << "\r\n";
    sip_register << "CSeq: 2 REGISTER\r\n";
    sip_register << "Contact: <sip:" << line.username << "@192.168.10.150:5060>\r\n";
    sip_register << "Authorization: Digest username=\"" << line.username << "\", realm=\"" << realm
                 << "\", nonce=\"" << nonce << "\", uri=\"" << uri << "\", response=\"" << digest_response
                 << "\", algorithm=MD5";
    if (supports_qop) {
        sip_register << ", qop=auth, nc=" << nc << ", cnonce=\"" << cnonce << "\"";
    }
    sip_register << "\r\n";
    sip_register << "Max-Forwards: 70\r\n";
    sip_register << "User-Agent: Generic SIP Client/1.0\r\n";
    sip_register << "Expires: 3600\r\n";
    sip_register << "Content-Length: 0\r\n";
    sip_register << "\r\n";

    std::string register_msg = sip_register.str();
    std::cout << "📤 Authenticated REGISTER created (" << register_msg.length() << " bytes)" << std::endl;
    std::cout << "📋 Authorization header included with digest response" << std::endl;
    std::cout << "🔍 Raw SIP message being sent:" << std::endl;
    std::cout << "---BEGIN SIP MESSAGE---" << std::endl;
    std::cout << register_msg << std::endl;
    std::cout << "---END SIP MESSAGE---" << std::endl;

    // Send authenticated REGISTER
    std::cout << "📡 Sending authenticated REGISTER..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();

    ssize_t sent_bytes = sendto(sock, register_msg.c_str(), register_msg.length(), 0,
                               (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent_bytes < 0) {
        std::cout << "❌ Failed to send authenticated REGISTER: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }

    std::cout << "✅ Authenticated REGISTER sent (" << sent_bytes << " bytes)" << std::endl;

    // Wait for final response
    std::cout << "⏳ Waiting for authentication response..." << std::endl;
    char response_buffer[4096];
    struct sockaddr_in response_addr;
    socklen_t addr_len = sizeof(response_addr);

    // Set receive timeout
    struct timeval recv_timeout;
    recv_timeout.tv_sec = 5;
    recv_timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

    ssize_t received_bytes = recvfrom(sock, response_buffer, sizeof(response_buffer) - 1, 0,
                                     (struct sockaddr*)&response_addr, &addr_len);

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    close(sock);

    if (received_bytes > 0) {
        response_buffer[received_bytes] = '\0';
        std::string response(response_buffer);

        std::cout << "✅ Authentication response received (" << received_bytes << " bytes, " << duration.count() << "ms)" << std::endl;

        // Parse final response
        size_t first_line_end = response.find("\r\n");
        if (first_line_end != std::string::npos) {
            std::string status_line = response.substr(0, first_line_end);
            std::cout << "📥 Final Response: " << status_line << std::endl;

            if (status_line.find("SIP/2.0 2") != std::string::npos) {
                std::cout << "🎉 SIP REGISTRATION SUCCESSFUL!" << std::endl;
                std::cout << "✅ Successfully authenticated with PBX using digest authentication" << std::endl;
                std::cout << "===== SIP REGISTRATION COMPLETE =====\n" << std::endl;
                return true;
            } else {
                std::cout << "❌ Authentication failed: " << status_line << std::endl;
                std::cout << "📋 Full response for debugging:" << std::endl;
                std::cout << response << std::endl;
                std::cout << "===== SIP REGISTRATION FAILED =====\n" << std::endl;
                return false;
            }
        }
    } else {
        std::cout << "❌ No authentication response received (timeout)" << std::endl;
        std::cout << "===== SIP REGISTRATION TIMEOUT =====\n" << std::endl;
        return false;
    }

    return false;
}

std::string SimpleSipClient::create_digest_response_with_qop(const std::string& username, const std::string& password,
                                                           const std::string& realm, const std::string& nonce,
                                                           const std::string& method, const std::string& uri,
                                                           const std::string& qop, const std::string& nc, const std::string& cnonce) {
    // Calculate HA1 = MD5(username:realm:password)
    std::string ha1_input = username + ":" + realm + ":" + password;
    std::string ha1 = calculate_md5(ha1_input);

    // Calculate HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = calculate_md5(ha2_input);

    // With qop=auth: response = MD5(HA1:nonce:nc:cnonce:qop:HA2)
    std::string response_input = ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2;
    std::string response = calculate_md5(response_input);

    std::cout << "🔐 Digest Authentication with QOP:" << std::endl;
    std::cout << "   Username: " << username << std::endl;
    std::cout << "   Realm: " << realm << std::endl;
    std::cout << "   Nonce: " << nonce << std::endl;
    std::cout << "   QOP: " << qop << std::endl;
    std::cout << "   NC: " << nc << std::endl;
    std::cout << "   CNonce: " << cnonce << std::endl;
    std::cout << "   HA1: " << ha1 << std::endl;
    std::cout << "   HA2: " << ha2 << std::endl;
    std::cout << "   Response: " << response << std::endl;

    return response;
}


