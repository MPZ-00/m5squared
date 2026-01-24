<#
.SYNOPSIS
m5squared Windows Setup Script - Automates the initial setup process for Windows users.

.DESCRIPTION
This script automates the complete setup process for the m5squared project on Windows systems.
It performs the following tasks:
- Verifies Python 3.8+ installation
- Creates and configures a Python virtual environment
- Upgrades pip to the latest version
- Installs all project dependencies
- Sets up the .env configuration file from template
- Validates the installation with pip check

The script includes self-healing capabilities for broken virtual environments and
automatically handles execution policy requirements for virtual environment activation.

.PARAMETER VenvPath
Specifies the path where the virtual environment should be created.
Default: ".venv"

.PARAMETER ForceRecreateVenv
Forces recreation of the virtual environment even if it appears healthy.
This is useful when you want to start with a clean environment or if you
suspect the environment is corrupted despite passing health checks.

.EXAMPLE
.\setup-windows.ps1
Runs the setup with default settings, creating a virtual environment in .venv

.EXAMPLE
.\setup-windows.ps1 -VenvPath "venv"
Creates the virtual environment in a folder named "venv" instead of ".venv"

.EXAMPLE
.\setup-windows.ps1 -ForceRecreateVenv
Forces complete recreation of the virtual environment, even if it exists and is healthy

.EXAMPLE
.\setup-windows.ps1 -VenvPath "myenv" -ForceRecreateVenv
Creates a fresh virtual environment in the "myenv" folder

.NOTES
Requirements:
- Python 3.8 or higher installed and available in PATH
- Internet connection for downloading packages
- Sufficient disk space for virtual environment and dependencies

After successful setup:
1. Edit .env file with your wheel details
2. Convert QR code: python m25_qr_to_key.py 'YourQRCodeString'
3. Launch GUI: python m25_gui.py

For more information, see:
- doc/windows-setup.md
- README.md

.LINK
https://github.com/MPZ-00/m5squared

.LINK
https://github.com/roll2own/m5squared
#>

[CmdletBinding()]
param(
    [switch]$Help,
    [string]$VenvPath = ".venv",
    [switch]$ForceRecreateVenv
)

# Handle legacy help flags (-help, --help, /?, -?, etc.)
if ($Help -or $args -match '^(--?help|/\?|-\?)$') {
    Get-Help $PSCommandPath
    exit 0
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Info($msg)    { Write-Host $msg -ForegroundColor Cyan }
function Step($msg)    { Write-Host $msg -ForegroundColor Yellow }
function Ok($msg)      { Write-Host $msg -ForegroundColor Green }
function Warn($msg)    { Write-Host $msg -ForegroundColor Yellow }
function Fail($msg)    { Write-Host $msg -ForegroundColor Red; exit 1 }

function Assert-Command($name, $hint) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Fail "ERROR: '$name' not found. $hint"
    }
}

function Get-PythonVersion {
    $out = & python --version 2>&1
    if ($out -notmatch 'Python (\d+)\.(\d+)\.(\d+)') {
        Fail "ERROR: Could not parse python version: $out"
    }
    [pscustomobject]@{
        Raw   = $out
        Major = [int]$matches[1]
        Minor = [int]$matches[2]
        Patch = [int]$matches[3]
    }
}

function Test-VenvHealthy($VenvPath) {
    $py  = Join-Path $VenvPath 'Scripts\python.exe'
    $cfg = Join-Path $VenvPath 'pyvenv.cfg'
    $act = Join-Path $VenvPath 'Scripts\Activate.ps1'
    return (Test-Path $VenvPath) -and (Test-Path $py) -and (Test-Path $cfg) -and (Test-Path $act)
}

function Remove-Venv($VenvPath) {
    if (Test-Path $VenvPath) {
        Warn "  Removing broken virtual environment: $VenvPath"
        Remove-Item -Recurse -Force $VenvPath
    }
}

