#!/bin/bash

# Installation script for Whisper Talk LLaMA Menu Bar App

APP_NAME="WhisperTalkLlama.app"
SOURCE_PATH="/Users/whisper/Documents/augment-projects/github versionierung/whisper.cpp/$APP_NAME"
DEST_PATH="/Applications/$APP_NAME"

echo "🚀 Installing Whisper Talk LLaMA Menu Bar App..."

# Check if source app exists
if [ ! -d "$SOURCE_PATH" ]; then
    echo "❌ Error: Source app not found at $SOURCE_PATH"
    echo "Please make sure the app has been built first."
    exit 1
fi

# Check if destination already exists
if [ -d "$DEST_PATH" ]; then
    echo "⚠️  App already exists in Applications folder."
    read -p "Do you want to replace it? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation cancelled."
        exit 0
    fi
    echo "🗑️  Removing existing app..."
    sudo rm -rf "$DEST_PATH"
fi

# Copy app to Applications
echo "📦 Copying app to Applications folder..."
sudo cp -R "$SOURCE_PATH" "$DEST_PATH"

# Set proper permissions
echo "🔐 Setting permissions..."
sudo chown -R root:admin "$DEST_PATH"
sudo chmod -R 755 "$DEST_PATH"

# Verify installation
if [ -d "$DEST_PATH" ]; then
    echo "✅ Installation successful!"
    echo ""
    echo "📱 The Whisper Talk LLaMA menu bar app has been installed to:"
    echo "   $DEST_PATH"
    echo ""
    echo "🎯 To use the app:"
    echo "   1. Open Finder and go to Applications"
    echo "   2. Double-click on 'WhisperTalkLlama' to launch"
    echo "   3. Look for the Hello Kitty-style icon in your menu bar (top-right)"
    echo "   4. Click the icon to access the menu with options to:"
    echo "      • Open Web Interface (http://localhost:8081)"
    echo "      • Start/Stop the AI Phone Server"
    echo "      • Quit the application"
    echo ""
    echo "🔧 Note: Make sure you have the required models in the models/ directory"
    echo "   before starting the server."
    echo ""
    echo "🌟 Enjoy your AI Phone System!"
else
    echo "❌ Installation failed!"
    exit 1
fi
