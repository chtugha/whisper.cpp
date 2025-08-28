# WebSocket version - no longer requires SDL2
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL for WebSocket support
find_package(OpenSSL REQUIRED)

# Standalone HTTP Server Module (for debugging)
add_executable(whisper-http-server
    http-server.cpp
    simple-http-api.cpp
    database.cpp
)

# Standalone SIP Client Module
add_executable(whisper-sip-client
    sip-client-main.cpp
    database.cpp
)

target_include_directories(whisper-sip-client PRIVATE ${OPENSSL_INCLUDE_DIR})

set(TARGET whisper-talk-llama)
add_executable(${TARGET}
    talk-llama.cpp
    sip-client.cpp
    http-api.cpp
    database.cpp
    tts-engine.mm
    llama.cpp
    llama-adapter.cpp
    llama-arch.cpp
    llama-batch.cpp
    llama-chat.cpp
    llama-context.cpp
    llama-cparams.cpp
    llama-grammar.cpp
    llama-graph.cpp
    llama-hparams.cpp
    llama-impl.cpp
    llama-io.cpp
    llama-kv-cache-unified.cpp
    llama-kv-cache-unified-iswa.cpp
    llama-memory-recurrent.cpp
    llama-memory-hybrid.cpp
    llama-memory.cpp
    llama-mmap.cpp
    llama-model-loader.cpp
    llama-model-saver.cpp
    llama-model.cpp
    llama-quant.cpp
    llama-sampling.cpp
    llama-vocab.cpp
    unicode.cpp
    unicode-data.cpp)

target_include_directories(${TARGET} PRIVATE ${OPENSSL_INCLUDE_DIR})

# Link HTTP server module
target_link_libraries(whisper-http-server PRIVATE
    common
    ${OPENSSL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    sqlite3)

# Link SIP client module
target_link_libraries(whisper-sip-client PRIVATE
    common
    ${OPENSSL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    sqlite3)

# Link main target
target_link_libraries(${TARGET} PRIVATE
    common
    whisper
    ${OPENSSL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    sqlite3)

# Add macOS frameworks for TTS support
if(APPLE)
    target_link_libraries(${TARGET} PRIVATE
        "-framework Foundation"
        "-framework AVFoundation")
endif()

if(WIN32)
    # It requires Windows 8.1 or later for PrefetchVirtualMemory
    target_compile_definitions(${TARGET} PRIVATE -D_WIN32_WINNT=0x0602)
endif()

include(DefaultTargetOptions)
