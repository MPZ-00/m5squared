@echo off
REM m5squared Windows Setup Script (Command Prompt version)
REM Run this from Command Prompt if you prefer cmd.exe over PowerShell

echo m5squared Windows Setup
echo ======================
echo.

echo [1/6] Checking Python installation...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo   ERROR: Python not found in PATH
    echo   Please install Python 3.12+ from python.org
    exit /b 1
)
python --version
echo   Python found

echo [2/6] Creating virtual environment...
if exist .venv (
    echo   Virtual environment already exists
) else (
    python -m venv .venv
    if %errorlevel% neq 0 (
        echo   ERROR: Failed to create virtual environment
        exit /b 1
    )
    echo   Virtual environment created
)

echo [3/6] Activating virtual environment...
call .venv\Scripts\activate.bat
echo   Virtual environment activated

echo [4/6] Upgrading pip...
python -m pip install --upgrade pip --quiet
echo   pip upgraded

echo [5/6] Installing project dependencies...
pip install -e . --quiet
if %errorlevel% neq 0 (
    echo   ERROR: Failed to install dependencies
    exit /b 1
)
echo   Dependencies installed

echo [6/6] Setting up .env file...
if exist .env (
    echo   .env file already exists
) else (
    if exist .env.example (
        copy .env.example .env >nul
        echo   Created .env from template
        echo   Please edit .env and fill in your wheel details
    ) else (
        echo   WARNING: .env.example not found
    )
)

echo.
echo Setup complete!
echo.
echo Next steps:
echo   1. Edit .env file: notepad .env
echo   2. Convert QR codes: python m25_qr_to_key.py "YourQRCodeString"
echo   3. Launch GUI: python m25_gui.py
echo.
echo For detailed documentation, see doc\windows-setup.md
echo.
pause
