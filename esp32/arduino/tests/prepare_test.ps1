#!/usr/bin/env pwsh
# Prepare test environment by merging remote_control files with test
# Creates a temporary build directory that can be opened in Arduino IDE

param(
    [Parameter(Mandatory=$true)]
    [string]$TestName
)

$ErrorActionPreference = "Stop"

$testDir = Join-Path $PSScriptRoot $TestName
$remoteControlDir = Join-Path $PSScriptRoot "..\remote_control"
$buildDir = Join-Path $PSScriptRoot "_build_$TestName"

if (-not (Test-Path $testDir)) {
    Write-Host "ERROR: Test directory not found: $testDir" -ForegroundColor Red
    exit 1
}

$testIno = Join-Path $testDir "$TestName.ino"
if (-not (Test-Path $testIno)) {
    Write-Host "ERROR: Test sketch not found: $testIno" -ForegroundColor Red
    exit 1
}

Write-Host "Preparing test environment for: $TestName" -ForegroundColor Cyan
Write-Host ""

# Create or clean build directory
if (Test-Path $buildDir) {
    Write-Host "Cleaning existing build directory..." -ForegroundColor Yellow
    Remove-Item $buildDir\* -Recurse -Force
} else {
    Write-Host "Creating build directory: $buildDir" -ForegroundColor Green
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

# Copy all remote_control files EXCEPT .ino files
Write-Host "Copying remote_control files..." -ForegroundColor Yellow
Get-ChildItem $remoteControlDir -File | Where-Object { $_.Extension -ne ".ino" } | ForEach-Object {
    Copy-Item $_.FullName $buildDir -Force
    Write-Host "  - $($_.Name)" -ForegroundColor DarkGray
}

# Copy test .ino file (rename to match directory)
$buildIno = Join-Path $buildDir "_build_$TestName.ino"
Copy-Item $testIno $buildIno -Force
Write-Host "  - $TestName.ino -> _build_$TestName.ino" -ForegroundColor Green

Write-Host ""
Write-Host "Build directory ready: $buildDir" -ForegroundColor Green
Write-Host ""

# Check if arduino alias exists and launch
$arduinoCmd = Get-Command "arduino" -ErrorAction SilentlyContinue
if ($arduinoCmd) {
    Write-Host "Launching Arduino IDE..." -ForegroundColor Green
    $inoPath = Join-Path $buildDir "_build_$TestName.ino"
    & arduino $inoPath
} else {
    Write-Host "To test in Arduino IDE:" -ForegroundColor Cyan
    Write-Host "  arduino _build_$TestName\_build_$TestName.ino" -ForegroundColor White
    Write-Host ""
    Write-Host "NOTE: 'arduino' alias not set. To create:" -ForegroundColor Yellow
    Write-Host "  Set-Alias -Name arduino -Value 'C:\Path\To\Arduino IDE.exe'" -ForegroundColor DarkGray
    Write-Host ""
}

Write-Host "Build directory is reused and excluded from git" -ForegroundColor DarkGray
