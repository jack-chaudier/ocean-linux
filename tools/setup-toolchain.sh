#!/bin/bash
#
# Ocean Kernel - Cross-Compiler Setup Script
#
# This script helps set up the x86_64-elf cross-compiler toolchain
# required to build Ocean on non-x86_64 systems (like macOS ARM64).
#

set -e

echo "Ocean Kernel Toolchain Setup"
echo "============================"
echo

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    echo "Detected: macOS"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    echo "Detected: Linux"
else
    echo "Unsupported OS: $OSTYPE"
    exit 1
fi

# Check for existing cross-compiler
if command -v x86_64-elf-gcc &> /dev/null; then
    echo "x86_64-elf-gcc is already installed!"
    x86_64-elf-gcc --version | head -1
    exit 0
fi

echo
echo "x86_64-elf-gcc not found. Installing..."
echo

if [[ "$OS" == "macos" ]]; then
    # Check for Homebrew
    if ! command -v brew &> /dev/null; then
        echo "Homebrew is required. Install it from https://brew.sh"
        exit 1
    fi

    echo "Installing via Homebrew..."
    echo "This may take 15-30 minutes to compile."
    echo

    # Install dependencies
    brew install gmp mpfr libmpc || true

    # Install x86_64-elf-gcc from homebrew
    # Note: You may need to tap a specific repo
    if brew tap | grep -q "nativeos/i386-elf-toolchain"; then
        echo "Using nativeos tap..."
    else
        echo "Adding nativeos tap for cross-compiler..."
        brew tap nativeos/i386-elf-toolchain || true
    fi

    # Try to install pre-built binaries first
    echo "Attempting to install pre-built x86_64-elf-gcc..."
    brew install x86_64-elf-gcc 2>/dev/null || {
        echo
        echo "Pre-built binaries not available."
        echo
        echo "Options:"
        echo "1. Build from source (takes 30+ minutes):"
        echo "   brew install --build-from-source x86_64-elf-gcc"
        echo
        echo "2. Use Docker (recommended for quick setup):"
        echo "   See docker/Dockerfile in this repo"
        echo
        echo "3. Install manually using crosstool-ng"
        echo
        exit 1
    }

    # Also install NASM
    brew install nasm xorriso || true

elif [[ "$OS" == "linux" ]]; then
    # Check for package manager
    if command -v apt-get &> /dev/null; then
        echo "Installing via apt..."
        sudo apt-get update
        sudo apt-get install -y gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
        sudo apt-get install -y nasm xorriso mtools
    elif command -v dnf &> /dev/null; then
        echo "Installing via dnf..."
        sudo dnf install -y gcc binutils nasm xorriso mtools
    elif command -v pacman &> /dev/null; then
        echo "Installing via pacman..."
        sudo pacman -S --noconfirm gcc binutils nasm xorriso mtools
    else
        echo "Unsupported package manager. Please install x86_64 cross-compiler manually."
        exit 1
    fi
fi

echo
echo "Verifying installation..."

if command -v x86_64-elf-gcc &> /dev/null; then
    echo "SUCCESS: x86_64-elf-gcc installed"
    x86_64-elf-gcc --version | head -1
elif command -v x86_64-linux-gnu-gcc &> /dev/null; then
    echo "SUCCESS: x86_64-linux-gnu-gcc installed"
    x86_64-linux-gnu-gcc --version | head -1
else
    echo "WARNING: Cross-compiler not found in PATH"
    echo "You may need to add it to your PATH or build from source"
fi

echo
echo "Other required tools:"
command -v nasm &> /dev/null && echo "  nasm: OK" || echo "  nasm: MISSING"
command -v xorriso &> /dev/null && echo "  xorriso: OK" || echo "  xorriso: MISSING"
command -v qemu-system-x86_64 &> /dev/null && echo "  qemu-system-x86_64: OK" || echo "  qemu-system-x86_64: MISSING (needed for testing)"

echo
echo "Done!"
