// Standalone HTTP Server Module
// Serves web interface and manages database via REST API

#include "simple-http-api.h"
#include "database.h"
#include "whisper-service.h"
#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <vector>

// Global variables for signal handling
static bool g_running = true;
static std::unique_ptr<SimpleHttpServer> g_server;
static std::unique_ptr<WhisperService> g_whisper_service;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
    if (g_server) {
        g_server->stop();
    }
    if (g_whisper_service) {
        g_whisper_service->stop();
    }
}

void print_usage() {
    std::cout << "Usage: whisper-http-server [options]\n"
              << "Options:\n"
              << "  --port PORT     HTTP server port (default: 8081)\n"
              << "  --db PATH       Database file path (default: whisper_talk.db)\n"
              << "  --help          Show this help message\n";
}

int main(int argc, char** argv) {
    int port = 8081;
    std::string db_path = "whisper_talk.db";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--help") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage();
            return 1;
        }
    }
    
    std::cout << "🌐 Starting Whisper Talk LLaMA HTTP Server..." << std::endl;
    std::cout << "   Port: " << port << std::endl;
    std::cout << "   Database: " << db_path << std::endl;
    
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

    // Check for Whisper prerequisites
    bool whisper_available = false;

    // Check if whisper binary exists
    bool whisper_binary_exists = (access("./build/bin/whisper-cli", F_OK) == 0) ||
                                 (access("./build/bin/whisper-server", F_OK) == 0);

    // Check for available models
    std::vector<std::string> available_models;
    std::vector<std::string> model_candidates = {
        "models/ggml-large-v3-q5_0.bin",
        "models/ggml-large-v3.bin",
        "models/ggml-base.en.bin",
        "models/ggml-base.bin",
        "models/ggml-small.en.bin",
        "models/ggml-small.bin"
    };

    for (const auto& model : model_candidates) {
        if (access(model.c_str(), F_OK) == 0) {
            available_models.push_back(model);
        }
    }

    if (whisper_binary_exists && !available_models.empty()) {
        std::cout << "🎤 Whisper prerequisites found:" << std::endl;
        std::cout << "   • Binary: Available" << std::endl;
        std::cout << "   • Models: " << available_models.size() << " found" << std::endl;

        // Initialize Whisper service
        std::cout << "🎤 Initializing Whisper service..." << std::endl;
        g_whisper_service = std::make_unique<WhisperService>();
        g_whisper_service->set_database(&database);

        // Start Whisper service
        if (g_whisper_service->start()) {
            std::cout << "✅ Whisper service started" << std::endl;
            whisper_available = true;

            // Load model with memory check and last chosen preference
            std::cout << "🎤 Loading Whisper model with memory optimization..." << std::endl;

            std::thread model_loader([available_models]() {
                // Try to load the last chosen model first if it exists
                std::string preferred_model = "";

                // Check if we have a preference from previous runs (could be stored in config)
                // For now, prefer quantized large model if available
                for (const auto& model : available_models) {
                    if (model.find("large-v3-q5_0") != std::string::npos) {
                        preferred_model = model;
                        break;
                    }
                }

                // If no preferred model, use first available
                if (preferred_model.empty()) {
                    preferred_model = available_models[0];
                }

                std::string model_name = preferred_model.substr(preferred_model.find_last_of("/") + 1);
                model_name = model_name.substr(0, model_name.find_last_of("."));

                std::cout << "🎯 Attempting to load preferred model: " << model_name << std::endl;

                // Use memory-aware loading
                if (!g_whisper_service->load_model_with_memory_check(preferred_model, model_name)) {
                    std::cout << "❌ Failed to load any suitable model" << std::endl;
                }
            });
            model_loader.detach();
        } else {
            std::cout << "⚠️  Failed to start Whisper service, continuing without it" << std::endl;
        }
    } else {
        std::cout << "⚠️  Whisper service not available:" << std::endl;
        if (!whisper_binary_exists) {
            std::cout << "   • Missing Whisper binary (whisper-cli or whisper-server)" << std::endl;
        }
        if (available_models.empty()) {
            std::cout << "   • No Whisper models found in models/ directory" << std::endl;
            std::cout << "   • Download models with: ./download-ggml-model.sh [model-name]" << std::endl;
        }
        std::cout << "   • HTTP server will start without Whisper functionality" << std::endl;
    }

    // Create simple HTTP server with database and optional Whisper service
    g_server = std::make_unique<SimpleHttpServer>(port, &database, g_whisper_service.get());
    std::cout << "✅ HTTP server initialized" << std::endl;

    if (whisper_available) {
        std::cout << "🎤 Whisper service integration: ENABLED" << std::endl;
    } else {
        std::cout << "⚠️  Whisper service integration: DISABLED" << std::endl;
    }
    
    // Start server
    if (!g_server->start()) {
        std::cerr << "❌ Failed to start HTTP server!" << std::endl;
        return 1;
    }
    std::cout << "🚀 HTTP server listening on port " << port << std::endl;
    std::cout << "📱 Open http://localhost:" << port << " in your browser" << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    
    // Main loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "🛑 Shutting down HTTP server..." << std::endl;
    g_server->stop();

    if (g_whisper_service) {
        std::cout << "🛑 Shutting down Whisper service..." << std::endl;
        g_whisper_service->stop();
        g_whisper_service.reset();
    }

    database.close();
    std::cout << "✅ HTTP server stopped cleanly" << std::endl;
    
    return 0;
}
