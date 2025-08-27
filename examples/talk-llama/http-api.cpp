#include "http-api.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <regex>
#include <iostream>

HttpApiServer::HttpApiServer(int port) : port_(port), server_socket_(-1) {
}

HttpApiServer::~HttpApiServer() {
    stop();
}

bool HttpApiServer::init(std::shared_ptr<SipClientManager> sip_manager) {
    sip_manager_ = sip_manager;
    return true;
}

bool HttpApiServer::start() {
    if (is_running_.load()) {
        return false;
    }
    
    // Create server socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        fprintf(stderr, "Failed to create HTTP server socket\n");
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Failed to set HTTP socket options\n");
        close(server_socket_);
        return false;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind HTTP socket to port %d\n", port_);
        close(server_socket_);
        return false;
    }
    
    // Listen for connections
    if (listen(server_socket_, 10) < 0) {
        fprintf(stderr, "Failed to listen on HTTP socket\n");
        close(server_socket_);
        return false;
    }
    
    is_running_.store(true);
    server_thread_ = std::thread(&HttpApiServer::server_loop, this);
    
    printf("HTTP API server listening on port %d\n", port_);
    return true;
}

bool HttpApiServer::stop() {
    if (!is_running_.load()) {
        return false;
    }
    
    is_running_.store(false);
    
    // Close server socket
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    // Wait for server thread
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    printf("HTTP API server stopped\n");
    return true;
}

void HttpApiServer::server_loop() {
    while (is_running_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (is_running_.load()) {
                fprintf(stderr, "Failed to accept HTTP client connection\n");
            }
            continue;
        }
        
        // Handle client in separate thread
        std::thread([this, client_socket]() {
            handle_client(client_socket);
            close(client_socket);
        }).detach();
    }
}

void HttpApiServer::handle_client(int client_socket) {
    char buffer[8192];
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return;
    }
    
    buffer[bytes_read] = '\0';
    std::string raw_request(buffer);
    
    // Parse HTTP request
    HttpRequest request = parse_request(raw_request);
    HttpResponse response;
    
    // Route request
    if (request.path.starts_with("/api/")) {
        response = handle_api_request(request);
    } else {
        // Serve static files
        std::string file_path = request.path;
        if (file_path == "/") {
            file_path = "/index.html";
        }
        response = serve_static_file(file_path);
    }
    
    // Add CORS headers
    add_cors_headers(response);
    
    // Send response
    std::string response_str = format_response(response);
    send(client_socket, response_str.c_str(), response_str.length(), 0);
}

HttpApiServer::HttpRequest HttpApiServer::parse_request(const std::string& raw_request) {
    HttpRequest request;
    
    std::istringstream stream(raw_request);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream request_line(line);
        std::string path_and_query;
        request_line >> request.method >> path_and_query;
        
        // Split path and query
        size_t query_pos = path_and_query.find('?');
        if (query_pos != std::string::npos) {
            request.path = path_and_query.substr(0, query_pos);
            request.query = path_and_query.substr(query_pos + 1);
        } else {
            request.path = path_and_query;
        }
    }
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            
            request.headers[key] = value;
        }
    }
    
    // Parse body
    std::string body_line;
    while (std::getline(stream, body_line)) {
        request.body += body_line + "\n";
    }
    
    return request;
}

std::string HttpApiServer::format_response(const HttpResponse& response) {
    std::ostringstream stream;
    
    // Status line
    stream << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";
    
    // Headers
    for (const auto& [key, value] : response.headers) {
        stream << key << ": " << value << "\r\n";
    }
    
    // Content-Length
    stream << "Content-Length: " << response.body.length() << "\r\n";
    
    // End of headers
    stream << "\r\n";
    
    // Body
    stream << response.body;
    
    return stream.str();
}

