#!/bin/bash
# m5squared Linux Setup Script
# Automates the initial setup process for Linux users.

set -euo pipefail

# Color output functions
info()    { echo -e "\033[0;36m$*\033[0m"; }
step()    { echo -e "\033[0;33m$*\033[0m"; }
ok()      { echo -e "\033[0;32m$*\033[0m"; }
warn()    { echo -e "\033[0;33m$*\033[0m"; }
fail()    { echo -e "\033[0;31m$*\033[0m" >&2; exit 1; }

# Command existence check
assert_command() {
    local cmd=$1
    local hint=$2
    if ! command -v "$cmd" &> /dev/null; then
        fail "ERROR: '$cmd' not found. $hint"
    fi
}

# Check Python version
get_python_version() {
    python3 --version 2>&1 | grep -oP 'Python \K[0-9]+\.[0-9]+\.[0-9]+'
}

check_python_version() {
    local version=$1
    local major minor
    major=$(echo "$version" | cut -d. -f1)
    minor=$(echo "$version" | cut -d. -f2)
    
    if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 8 ]; }; then
        fail "ERROR: Python 3.8+ required. Found: $version"
    fi
}

# Check if venv is healthy
test_venv_healthy() {
    local venv_path=$1
    [ -d "$venv_path" ] && \
    [ -f "$venv_path/bin/python" ] && \
    [ -f "$venv_path/pyvenv.cfg" ] && \
    [ -f "$venv_path/bin/activate" ]
}

# Remove broken venv
remove_venv() {
    local venv_path=$1
    if [ -d "$venv_path" ]; then
        warn "  Removing broken virtual environment: $venv_path"
        rm -rf "$venv_path"
    fi
}

# Ensure venv exists and is healthy
ensure_venv() {
    local venv_path=$1
    local force_recreate=${2:-false}
    
    if test_venv_healthy "$venv_path" && [ "$force_recreate" != "true" ]; then
        ok "  Virtual environment looks healthy"
        return
    fi
    
    if [ -d "$venv_path" ]; then
        warn "  Virtual environment exists but looks broken"
        remove_venv "$venv_path"
    fi
    
    step "  Creating virtual environment..."
    python3 -m venv "$venv_path"
    
    if ! test_venv_healthy "$venv_path"; then
        fail "ERROR: venv creation finished but environment is still invalid"
    fi
    ok "  Virtual environment created"
}

# Detect distribution for system dependencies
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    else
        echo "unknown"
    fi
}

# Check system dependencies
check_system_deps() {
    local distro=$(detect_distro)
    local missing_packages=()
    
    step "  Checking system dependencies..."
    
    # Check for tkinter
    if ! python3 -c "import tkinter" 2>/dev/null; then
        case "$distro" in
            ubuntu|debian|linuxmint)
                missing_packages+=("python3-tk")
                ;;
            fedora|rhel|centos|rocky|almalinux)
                missing_packages+=("python3-tkinter")
                ;;
            arch|manjaro)
                missing_packages+=("tk")
                ;;
            *)
                warn "  Cannot detect tkinter package name for your distribution"
                warn "  Please install the Python tkinter package manually"
                ;;
        esac
    fi
    
    # Check for Bluetooth development libraries
    if [ ! -f "/usr/include/bluetooth/bluetooth.h" ] && [ ! -f "/usr/local/include/bluetooth/bluetooth.h" ]; then
        case "$distro" in
            ubuntu|debian|linuxmint)
                missing_packages+=("libbluetooth-dev" "bluez")
                ;;
            fedora|rhel|centos|rocky|almalinux)
                missing_packages+=("bluez-libs-devel" "bluez")
                ;;
            arch|manjaro)
                missing_packages+=("bluez" "bluez-utils")
                ;;
            *)
                warn "  Cannot detect Bluetooth package names for your distribution"
                warn "  Please install Bluetooth development libraries manually"
                ;;
        esac
    fi
    
    if [ ${#missing_packages[@]} -gt 0 ]; then
        warn "  Missing system packages detected: ${missing_packages[*]}"
        warn ""
        case "$distro" in
            ubuntu|debian|linuxmint)
                warn "  Install with: sudo apt install ${missing_packages[*]}"
                ;;
            fedora|rhel|centos|rocky|almalinux)
                warn "  Install with: sudo dnf install ${missing_packages[*]}"
                ;;
            arch|manjaro)
                warn "  Install with: sudo pacman -S ${missing_packages[*]}"
                ;;
            *)
                warn "  Please install these packages using your distribution's package manager"
                ;;
        esac
        warn ""
        read -p "Continue without installing system packages? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            fail "Setup cancelled"
        fi
    else
        ok "  System dependencies OK"
    fi
}

