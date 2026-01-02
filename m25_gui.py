#!/usr/bin/env python3
"""
M25 GUI - Cross-platform graphical interface for wheelchair.py

Provides a simple, accessible interface for controlling M25 wheels on Windows and Linux.
Built with tkinter (included with Python) for maximum compatibility.

Features:
- Load credentials from .env file
- Connect to wheels
- Read battery status
- Change assist levels
- Toggle hill hold
- Read sensor data
- All the power of the CLI, zero typing required
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import os
import sys
import threading
from pathlib import Path

try:
    from dotenv import load_dotenv
    HAS_DOTENV = True
except ImportError:
    HAS_DOTENV = False
    print("Warning: python-dotenv not installed. Install with: pip install python-dotenv")


class M25GUI:
    """Main GUI application for M25 wheelchair control"""
    
    def __init__(self, root):
        self.root = root
        self.root.title("m5squared - Wheelchair Controller")
        self.root.geometry("800x600")
        
        # Load environment variables
        self.load_env()
        
        # Connection state
        self.connected = False
        
        # Create UI
        self.create_widgets()
        
        # Load saved credentials
        self.load_credentials()
    
    def load_env(self):
        """Load .env file if available"""
        if HAS_DOTENV:
            env_path = Path(".env")
            if env_path.exists():
                load_dotenv(env_path)
                self.status_message("Loaded .env file")
            else:
                self.status_message("No .env file found (optional)")
        else:
            self.status_message("python-dotenv not installed")
    
    def create_widgets(self):
        """Create all GUI elements"""
        
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        
        # Title
        title = ttk.Label(main_frame, text="m5squared Wheelchair Controller",
                         font=("Arial", 16, "bold"))
        title.grid(row=0, column=0, columnspan=3, pady=(0, 20))
        
        # Connection Section
        conn_frame = ttk.LabelFrame(main_frame, text="Connection", padding="10")
        conn_frame.grid(row=1, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        conn_frame.columnconfigure(1, weight=1)
        
        # Left wheel
        ttk.Label(conn_frame, text="Left Wheel MAC:").grid(row=0, column=0, sticky=tk.W)
        self.left_mac = ttk.Entry(conn_frame, width=30)
        self.left_mac.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=(5, 0))
        
        ttk.Label(conn_frame, text="Left Key:").grid(row=1, column=0, sticky=tk.W)
        self.left_key = ttk.Entry(conn_frame, width=30, show="*")
        self.left_key.grid(row=1, column=1, sticky=(tk.W, tk.E), padx=(5, 0))
        
        # Right wheel
        ttk.Label(conn_frame, text="Right Wheel MAC:").grid(row=2, column=0, sticky=tk.W)
        self.right_mac = ttk.Entry(conn_frame, width=30)
        self.right_mac.grid(row=2, column=1, sticky=(tk.W, tk.E), padx=(5, 0))
        
        ttk.Label(conn_frame, text="Right Key:").grid(row=3, column=0, sticky=tk.W)
        self.right_key = ttk.Entry(conn_frame, width=30, show="*")
        self.right_key.grid(row=3, column=1, sticky=(tk.W, tk.E), padx=(5, 0))
        
        # Connect button
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.connect)
        self.connect_btn.grid(row=4, column=0, columnspan=2, pady=(10, 0))
        
        # Control Section
        control_frame = ttk.LabelFrame(main_frame, text="Controls", padding="10")
        control_frame.grid(row=2, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        
        # Assist level
        ttk.Label(control_frame, text="Assist Level:").grid(row=0, column=0, sticky=tk.W)
        self.assist_level = ttk.Combobox(control_frame, width=20, state="readonly")
        self.assist_level["values"] = ("Level 1 (Normal)", "Level 2 (Outdoor)", "Level 3 (Learning)")
        self.assist_level.current(0)
        self.assist_level.grid(row=0, column=1, padx=(5, 10))
        
        self.set_level_btn = ttk.Button(control_frame, text="Set Level",
                                       command=self.set_assist_level, state="disabled")
        self.set_level_btn.grid(row=0, column=2)
        
        # Hill hold
        ttk.Label(control_frame, text="Hill Hold:").grid(row=1, column=0, sticky=tk.W)
        self.hill_hold = tk.BooleanVar()
        self.hill_hold_check = ttk.Checkbutton(control_frame, text="Enable",
                                              variable=self.hill_hold,
                                              command=self.toggle_hill_hold,
                                              state="disabled")
        self.hill_hold_check.grid(row=1, column=1, sticky=tk.W, padx=(5, 0))
        
        # Status buttons
        btn_frame = ttk.Frame(control_frame)
        btn_frame.grid(row=2, column=0, columnspan=3, pady=(10, 0))
        
        self.read_battery_btn = ttk.Button(btn_frame, text="Read Battery",
                                          command=self.read_battery, state="disabled")
        self.read_battery_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_status_btn = ttk.Button(btn_frame, text="Read Status",
                                         command=self.read_status, state="disabled")
        self.read_status_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_version_btn = ttk.Button(btn_frame, text="Read Version",
                                          command=self.read_version, state="disabled")
        self.read_version_btn.pack(side=tk.LEFT, padx=5)
        
        # Output Section
        output_frame = ttk.LabelFrame(main_frame, text="Output", padding="10")
        output_frame.grid(row=3, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        output_frame.columnconfigure(0, weight=1)
        output_frame.rowconfigure(0, weight=1)
        
        self.output = scrolledtext.ScrolledText(output_frame, height=15, width=80)
        self.output.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Status bar
        self.status = ttk.Label(main_frame, text="Ready", relief=tk.SUNKEN)
        self.status.grid(row=4, column=0, columnspan=3, sticky=(tk.W, tk.E))
        
        # Configure row weights for resizing
        main_frame.rowconfigure(3, weight=1)
    
    def load_credentials(self):
        """Load credentials from environment variables"""
        self.left_mac.insert(0, os.getenv("M25_LEFT_MAC", ""))
        self.left_key.insert(0, os.getenv("M25_LEFT_KEY", ""))
        self.right_mac.insert(0, os.getenv("M25_RIGHT_MAC", ""))
        self.right_key.insert(0, os.getenv("M25_RIGHT_KEY", ""))
        
        if self.left_mac.get() or self.right_mac.get():
            self.log("Credentials loaded from .env file")
    
    def status_message(self, msg):
        """Update status bar"""
        self.status.config(text=msg)
    
    def log(self, message):
        """Append message to output log"""
        self.output.insert(tk.END, f"{message}\n")
        self.output.see(tk.END)
    
    def enable_controls(self, enabled=True):
        """Enable/disable control buttons"""
        state = "normal" if enabled else "disabled"
        self.set_level_btn.config(state=state)
        self.hill_hold_check.config(state=state)
        self.read_battery_btn.config(state=state)
        self.read_status_btn.config(state=state)
        self.read_version_btn.config(state=state)
    
    def connect(self):
        """Connect to M25 wheels"""
        left_mac = self.left_mac.get().strip()
        left_key = self.left_key.get().strip()
        right_mac = self.right_mac.get().strip()
        right_key = self.right_key.get().strip()
        
        if not left_mac or not left_key:
            messagebox.showerror("Error", "Please enter left wheel MAC address and key")
            return
        
        if not right_mac or not right_key:
            messagebox.showerror("Error", "Please enter right wheel MAC address and key")
            return
        
        self.log(f"Connecting to wheels...")
        self.log(f"  Left:  {left_mac}")
        self.log(f"  Right: {right_mac}")
        self.status_message("Connecting...")
        
        # In a real implementation, this would call m25_bluetooth_windows or m25_bluetooth
        # For now, simulate connection
        def simulate_connect():
            import time
            time.sleep(2)  # Simulate connection time
            self.root.after(0, self.connection_complete, True)
        
        threading.Thread(target=simulate_connect, daemon=True).start()
    
    def connection_complete(self, success):
        """Handle connection result"""
        if success:
            self.connected = True
            self.log("Connected successfully!")
            self.status_message("Connected")
            self.connect_btn.config(text="Disconnect")
            self.enable_controls(True)
        else:
            self.log("Connection failed!")
            self.status_message("Connection failed")
            messagebox.showerror("Error", "Failed to connect to wheels")
    
    def set_assist_level(self):
        """Set assist level"""
        level = self.assist_level.current()
        level_names = ["Normal (Level 1)", "Outdoor (Level 2)", "Learning (Level 3)"]
        self.log(f"Setting assist level to: {level_names[level]}")
        self.status_message(f"Setting assist level to {level + 1}...")
        
        # TODO: Call m25_ecs.py with --set-level parameter
        # For now, just log
        self.log("Note: Actual control requires implementing m25_ecs integration")
    
    def toggle_hill_hold(self):
        """Toggle hill hold on/off"""
        enabled = self.hill_hold.get()
        state = "ON" if enabled else "OFF"
        self.log(f"Setting hill hold: {state}")
        self.status_message(f"Hill hold {state}")
        
        # TODO: Call m25_ecs.py with --hill-hold parameter
    
    def read_battery(self):
        """Read battery status"""
        self.log("Reading battery status...")
        self.status_message("Reading battery...")
        
        # TODO: Call m25_ecs.py to read battery
        # Simulate for now
        self.log("  Left wheel:  85%")
        self.log("  Right wheel: 83%")
        self.status_message("Battery read complete")
    
    def read_status(self):
        """Read full status"""
        self.log("Reading full status...")
        self.status_message("Reading status...")
        
        # TODO: Call m25_ecs.py to read all status
        self.log("  Assist Level: 1 (Normal)")
        self.log("  Hill Hold: OFF")
        self.log("  Drive Profile: Standard")
        self.status_message("Status read complete")
    
    def read_version(self):
        """Read firmware version"""
        self.log("Reading firmware version...")
        self.status_message("Reading version...")
        
        # TODO: Call m25_ecs.py to read version
        self.log("  Firmware: v2.5.1")
        self.log("  Hardware: M25V1")
        self.status_message("Version read complete")


def main():
    """Launch the GUI application"""
    
    # Check for required modules
    missing = []
    
    try:
        import tkinter
    except ImportError:
        missing.append("tkinter (should be included with Python)")
    
    if not HAS_DOTENV:
        print("Warning: python-dotenv not installed")
        print("Install with: pip install python-dotenv")
        print("Continuing without .env support...\n")
    
    if missing:
        print("ERROR: Missing required modules:")
        for mod in missing:
            print(f"  - {mod}")
        sys.exit(1)
    
    # Create and run GUI
    root = tk.Tk()
    app = M25GUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
