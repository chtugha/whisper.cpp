#include "audio-processor-service.h"
#include "database.h"
#include <iostream>
#include <signal.h>
#include <memory>
#include <thread>
#include <chrono>

static std::unique_ptr<AudioProcessorService> g_audio_service;
static std::unique_ptr<Database> g_database;

void signal_handler(int signal) {
    std::cout << "\n🛑 Received signal " << signal << ", shutting down..." << std::endl;
    
    if (g_audio_service) {
        g_audio_service->stop();
    }
    
    exit(0);
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --port PORT        Service port (default: 8083)" << std::endl;
    std::cout << "  -d, --database PATH    Database path (default: whisper_talk.db)" << std::endl;
    std::cout << "  -w, --whisper URL      Whisper service URL (default: http://localhost:8082)" << std::endl;
    std::cout << "  -h, --help             Show this help" << std::endl;
}

int main(int argc, char** argv) {
    // Default configuration
    int port = 8083;
    std::string database_path = "whisper_talk.db";
    std::string whisper_endpoint = "http://localhost:8082";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else {
                std::cerr << "❌ Port argument requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-d" || arg == "--database") {
            if (i + 1 < argc) {
                database_path = argv[++i];
            } else {
                std::cerr << "❌ Database argument requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "-w" || arg == "--whisper") {
            if (i + 1 < argc) {
                whisper_endpoint = argv[++i];
            } else {
                std::cerr << "❌ Whisper endpoint argument requires a value" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "❌ Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::cout << "🎵 Starting Audio Processor Service..." << std::endl;
    std::cout << "📡 Port: " << port << std::endl;
    std::cout << "🗄️ Database: " << database_path << std::endl;
    std::cout << "🎤 Whisper endpoint: " << whisper_endpoint << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize database
    g_database = std::make_unique<Database>();
    if (!g_database->init(database_path)) {
        std::cerr << "❌ Failed to initialize database" << std::endl;
        return 1;
    }
    
    // Create audio processor service
    g_audio_service = AudioProcessorServiceFactory::create();
    g_audio_service->set_database(g_database.get());
    g_audio_service->set_whisper_endpoint(whisper_endpoint);
    
    // Start service
    if (!g_audio_service->start(port)) {
        std::cerr << "❌ Failed to start audio processor service" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Audio Processor Service running on port " << port << std::endl;
    std::cout << "📋 Status:" << std::endl;
    
    // Main service loop
    while (g_audio_service->is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Print status every 5 seconds
        auto status = g_audio_service->get_status();
        std::cout << "📊 Active sessions: " << status.active_sessions 
                  << ", Packets processed: " << status.total_packets_processed << std::endl;
    }
    
    std::cout << "🛑 Audio Processor Service stopped" << std::endl;
    return 0;
}
