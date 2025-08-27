#pragma once

#include "sip-client.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>

// Simple HTTP server for the web frontend API
class HttpApiServer {
public:
    HttpApiServer(int port = 8081);
    ~HttpApiServer();
    
    // Initialize with SIP client manager
    bool init(std::shared_ptr<SipClientManager> sip_manager);
    
    // Server control
    bool start();
    bool stop();
    bool is_running() const { return is_running_.load(); }
    
private:
    int port_;
    std::atomic<bool> is_running_{false};
    std::shared_ptr<SipClientManager> sip_manager_;
    
    // HTTP server thread
    std::thread server_thread_;
    int server_socket_;
    
    // Server methods
    void server_loop();
    void handle_client(int client_socket);
    
    // HTTP request handling
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string query;
        std::string body;
        std::unordered_map<std::string, std::string> headers;
    };
    
    struct HttpResponse {
        int status_code = 200;
        std::string status_text = "OK";
        std::unordered_map<std::string, std::string> headers;
        std::string body;
    };
    
    HttpRequest parse_request(const std::string& raw_request);
    std::string format_response(const HttpResponse& response);
    
    // API endpoints
    HttpResponse handle_api_request(const HttpRequest& request);
    HttpResponse handle_get_clients(const HttpRequest& request);
    HttpResponse handle_post_clients(const HttpRequest& request);
    HttpResponse handle_put_client(const HttpRequest& request, const std::string& client_id);
    HttpResponse handle_delete_client(const HttpRequest& request, const std::string& client_id);
    HttpResponse handle_start_client(const HttpRequest& request, const std::string& client_id);
    HttpResponse handle_stop_client(const HttpRequest& request, const std::string& client_id);
    HttpResponse handle_get_client_stats(const HttpRequest& request, const std::string& client_id);
    
    // Static file serving
    HttpResponse serve_static_file(const std::string& path);
    std::string get_mime_type(const std::string& extension);
    
    // Utility methods
    std::string url_decode(const std::string& str);
    std::vector<std::string> split_path(const std::string& path);
    SipClientConfig parse_client_config(const std::string& json);
    std::string client_config_to_json(const SipClientConfig& config);
    std::string client_stats_to_json(const SipClient::Stats& stats);
    
    // CORS headers
    void add_cors_headers(HttpResponse& response);
};

// JSON utility functions
std::string escape_json_string(const std::string& str);
std::string parse_json_string(const std::string& json, const std::string& key);
int parse_json_int(const std::string& json, const std::string& key);
bool parse_json_bool(const std::string& json, const std::string& key);
