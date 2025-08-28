#pragma once

#include <string>
#include <memory>
#include <vector>
#include <sqlite3.h>

struct Caller {
    int id;
    std::string phone_number;
    std::string created_at;
    std::string last_call;
};

struct CallSession {
    std::string session_id;
    int caller_id;
    std::string phone_number;
    int line_id;                    // Which SIP line handled this call
    std::string user_id;            // Optional unique user identifier
    std::string start_time;
    std::string whisper_data;
    std::string llama_response;
    std::string piper_audio_path;
};

struct SipLineConfig {
    int line_id;
    std::string extension;
    std::string username;
    std::string password;
    std::string server_ip;
    int server_port;
    std::string display_name;
    bool enabled;
    std::string status;
};

class Database {
public:
    Database();
    ~Database();
    
    bool init(const std::string& db_path = "whisper_talk.db");
    void close();
    
    // Caller management
    int get_or_create_caller(const std::string& phone_number);
    bool update_caller_last_call(int caller_id);
    std::vector<Caller> get_all_callers();
    
    // Session management
    std::string create_session(int caller_id, const std::string& phone_number, int line_id, const std::string& user_id = "");
    bool update_session_whisper(const std::string& session_id, const std::string& whisper_data);
    bool update_session_llama(const std::string& session_id, const std::string& llama_response);
    bool append_session_llama(const std::string& session_id, const std::string& llama_text);
    bool update_session_piper(const std::string& session_id, const std::string& piper_audio_path);
    CallSession get_session(const std::string& session_id);

    // SIP line management
    int create_sip_line(const std::string& extension, const std::string& username,
                       const std::string& password, const std::string& server_ip,
                       int server_port, const std::string& display_name);
    std::vector<SipLineConfig> get_all_sip_lines();
    bool update_sip_line_status(int line_id, const std::string& status);
    bool toggle_sip_line(int line_id);
    bool delete_sip_line(int line_id);
    SipLineConfig get_sip_line(int line_id);
    std::vector<CallSession> get_caller_sessions(int caller_id, int limit = 10);

    // System configuration
    int get_system_speed(); // 1-5 scale (1=slow, 5=fast)
    bool set_system_speed(int speed);

private:
    sqlite3* db_;
    bool create_tables();
    std::string generate_uuid();
    std::string get_current_timestamp();
};