# Parse arguments
FORCE_RECREATE=false
VENV_PATH=".venv"

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            cat << EOF
m5squared Linux Setup Script

Usage: ./setup-linux.sh [OPTIONS]

Options:
  -h, --help              Show this help message
  -f, --force-recreate    Force recreation of virtual environment
  -v, --venv-path PATH    Specify virtual environment path (default: .venv)

This script automates the setup process by:
  - Verifying Python 3.8+ installation
  - Checking system dependencies (tkinter, bluetooth libraries)
  - Creating and configuring a Python virtual environment
  - Installing all project dependencies
  - Setting up the .env configuration file

After successful setup:
  1. Edit .env file with your wheel details
  2. Convert QR code: python m25_qr_to_key.py 'YourQRCodeString'
  3. Launch GUI: python m25_gui.py

For more information, see:
  - doc/usage-setup.md
  - README.md
EOF
            exit 0
            ;;
        -f|--force-recreate)
            FORCE_RECREATE=true
            shift
            ;;
        -v|--venv-path)
            VENV_PATH="$2"
            shift 2
            ;;
        *)
            fail "Unknown option: $1. Use --help for usage information."
            ;;
    esac
done

info "m5squared Linux Setup"
info "====================="
echo ""

# [1/7] Python
step "[1/7] Checking Python installation..."
assert_command python3 "Install Python 3.8+ using your distribution's package manager."

PYVER=$(get_python_version)
ok "  Found: Python $PYVER"
check_python_version "$PYVER"

# [2/7] System dependencies
step "[2/7] Checking system dependencies..."
check_system_deps

# [3/7] Venv (self-healing)
step "[3/7] Ensuring virtual environment..."
ensure_venv "$VENV_PATH" "$FORCE_RECREATE"

# [4/7] Activation
step "[4/7] Activating virtual environment..."
# shellcheck disable=SC1091
source "$VENV_PATH/bin/activate"
ok "  Virtual environment activated"

# Always use venv python
VENV_PYTHON="$VENV_PATH/bin/python"

# [5/7] pip
step "[5/7] Upgrading pip..."
"$VENV_PYTHON" -m pip install --upgrade pip
ok "  pip upgraded"

# [6/7] deps
step "[6/7] Installing project dependencies..."
"$VENV_PYTHON" -m pip install -e ".[linux]"
ok "  Dependencies installed"

step "      Verifying installed packages..."
"$VENV_PYTHON" -m pip check
ok "  pip check passed"

# [7/7] .env
step "[7/7] Setting up .env file..."
if [ -f ".env" ]; then
    ok "  .env file already exists"
else
    if [ -f ".env.example" ]; then
        cp ".env.example" ".env"
        ok "  Created .env from template"
        warn "  Edit .env and fill in your wheel details"
    else
        warn "  .env.example not found"
    fi
fi

echo ""
ok "Setup complete!"
echo ""
info "Next steps:"
echo "  1. Edit .env:"
echo "     nano .env"
echo ""
echo "  2. Convert QR to key:"
echo "     $VENV_PYTHON m25_qr_to_key.py 'YourQRCodeString'"
echo ""
echo "  3. Launch GUI:"
echo "     $VENV_PYTHON m25_gui.py"
echo ""
echo "  CLI example:"
echo "     $VENV_PYTHON m25_ecs.py --dry-run"
echo ""
info "Docs:"
echo "  - doc/usage-setup.md"
echo "  - README.md"
echo ""
info "Note: You may need to run with sudo for Bluetooth access:"
echo "  sudo $VENV_PYTHON m25_gui.py"
echo ""
