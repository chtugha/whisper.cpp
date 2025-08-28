#!/bin/bash

# Optimized Whisper.cpp build script for Apple Silicon
# Based on: https://github.com/chtugha/whisper.cpp_macos_howto

set -e

echo "🍎 Building Whisper.cpp optimized for Apple Silicon..."

# Check if we're on Apple Silicon
if [[ $(uname -m) != "arm64" ]]; then
    echo "⚠️  Warning: This script is optimized for Apple Silicon (arm64)"
    echo "   Current architecture: $(uname -m)"
fi

# Check for required tools
echo "🔍 Checking for required tools..."

if ! command -v cmake &> /dev/null; then
    echo "❌ cmake not found. Install with: brew install cmake"
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo "❌ ninja not found. Install with: brew install ninja"
    exit 1
fi

# Find LLVM version
LLVM_PATH="/opt/homebrew/Cellar/llvm"
if [[ -d "$LLVM_PATH" ]]; then
    LLVM_VERSION=$(ls "$LLVM_PATH" | head -1)
    echo "✅ Found LLVM version: $LLVM_VERSION"
    
    # Set optimal compiler flags for Apple Silicon
    export CC="/opt/homebrew/Cellar/llvm/$LLVM_VERSION/bin/clang"
    export CXX="/opt/homebrew/Cellar/llvm/$LLVM_VERSION/bin/clang++"
    export LDFLAGS="-L/opt/homebrew/Cellar/llvm/$LLVM_VERSION/lib"
    export CPPFLAGS="-I/opt/homebrew/Cellar/llvm/$LLVM_VERSION/include"
    export SDKROOT="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
    
    echo "🔧 Using optimized LLVM compiler: $CC"
else
    echo "⚠️  LLVM not found at $LLVM_PATH, using system compiler"
    echo "   For best performance, install with: brew install llvm"
fi

# Clean previous build
if [[ -d "build" ]]; then
    echo "🧹 Cleaning previous build..."
    rm -rf build
fi

echo "⚙️  Configuring build with optimal Apple Silicon settings..."

# Configure with optimal settings for Apple Silicon
cmake -G Ninja -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DWHISPER_COREML=1 \
    -DWHISPER_CCACHE=OFF \
    -DWHISPER_METAL=1 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_C_FLAGS="-O3 -march=native -mtune=native" \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -mtune=native"

echo "🔨 Building Whisper.cpp..."
cmake --build build -j --config Release

echo "✅ Build completed successfully!"

# Test the build
if [[ -f "build/bin/whisper-cli" ]]; then
    echo "🎤 Testing Whisper build..."
    if [[ -f "samples/jfk.wav" ]]; then
        echo "   Testing with JFK sample..."
        ./build/bin/whisper-cli -m models/ggml-large-v3-q5_0.bin -f samples/jfk.wav -l auto
    else
        echo "   Sample file not found, skipping test"
    fi
else
    echo "❌ Build failed - whisper-cli not found"
    exit 1
fi

echo ""
echo "🎉 Whisper.cpp optimized build complete!"
echo ""
echo "📊 Available models:"
ls -lh models/*.bin 2>/dev/null || echo "   No models found in models/ directory"
echo ""
echo "🚀 Key optimizations enabled:"
echo "   • CoreML acceleration (WHISPER_COREML=1)"
echo "   • Metal GPU acceleration (WHISPER_METAL=1)"
echo "   • Apple Silicon native compilation (arm64)"
echo "   • LLVM optimized compiler"
echo "   • Quantized model for speed (Q5_0)"
echo ""
echo "💡 Usage examples:"
echo "   ./build/bin/whisper-cli -m models/ggml-large-v3-q5_0.bin -f audio.wav"
echo "   ./build/bin/whisper-server -m models/ggml-large-v3-q5_0.bin --port 8082"
