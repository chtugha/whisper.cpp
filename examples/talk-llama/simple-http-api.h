#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// Forward declarations
class Database;
class WhisperService;
struct SipLineConfig;

// Simple HTTP server for web interface only
// No dependencies on SIP client or AI models

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::string body;
};

struct HttpResponse {
    int status_code;
    std::string status_text;
    std::map<std::string, std::string> headers;
    std::string body;
};

// Forward declarations
class Database;
struct SipLineConfig;

class SimpleHttpServer {
public:
    SimpleHttpServer(int port, Database* database = nullptr, WhisperService* whisper_service = nullptr);
    ~SimpleHttpServer();

    bool start();
    void stop();
    bool is_running() const { return running_; }
    
private:
    int port_;
    int server_socket_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    
    void server_loop();
    void handle_client(int client_socket);
    HttpRequest parse_request(const std::string& raw_request);
    std::string create_response(const HttpResponse& response);
    
    // Route handlers
    HttpResponse handle_request(const HttpRequest& request);
    HttpResponse serve_static_file(const std::string& path);
    HttpResponse handle_api_request(const HttpRequest& request);
    
    // API endpoints
    HttpResponse api_status(const HttpRequest& request);
    HttpResponse api_callers(const HttpRequest& request);
    HttpResponse api_sessions_get(const HttpRequest& request);
    HttpResponse api_sip_lines(const HttpRequest& request);
    HttpResponse api_sip_lines_post(const HttpRequest& request);
    HttpResponse api_sip_lines_delete(const HttpRequest& request, int line_id);
    HttpResponse api_sip_lines_toggle(const HttpRequest& request, int line_id);

    // System configuration endpoints
    HttpResponse api_system_speed_get(const HttpRequest& request);
    HttpResponse api_system_speed_post(const HttpRequest& request);

    // Whisper model management API
    HttpResponse api_whisper_status(const HttpRequest& request);
    HttpResponse api_whisper_models(const HttpRequest& request);
    HttpResponse api_whisper_load_model(const HttpRequest& request);
    HttpResponse api_whisper_upload_model(const HttpRequest& request);
    HttpResponse api_whisper_start(const HttpRequest& request);
    HttpResponse api_whisper_stop(const HttpRequest& request);
    // api_whisper_transcribe removed - using direct interface now
    
    std::string get_mime_type(const std::string& extension);

    // Database connection
    Database* database_;

    // Whisper service connection
    WhisperService* whisper_service_;
};