function Ensure-Venv($VenvPath) {
    if (Test-VenvHealthy $VenvPath -and -not $ForceRecreateVenv) {
        Ok "  Virtual environment looks healthy"
        return
    }

    if (Test-Path $VenvPath) {
        Warn "  Virtual environment exists but looks broken"
        Remove-Venv $VenvPath
    }

    Step "  Creating virtual environment..."
    & python -m venv $VenvPath
    if (-not (Test-VenvHealthy $VenvPath)) {
        Fail "ERROR: venv creation finished but environment is still invalid"
    }
    Ok "  Virtual environment created"
}

function Ensure-ExecutionPolicyBypassForSession {
    $policy = Get-ExecutionPolicy -Scope Process
    if ($policy -eq 'Restricted' -or $policy -eq 'AllSigned') {
        Warn "  Setting execution policy for this session (Process scope)"
        Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    }
}

Info "m5squared Windows Setup"
Info "======================="
Write-Host ""

# [1/6] Python
Step "[1/6] Checking Python installation..."
Assert-Command python "Install Python 3.12+ from python.org and ensure it is in PATH."

$pyver = Get-PythonVersion
Ok "  Found: $($pyver.Raw)"
if ($pyver.Major -lt 3 -or ($pyver.Major -eq 3 -and $pyver.Minor -lt 8)) {
    Fail "ERROR: Python 3.8+ required. Found: $($pyver.Major).$($pyver.Minor).$($pyver.Patch)"
}

# [2/6] Venv (self-healing)
Step "[2/6] Ensuring virtual environment..."
$venv = ".venv"
Ensure-Venv $venv

# [3/6] Activation (optional) + execution policy
Step "[3/6] Activating virtual environment (optional)..."
$activateScript = Join-Path $venv "Scripts\Activate.ps1"
Ensure-ExecutionPolicyBypassForSession
try {
    & $activateScript
    Ok "  Virtual environment activated"
} catch {
    Warn "  Activation failed. Continuing using venv python directly."
}

# Always use venv python for installs to avoid relying on activation
$VenvPython = Join-Path $venv "Scripts\python.exe"

# [4/6] pip
Step "[4/6] Upgrading pip..."
& $VenvPython -m pip install --upgrade pip
Ok "  pip upgraded"

# [5/6] deps
Step "[5/6] Installing project dependencies..."
& $VenvPython -m pip install -e ".[windows]"
Ok "  Dependencies installed"

Step "      Verifying installed packages..."
& $VenvPython -m pip check
Ok "  pip check passed"

# [6/6] .env
Step "[6/6] Setting up .env file..."
if (Test-Path ".env") {
    Ok "  .env file already exists"
} else {
    if (Test-Path ".env.example") {
        Copy-Item ".env.example" ".env"
        Ok "  Created .env from template"
        Warn "  Edit .env and fill in your wheel details"
    } else {
        Warn "  .env.example not found"
    }
}

Write-Host ""
Ok "Setup complete!"
Write-Host ""
Info "Next steps:"
Write-Host "  1. Edit .env:" -ForegroundColor White
Write-Host "     notepad .env" -ForegroundColor Gray
Write-Host ""
Write-Host "  2. Convert QR to key:" -ForegroundColor White
Write-Host "     $VenvPython m25_qr_to_key.py 'YourQRCodeString'" -ForegroundColor Gray
Write-Host ""
Write-Host "  3. Launch GUI:" -ForegroundColor White
Write-Host "     $VenvPython m25_gui.py" -ForegroundColor Gray
Write-Host ""
Write-Host "  CLI example:" -ForegroundColor White
Write-Host "     $VenvPython m25_ecs.py --dry-run" -ForegroundColor Gray
Write-Host ""
Info "Docs:"
Write-Host "  - doc/windows-setup.md" -ForegroundColor White
Write-Host "  - README.md" -ForegroundColor White
Write-Host ""
