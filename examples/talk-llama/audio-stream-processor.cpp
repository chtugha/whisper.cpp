#include "audio-stream-processor.h"
#include "whisper.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>

// Simple VAD Implementation
SimpleVAD::SimpleVAD(float silence_threshold, int silence_duration_ms) 
    : silence_threshold_(silence_threshold), energy_window_size_(10) {
    silence_duration_samples_ = (silence_duration_ms * 16000) / 1000; // Assume 16kHz
    recent_energy_.reserve(energy_window_size_);
}

bool SimpleVAD::has_speech(const std::vector<float>& audio_samples) {
    if (audio_samples.empty()) return false;
    
    // Calculate RMS energy
    float energy = 0.0f;
    for (float sample : audio_samples) {
        energy += sample * sample;
    }
    energy = std::sqrt(energy / audio_samples.size());
    
    return energy > silence_threshold_;
}

bool SimpleVAD::is_silence_boundary(const std::vector<float>& audio_samples) {
    if (audio_samples.size() < silence_duration_samples_) return false;
    
    // Check last N samples for silence
    size_t check_samples = std::min((size_t)silence_duration_samples_, audio_samples.size());
    std::vector<float> tail_samples(
        audio_samples.end() - check_samples, 
        audio_samples.end()
    );
    
    return !has_speech(tail_samples);
}

// Audio Stream Processor Implementation
AudioStreamProcessor::AudioStreamProcessor(STTInterface* stt_engine) 
    : stt_engine_(stt_engine), running_(false) {
}

AudioStreamProcessor::~AudioStreamProcessor() {
    stop();
}

bool AudioStreamProcessor::start() {
    if (running_.load()) return true;
    if (!stt_engine_ || !stt_engine_->is_ready()) return false;
    
    running_.store(true);
    processing_thread_ = std::thread(&AudioStreamProcessor::processing_loop, this);
    
    std::cout << "🎤 Audio Stream Processor started" << std::endl;
    return true;
}

void AudioStreamProcessor::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    std::cout << "🛑 Audio Stream Processor stopped" << std::endl;
}

void AudioStreamProcessor::add_audio(const std::string& session_id, const std::vector<float>& audio_samples) {
    if (audio_samples.empty()) return;
    
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    auto& buffer = session_buffers_[session_id];
    
    // Initialize if new session
    if (buffer.audio_buffer.empty()) {
        buffer.chunk_start = std::chrono::steady_clock::now();
        std::cout << "🎵 New audio session started: " << session_id << std::endl;
    }
    
    // Append audio samples
    buffer.audio_buffer.insert(buffer.audio_buffer.end(), audio_samples.begin(), audio_samples.end());
    buffer.last_activity = std::chrono::steady_clock::now();
    
    // Update speech detection
    if (vad_.has_speech(audio_samples)) {
        buffer.has_speech = true;
    }
}

void AudioStreamProcessor::set_transcription_callback(TranscriptionCallback callback) {
    transcription_callback_ = callback;
}

void AudioStreamProcessor::processing_loop() {
    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(buffers_mutex_);
            
            // Process each session buffer
            for (auto& [session_id, buffer] : session_buffers_) {
                if (should_process_chunk(buffer)) {
                    process_session_buffer(session_id, buffer);
                }
            }
            
            // Cleanup old sessions
            cleanup_old_sessions();
        }
        
        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool AudioStreamProcessor::should_process_chunk(const SessionBuffer& buffer) {
    if (buffer.audio_buffer.empty() || !buffer.has_speech) return false;
    
    auto now = std::chrono::steady_clock::now();
    auto chunk_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer.chunk_start).count();
    
    // Process if:
    // 1. We have minimum duration AND silence boundary detected
    // 2. OR we've reached maximum duration (timeout)
    if (chunk_duration >= min_chunk_duration_ms_) {
        if (vad_.is_silence_boundary(buffer.audio_buffer) || chunk_duration >= max_chunk_duration_ms_) {
            return true;
        }
    }
    
    return false;
}

void AudioStreamProcessor::process_session_buffer(const std::string& session_id, SessionBuffer& buffer) {
    if (buffer.audio_buffer.empty()) return;
    
    std::cout << "🔄 Processing audio chunk for session " << session_id 
              << " (" << buffer.audio_buffer.size() << " samples)" << std::endl;
    
    // Transcribe audio
    std::string transcription = stt_engine_->transcribe(buffer.audio_buffer);
    
    // Call callback with result
    if (transcription_callback_ && !transcription.empty()) {
        transcription_callback_(session_id, transcription);
        std::cout << "✅ Transcription: \"" << transcription << "\"" << std::endl;
    }
    
    // Reset buffer for next chunk
    buffer.audio_buffer.clear();
    buffer.chunk_start = std::chrono::steady_clock::now();
    buffer.has_speech = false;
}

void AudioStreamProcessor::cleanup_old_sessions() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = session_buffers_.begin(); it != session_buffers_.end();) {
        auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_activity).count();
        
        if (idle_time > 30) { // Remove sessions idle for 30+ seconds
            std::cout << "🧹 Cleaning up idle session: " << it->first << std::endl;
            it = session_buffers_.erase(it);
        } else {
            ++it;
        }
    }
}

// Whisper STT Implementation
WhisperSTT::WhisperSTT(struct whisper_context* ctx) : whisper_ctx_(ctx) {
}

bool WhisperSTT::is_ready() {
    return whisper_ctx_ != nullptr;
}

std::string WhisperSTT::transcribe(const std::vector<float>& audio_samples) {
    if (!whisper_ctx_ || audio_samples.empty()) return "";
    
    std::lock_guard<std::mutex> lock(whisper_mutex_);
    
    // Set up Whisper parameters for real-time processing
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.translate = false;
    wparams.no_context = true;
    wparams.single_segment = true;
    wparams.max_tokens = 32;
    wparams.language = "en";
    wparams.n_threads = 4;
    
    // Process audio
    if (whisper_full(whisper_ctx_, wparams, audio_samples.data(), audio_samples.size()) != 0) {
        return "";
    }
    
    // Extract transcription
    std::string result;
    const int n_segments = whisper_full_n_segments(whisper_ctx_);
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(whisper_ctx_, i);
        result += text;
    }
    
    return result;
}
