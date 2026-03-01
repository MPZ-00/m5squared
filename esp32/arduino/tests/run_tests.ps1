#!/usr/bin/env pwsh
# Automated test runner for ESP32 Arduino tests
# Requires: Arduino CLI (arduino-cli)

param(
    [string]$Board = "esp32:esp32:esp32",
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  ESP32 Arduino Test Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if arduino-cli is installed
if (-not (Get-Command "arduino-cli" -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: arduino-cli not found!" -ForegroundColor Red
    Write-Host "Install: winget install Arduino.ArduinoCLI" -ForegroundColor Yellow
    exit 1
}

# Ensure ESP32 core is installed
Write-Host "Checking ESP32 core..." -ForegroundColor Yellow
arduino-cli core update-index
arduino-cli core install esp32:esp32 2>$null

# Find all test sketches
$testDirs = Get-ChildItem -Path . -Directory -Filter "test_*"

if ($testDirs.Count -eq 0) {
    Write-Host "No test directories found!" -ForegroundColor Red
    exit 1
}

$totalTests = 0
$passedTests = 0
$failedTests = 0

foreach ($testDir in $testDirs) {
    $testName = $testDir.Name
    $sketchPath = Join-Path $testDir.FullName "$testName.ino"
    
    if (-not (Test-Path $sketchPath)) {
        Write-Host "WARNING: Sketch not found: $sketchPath" -ForegroundColor Yellow
        continue
    }
    
    Write-Host ""
    Write-Host "======================================== " -ForegroundColor Cyan
    Write-Host " Testing: $testName" -ForegroundColor Cyan
    Write-Host "======================================== " -ForegroundColor Cyan
    
    # Compile
    Write-Host "Compiling..." -ForegroundColor Yellow
    $compileResult = arduino-cli compile --fqbn $Board $testDir.FullName 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "COMPILE FAILED!" -ForegroundColor Red
        Write-Host $compileResult
        $failedTests++
        continue
    }
    
    Write-Host "Compile OK" -ForegroundColor Green
    
    # Ask if user wants to upload and run
    $upload = Read-Host "Upload and run? (y/N)"
    
    if ($upload -eq "y" -or $upload -eq "Y") {
        Write-Host "Uploading to $Port..." -ForegroundColor Yellow
        arduino-cli upload -p $Port --fqbn $Board $testDir.FullName
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Upload OK" -ForegroundColor Green
            Write-Host ""
            Write-Host "Open Serial Monitor to view test results:" -ForegroundColor Yellow
            Write-Host "  arduino-cli monitor -p $Port -c baudrate=115200" -ForegroundColor Cyan
            Write-Host ""
            
            # Optionally open monitor automatically
            $monitor = Read-Host "Open serial monitor? (Y/n)"
            if ($monitor -ne "n" -and $monitor -ne "N") {
                arduino-cli monitor -p $Port -c baudrate=115200
            }
            
            $passedTests++
        } else {
            Write-Host "Upload FAILED!" -ForegroundColor Red
            $failedTests++
        }
    } else {
        Write-Host "Skipped upload" -ForegroundColor Yellow
        $passedTests++
    }
    
    $totalTests++
}

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total:  $totalTests" -ForegroundColor White
Write-Host "Passed: $passedTests" -ForegroundColor Green
Write-Host "Failed: $failedTests" -ForegroundColor Red

if ($failedTests -eq 0) {
    Write-Host ""
    Write-Host "ALL TESTS PASSED!" -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "SOME TESTS FAILED!" -ForegroundColor Red
    exit 1
}
