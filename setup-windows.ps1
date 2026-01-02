# m5squared Windows Setup Script
# Automates the initial setup process for Windows users

Write-Host "m5squared Windows Setup" -ForegroundColor Cyan
Write-Host "======================" -ForegroundColor Cyan
Write-Host ""

# Check Python installation
Write-Host "[1/6] Checking Python installation..." -ForegroundColor Yellow
try {
    $pythonVersion = python --version 2>&1
    Write-Host "  Found: $pythonVersion" -ForegroundColor Green
    
    # Check if version is 3.8+
    if ($pythonVersion -match "Python (\d+)\.(\d+)") {
        $major = [int]$matches[1]
        $minor = [int]$matches[2]
        if ($major -lt 3 -or ($major -eq 3 -and $minor -lt 8)) {
            Write-Error "  ERROR: Python 3.8+ required, found $major.$minor" -ForegroundColor Red
            exit 1
        }
    }
} catch {
    Write-Host "  ERROR: Python not found in PATH" -ForegroundColor Red
    Write-Host "  Please install Python 3.12+ from python.org" -ForegroundColor Red
    exit 1
}

# Create virtual environment
Write-Host "[2/6] Creating virtual environment..." -ForegroundColor Yellow
if (Test-Path ".venv") {
    Write-Host "  Virtual environment already exists" -ForegroundColor Green
} else {
    python -m venv .venv
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  Virtual environment created" -ForegroundColor Green
    } else {
        Write-Host "  ERROR: Failed to create virtual environment" -ForegroundColor Red
        exit 1
    }
}

# Activate virtual environment
Write-Host "[3/6] Activating virtual environment..." -ForegroundColor Yellow
$activateScript = ".\.venv\Scripts\Activate.ps1"

if (Test-Path $activateScript) {
    # Check execution policy
    $policy = Get-ExecutionPolicy -Scope Process
    if ($policy -eq "Restricted" -or $policy -eq "AllSigned") {
        Write-Host "  Setting execution policy for this session..." -ForegroundColor Yellow
        Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    }
    
    & $activateScript
    Write-Host "  Virtual environment activated" -ForegroundColor Green
} else {
    Write-Host "  ERROR: Activation script not found" -ForegroundColor Red
    exit 1
}

# Upgrade pip
Write-Host "[4/6] Upgrading pip..." -ForegroundColor Yellow
python -m pip install --upgrade pip --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "  pip upgraded" -ForegroundColor Green
} else {
    Write-Host "  WARNING: pip upgrade failed (continuing anyway)" -ForegroundColor Yellow
}

# Install project dependencies
Write-Host "[5/6] Installing project dependencies..." -ForegroundColor Yellow
pip install -e . --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "  Dependencies installed" -ForegroundColor Green
} else {
    Write-Host "  ERROR: Failed to install dependencies" -ForegroundColor Red
    exit 1
}

# Create .env file if it doesn't exist
Write-Host "[6/6] Setting up .env file..." -ForegroundColor Yellow
if (Test-Path ".env") {
    Write-Host "  .env file already exists" -ForegroundColor Green
} else {
    if (Test-Path ".env.example") {
        Copy-Item ".env.example" ".env"
        Write-Host "  Created .env from template" -ForegroundColor Green
        Write-Host "  Please edit .env and fill in your wheel details" -ForegroundColor Yellow
    } else {
        Write-Host "  WARNING: .env.example not found" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Setup complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Edit .env file with your wheel MAC addresses and keys:" -ForegroundColor White
Write-Host "     notepad .env" -ForegroundColor Gray
Write-Host ""
Write-Host "  2. Convert QR codes to encryption keys:" -ForegroundColor White
Write-Host "     python m25_qr_to_key.py 'YourQRCodeString'" -ForegroundColor Gray
Write-Host ""
Write-Host "  3. Launch the GUI interface:" -ForegroundColor White
Write-Host "     python m25_gui.py" -ForegroundColor Gray
Write-Host ""
Write-Host "  Or use command line tools:" -ForegroundColor White
Write-Host "     python m25_ecs.py --dry-run" -ForegroundColor Gray
Write-Host ""
Write-Host "For detailed documentation, see:" -ForegroundColor Cyan
Write-Host "  - doc/windows-setup.md" -ForegroundColor White
Write-Host "  - README.md" -ForegroundColor White
Write-Host ""
