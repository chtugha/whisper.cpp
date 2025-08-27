# whisper.cpp/examples/talk-llama

AI Phone System - SIP-based Multi-client AI Assistant

This is a complete AI phone system that allows multiple SIP clients to connect and have independent conversations with an AI assistant. The system uses Whisper for speech recognition, LLaMA for AI responses, and TTS for speech synthesis.

## Key Features

- **SIP-based Architecture**: Connect real phones, softphones, or PBX systems
- **Multi-client Support**: Multiple callers can talk to the AI simultaneously
- **Per-client Sessions**: Each caller has their own conversation context and history
- **Web Management Interface**: Easy-to-use web frontend for managing SIP clients
- **Text-to-Speech**: AI responses are converted to speech and sent back to callers
- **Lightweight & Optimized**: Designed for Apple Silicon with minimal resource usage
- **Real Phone Integration**: Works with actual phone systems and VoIP infrastructure

## Building

The AI Phone System depends on OpenSSL for HTTP API support:

```bash
# Install OpenSSL
# On Debian based linux distributions:
sudo apt-get install libssl-dev

# On Fedora Linux:
sudo dnf install openssl-devel

# On Mac OS (Apple Silicon optimized)
brew install openssl

# Build the AI Phone System
cmake -B build -S .
cmake --build build --config Release

# Run the system
./build/bin/whisper-talk-llama -mw ./models/ggml-small.en.bin -ml ../llama.cpp/models/llama-13b/ggml-model-q4_0.gguf -hp 8081
```

## Command Line Options

- The `-mw` argument specifies the Whisper model. Recommended `base` or `small` for real-time phone calls
- The `-ml` argument specifies the LLaMA model. See https://github.com/ggerganov/llama.cpp for model information
- The `-hp` argument specifies the HTTP API server port (default: 8081)

## Using the AI Phone System

### 1. Start the System

```bash
./build/bin/whisper-talk-llama -mw ./models/ggml-small.en.bin -ml ../llama.cpp/models/llama-13b/ggml-model-q4_0.gguf -hp 8081
```

### 2. Access the Web Interface

Open your browser and go to: `http://localhost:8081`

### 3. Configure SIP Clients

1. Click "Add New SIP Client" in the web interface
2. Fill in your SIP server details:
   - **Client ID**: Unique identifier (e.g., "office-phone-1")
   - **Username**: SIP username from your phone provider
   - **Password**: SIP password
   - **Server IP**: IP address of your SIP server/PBX
   - **Server Port**: Usually 5060
   - **Display Name**: Name shown to callers
   - **Auto Answer**: Whether to automatically answer calls
   - **Greeting**: Message spoken when calls are answered

3. Click "Add SIP Client"

### 4. Start SIP Clients

- Click the "Start" button next to each SIP client
- The client will register with your SIP server
- Status indicator will turn green when registered

### 5. Make Calls

- Call the phone number/extension associated with your SIP client
- The AI will answer automatically (if enabled) and greet you
- Speak naturally - your voice will be transcribed and the AI will respond
- The AI's response will be converted to speech and played back

## Architecture

The AI Phone System uses a sophisticated multi-client architecture:

### Core Components

- **Shared AI Models**: Single whisper_context and llama_context shared across all clients
- **Per-Client Sessions**: Each caller gets their own whisper_state and llama_seq_id
- **SIP Client Manager**: Manages multiple SIP registrations and call handling
- **HTTP API Server**: Provides web interface for configuration and monitoring
- **TTS Engine**: Converts AI responses to speech (Piper TTS or macOS TTS)

### Call Flow

```
Incoming Call → SIP Client → RTP Audio → Whisper (transcription) →
LLaMA (AI response) → TTS (speech synthesis) → RTP Audio → Outgoing Call
```

### Session Isolation

Each phone call gets:
- **Unique whisper_state**: Independent audio processing
- **Unique llama_seq_id**: Separate conversation context in shared LLaMA model
- **Conversation history**: Maintained per caller
- **Audio buffers**: Isolated incoming/outgoing audio streams

This ensures that multiple callers can have completely independent conversations with the AI simultaneously, while using minimal system resources.

## SIP Integration

The system acts as a SIP User Agent and can:
- Register with SIP servers/PBX systems
- Receive incoming calls
- Handle RTP audio streams
- Send audio responses back to callers
- Work with any standard SIP infrastructure

## Apple Silicon Optimization

- **Native ARM64**: Optimized for M1/M2/M3 processors
- **Metal acceleration**: GPU acceleration where available
- **Efficient TTS**: Uses native macOS TTS or lightweight Piper TTS
- **Low latency**: Designed for real-time phone conversations

## Session

The `whisper-talk-llama` tool supports session management to enable more coherent and continuous conversations. By maintaining context from previous interactions, it can better understand and respond to user requests in a more natural way.

To enable session support, use the `--session FILE` command line option when running the program. The `whisper-talk-llama` model state will be saved to the specified file after each interaction. If the file does not exist, it will be created. If the file exists, the model state will be loaded from it, allowing you to resume a previous session.

This feature is especially helpful for maintaining context in long conversations or when interacting with the AI assistant across multiple sessions. It ensures that the assistant remembers the previous interactions and can provide more relevant and contextual responses.

Example usage:

```bash
./build/bin/whisper-talk-llama --session ./my-session-file -mw ./models/ggml-small.en.bin -ml ../llama.cpp/models/llama-13b/ggml-model-q4_0.gguf -p "Georgi" -t 8
```

## TTS

For best experience, this example needs a TTS tool to convert the generated text responses to voice.
You can use any TTS engine that you would like - simply edit the [speak](speak) script to your needs.
By default, it is configured to use MacOS's `say` or Windows SpeechSynthesizer, but you can use whatever you wish.

## Discussion

If you have any feedback, please let "us" know in the following discussion: https://github.com/ggerganov/whisper.cpp/discussions/672?converting=1