HttpApiServer::HttpResponse HttpApiServer::handle_api_request(const HttpRequest& request) {
    HttpResponse response;
    
    if (!sip_manager_) {
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"error": "SIP manager not initialized"})";
        response.headers["Content-Type"] = "application/json";
        return response;
    }
    
    std::vector<std::string> path_parts = split_path(request.path);
    
    if (path_parts.size() >= 2 && path_parts[1] == "clients") {
        if (request.method == "GET" && path_parts.size() == 2) {
            return handle_get_clients(request);
        } else if (request.method == "POST" && path_parts.size() == 2) {
            return handle_post_clients(request);
        } else if (path_parts.size() >= 3) {
            std::string client_id = path_parts[2];
            
            if (request.method == "PUT") {
                return handle_put_client(request, client_id);
            } else if (request.method == "DELETE") {
                return handle_delete_client(request, client_id);
            } else if (path_parts.size() >= 4) {
                if (path_parts[3] == "start" && request.method == "POST") {
                    return handle_start_client(request, client_id);
                } else if (path_parts[3] == "stop" && request.method == "POST") {
                    return handle_stop_client(request, client_id);
                } else if (path_parts[3] == "stats" && request.method == "GET") {
                    return handle_get_client_stats(request, client_id);
                }
            }
        }
    }
    
    // Not found
    response.status_code = 404;
    response.status_text = "Not Found";
    response.body = R"({"error": "API endpoint not found"})";
    response.headers["Content-Type"] = "application/json";
    
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::handle_get_clients(const HttpRequest& request) {
    HttpResponse response;
    
    try {
        auto clients = sip_manager_->get_all_clients();
        auto active_clients = sip_manager_->get_active_clients();
        
        std::ostringstream json;
        json << "[";
        
        for (size_t i = 0; i < clients.size(); ++i) {
            if (i > 0) json << ",";
            
            const auto& config = clients[i];
            bool is_active = std::find(active_clients.begin(), active_clients.end(), 
                                     config.client_id) != active_clients.end();
            
            json << "{"
                 << R"("client_id":")" << escape_json_string(config.client_id) << R"(",)"
                 << R"("username":")" << escape_json_string(config.username) << R"(",)"
                 << R"("server_ip":")" << escape_json_string(config.server_ip) << R"(",)"
                 << R"("server_port":)" << config.server_port << ","
                 << R"("display_name":")" << escape_json_string(config.display_name) << R"(",)"
                 << R"("auto_answer":)" << (config.auto_answer ? "true" : "false") << ","
                 << R"("use_tts":)" << (config.use_tts ? "true" : "false") << ","
                 << R"("greeting":")" << escape_json_string(config.greeting) << R"(",)"
                 << R"("ai_persona":")" << escape_json_string(config.ai_persona) << R"(",)"
                 << R"("status":")" << (is_active ? "registered" : "offline") << R"(")"
                 << "}";
        }
        
        json << "]";
        
        response.body = json.str();
        response.headers["Content-Type"] = "application/json";
        
    } catch (const std::exception& e) {
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = R"({"error": "Failed to get clients"})";
        response.headers["Content-Type"] = "application/json";
    }
    
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::handle_post_clients(const HttpRequest& request) {
    HttpResponse response;
    
    try {
        SipClientConfig config = parse_client_config(request.body);
        
        if (sip_manager_->add_client(config)) {
            response.status_code = 201;
            response.status_text = "Created";
            response.body = R"({"message": "Client added successfully"})";
        } else {
            response.status_code = 400;
            response.status_text = "Bad Request";
            response.body = R"({"error": "Failed to add client"})";
        }
        
        response.headers["Content-Type"] = "application/json";
        
    } catch (const std::exception& e) {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.body = R"({"error": "Invalid client configuration"})";
        response.headers["Content-Type"] = "application/json";
    }
    
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::handle_delete_client(const HttpRequest& request, const std::string& client_id) {
    HttpResponse response;
    
    if (sip_manager_->remove_client(client_id)) {
        response.body = R"({"message": "Client removed successfully"})";
    } else {
        response.status_code = 404;
        response.status_text = "Not Found";
        response.body = R"({"error": "Client not found"})";
    }
    
    response.headers["Content-Type"] = "application/json";
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::handle_start_client(const HttpRequest& request, const std::string& client_id) {
    HttpResponse response;
    
    if (sip_manager_->start_client(client_id)) {
        response.body = R"({"message": "Client started successfully"})";
    } else {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.body = R"({"error": "Failed to start client"})";
    }
    
    response.headers["Content-Type"] = "application/json";
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::handle_stop_client(const HttpRequest& request, const std::string& client_id) {
    HttpResponse response;
    
    if (sip_manager_->stop_client(client_id)) {
        response.body = R"({"message": "Client stopped successfully"})";
    } else {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.body = R"({"error": "Failed to stop client"})";
    }
    
    response.headers["Content-Type"] = "application/json";
    return response;
}

HttpApiServer::HttpResponse HttpApiServer::serve_static_file(const std::string& path) {
    HttpResponse response;

    // Security: prevent directory traversal
    if (path.find("..") != std::string::npos) {
        response.status_code = 403;
        response.status_text = "Forbidden";
        response.body = "Access denied";
        return response;
    }

    std::string file_path = "examples/talk-llama/web-frontend" + path;
    std::ifstream file(file_path, std::ios::binary);

    if (!file.is_open()) {
        response.status_code = 404;
        response.status_text = "Not Found";
        response.body = "File not found";
        return response;
    }

    // Read file content
    std::ostringstream content;
    content << file.rdbuf();
    response.body = content.str();

    // Set content type
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        std::string extension = path.substr(dot_pos);
        response.headers["Content-Type"] = get_mime_type(extension);
    }

    return response;
}

std::string HttpApiServer::get_mime_type(const std::string& extension) {
    if (extension == ".html") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".svg") return "image/svg+xml";
    return "text/plain";
}

void HttpApiServer::add_cors_headers(HttpResponse& response) {
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
}

std::vector<std::string> HttpApiServer::split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string part;

    while (std::getline(stream, part, '/')) {
        if (!part.empty()) {
            parts.push_back(url_decode(part));
        }
    }

    return parts;
}

std::string HttpApiServer::url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value = std::stoi(str.substr(i + 1, 2), nullptr, 16);
            result += static_cast<char>(value);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

SipClientConfig HttpApiServer::parse_client_config(const std::string& json) {
    SipClientConfig config;

    config.client_id = parse_json_string(json, "client_id");
    config.username = parse_json_string(json, "username");
    config.password = parse_json_string(json, "password");
    config.server_ip = parse_json_string(json, "server_ip");
    config.server_port = parse_json_int(json, "server_port");
    config.display_name = parse_json_string(json, "display_name");
    config.auto_answer = parse_json_bool(json, "auto_answer");
    config.use_tts = parse_json_bool(json, "use_tts");
    config.greeting = parse_json_string(json, "greeting");
    config.ai_persona = parse_json_string(json, "ai_persona");

    return config;
}

// JSON utility functions
std::string escape_json_string(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string parse_json_string(const std::string& json, const std::string& key) {
    std::regex pattern(R"(")" + key + R"("\s*:\s*"([^"]*)")");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str();
    }
    return "";
}

int parse_json_int(const std::string& json, const std::string& key) {
    std::regex pattern(R"(")" + key + R"("\s*:\s*(\d+))");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return 0;
}

bool parse_json_bool(const std::string& json, const std::string& key) {
    std::regex pattern(R"(")" + key + R"("\s*:\s*(true|false))");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str() == "true";
    }
    return false;
}
