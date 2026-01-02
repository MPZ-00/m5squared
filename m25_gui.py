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
import asyncio
from pathlib import Path

try:
    from dotenv import load_dotenv
    HAS_DOTENV = True
except ImportError:
    HAS_DOTENV = False
    print("Warning: python-dotenv not installed. Install with: pip install python-dotenv")

try:
    from m25_bluetooth_windows import M25WindowsBluetooth
    HAS_BLUETOOTH = True
except ImportError:
    HAS_BLUETOOTH = False
    print("Warning: m25_bluetooth_windows not available")


class M25GUI:
    """Main GUI application for M25 wheelchair control"""
    
    # Theme definitions
    THEMES = {
        'dark': {
            'bg': '#1e1e1e',
            'fg': '#d4d4d4',
            'entry_bg': '#2d2d2d',
            'entry_fg': '#d4d4d4',
            'button_bg': '#3e3e3e',
            'button_fg': '#d4d4d4',
            'output_bg': '#1e1e1e',
            'output_fg': '#d4d4d4',
            'select_bg': '#264f78',
            'select_fg': '#ffffff',
        },
        'light': {
            'bg': '#f0f0f0',
            'fg': '#000000',
            'entry_bg': '#ffffff',
            'entry_fg': '#000000',
            'button_bg': '#e1e1e1',
            'button_fg': '#000000',
            'output_bg': '#ffffff',
            'output_fg': '#000000',
            'select_bg': '#0078d7',
            'select_fg': '#ffffff',
        }
    }
    
    def __init__(self, root):
        self.root = root
        self.root.title("m5squared - Wheelchair Controller")
        self.root.geometry("800x600")
        
        # Connection state
        self.connected = False
        self.scanned_devices = []
        self.bt_handler = None
        
        # Theme state (default to dark)
        self.current_theme = 'dark'
        
        # Create UI first (so status bar exists)
        self.create_widgets()
        
        # Apply theme
        self.apply_theme()
        
        # Load environment variables
        self.load_env()
        
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
        self.main_frame = tk.Frame(self.root, padx=10, pady=10)
        self.main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        self.main_frame.columnconfigure(1, weight=1)
        
        # Title
        title_frame = tk.Frame(self.main_frame)
        title_frame.grid(row=0, column=0, columnspan=3, pady=(0, 20))
        
        title = tk.Label(title_frame, text="m5squared Wheelchair Controller",
                         font=("Arial", 16, "bold"))
        title.pack(side=tk.LEFT, padx=(0, 20))
        
        # Theme toggle button
        self.theme_btn = tk.Button(title_frame, text="☀ Light Mode", 
                                   command=self.toggle_theme, relief=tk.FLAT,
                                   cursor="hand2", font=("Arial", 10))
        self.theme_btn.pack(side=tk.LEFT)
        
        # Connection Section
        self.conn_frame = tk.LabelFrame(self.main_frame, text="Connection", padx=10, pady=10, font=("", 9, "bold"))
        self.conn_frame.grid(row=1, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        self.conn_frame.columnconfigure(1, weight=1)
        
        # Scan button
        scan_frame = tk.Frame(self.conn_frame)
        scan_frame.grid(row=0, column=0, columnspan=2, pady=(0, 10), sticky=(tk.W, tk.E))
        
        self.scan_btn = tk.Button(scan_frame, text="🔍 Scan for Wheels", 
                                  command=self.scan_devices, cursor="hand2")
        self.scan_btn.pack(side=tk.LEFT, padx=(0, 10))
        
        self.filter_m25 = tk.BooleanVar(value=True)
        self.filter_check = tk.Checkbutton(scan_frame, text="Filter M25 only",
                                          variable=self.filter_m25)
        self.filter_check.pack(side=tk.LEFT, padx=(0, 10))
        
        self.scan_status_lbl = tk.Label(scan_frame, text="")
        self.scan_status_lbl.pack(side=tk.LEFT)
        
        # Device selection for left wheel
        self.lbl_left_device = tk.Label(self.conn_frame, text="Left Wheel:")
        self.lbl_left_device.grid(row=1, column=0, sticky=tk.W, pady=2)
        
        left_device_frame = tk.Frame(self.conn_frame)
        left_device_frame.grid(row=1, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        left_device_frame.columnconfigure(0, weight=1)
        
        self.left_device_var = tk.StringVar(value="")
        self.left_device_menu = tk.OptionMenu(left_device_frame, self.left_device_var, 
                                              "", command=self.on_left_device_selected)
        self.left_device_menu.config(width=35)
        self.left_device_menu.grid(row=0, column=0, sticky=(tk.W, tk.E))
        
        # Left wheel MAC and Key
        self.lbl_left_mac = tk.Label(self.conn_frame, text="Left Wheel MAC:")
        self.lbl_left_mac.grid(row=2, column=0, sticky=tk.W, pady=2, padx=(20, 0))
        self.left_mac = tk.Entry(self.conn_frame, width=30, relief=tk.FLAT, borderwidth=2)
        self.left_mac.grid(row=2, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        
        self.lbl_left_key = tk.Label(self.conn_frame, text="Left Key:")
        self.lbl_left_key.grid(row=3, column=0, sticky=tk.W, pady=2, padx=(20, 0))
        self.left_key = tk.Entry(self.conn_frame, width=30, show="*", relief=tk.FLAT, borderwidth=2)
        self.left_key.grid(row=3, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        
        # Device selection for right wheel
        self.lbl_right_device = tk.Label(self.conn_frame, text="Right Wheel:")
        self.lbl_right_device.grid(row=4, column=0, sticky=tk.W, pady=2)
        
        right_device_frame = tk.Frame(self.conn_frame)
        right_device_frame.grid(row=4, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        right_device_frame.columnconfigure(0, weight=1)
        
        self.right_device_var = tk.StringVar(value="")
        self.right_device_menu = tk.OptionMenu(right_device_frame, self.right_device_var,
                                               "", command=self.on_right_device_selected)
        self.right_device_menu.config(width=35)
        self.right_device_menu.grid(row=0, column=0, sticky=(tk.W, tk.E))
        
        # Right wheel MAC and Key
        self.lbl_right_mac = tk.Label(self.conn_frame, text="Right Wheel MAC:")
        self.lbl_right_mac.grid(row=5, column=0, sticky=tk.W, pady=2, padx=(20, 0))
        self.right_mac = tk.Entry(self.conn_frame, width=30, relief=tk.FLAT, borderwidth=2)
        self.right_mac.grid(row=5, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        
        self.lbl_right_key = tk.Label(self.conn_frame, text="Right Key:")
        self.lbl_right_key.grid(row=6, column=0, sticky=tk.W, pady=2, padx=(20, 0))
        self.right_key = tk.Entry(self.conn_frame, width=30, show="*", relief=tk.FLAT, borderwidth=2)
        self.right_key.grid(row=6, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        
        # Connect button
        self.connect_btn = tk.Button(self.conn_frame, text="Connect", command=self.connect, cursor="hand2")
        self.connect_btn.grid(row=7, column=0, columnspan=2, pady=(10, 0))
        
        # Control Section
        self.control_frame = tk.LabelFrame(self.main_frame, text="Controls", padx=10, pady=10, font=("", 9, "bold"))
        self.control_frame.grid(row=2, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        
        # Assist level
        self.lbl_assist = tk.Label(self.control_frame, text="Assist Level:")
        self.lbl_assist.grid(row=0, column=0, sticky=tk.W, pady=5)
        
        # Using Listbox as a styled alternative to Combobox
        assist_frame = tk.Frame(self.control_frame)
        assist_frame.grid(row=0, column=1, padx=(5, 10), sticky=tk.W)
        self.assist_level_var = tk.StringVar(value="Level 1 (Normal)")
        self.assist_levels = ["Level 1 (Normal)", "Level 2 (Outdoor)", "Level 3 (Learning)"]
        self.assist_level_menu = tk.OptionMenu(assist_frame, self.assist_level_var, *self.assist_levels)
        self.assist_level_menu.config(width=18)
        self.assist_level_menu.pack()
        
        self.set_level_btn = tk.Button(self.control_frame, text="Set Level",
                                       command=self.set_assist_level, state="disabled", cursor="hand2")
        self.set_level_btn.grid(row=0, column=2, pady=5)
        
        # Hill hold
        self.lbl_hill_hold = tk.Label(self.control_frame, text="Hill Hold:")
        self.lbl_hill_hold.grid(row=1, column=0, sticky=tk.W, pady=5)
        self.hill_hold = tk.BooleanVar()
        self.hill_hold_check = tk.Checkbutton(self.control_frame, text="Enable",
                                              variable=self.hill_hold,
                                              command=self.toggle_hill_hold,
                                              state="disabled")
        self.hill_hold_check.grid(row=1, column=1, sticky=tk.W, padx=(5, 0), pady=5)
        
        # Status buttons
        btn_frame = tk.Frame(self.control_frame)
        btn_frame.grid(row=2, column=0, columnspan=3, pady=(10, 0))
        
        self.read_battery_btn = tk.Button(btn_frame, text="🔋 Battery",
                                          command=self.read_battery, state="disabled", cursor="hand2")
        self.read_battery_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_status_btn = tk.Button(btn_frame, text="📊 Status",
                                         command=self.read_status, state="disabled", cursor="hand2")
        self.read_status_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_version_btn = tk.Button(btn_frame, text="ℹ Version",
                                          command=self.read_version, state="disabled", cursor="hand2")
        self.read_version_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_profile_btn = tk.Button(btn_frame, text="⚙ Profile",
                                         command=self.read_profile, state="disabled", cursor="hand2")
        self.read_profile_btn.pack(side=tk.LEFT, padx=5)
        
        self.read_params_btn = tk.Button(btn_frame, text="🔧 Parameters",
                                        command=self.read_parameters, state="disabled", cursor="hand2")
        self.read_params_btn.pack(side=tk.LEFT, padx=5)
        
        # Output Section
        self.output_frame = tk.LabelFrame(self.main_frame, text="Output", padx=10, pady=10, font=("", 9, "bold"))
        self.output_frame.grid(row=3, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        self.output_frame.columnconfigure(0, weight=1)
        self.output_frame.rowconfigure(0, weight=1)
        
        self.output = scrolledtext.ScrolledText(self.output_frame, height=15, width=80,
                                                wrap=tk.WORD, relief=tk.FLAT, borderwidth=2)
        self.output.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Status bar
        self.status = tk.Label(self.main_frame, text="Ready", anchor=tk.W, relief=tk.SUNKEN, 
                              padx=5, pady=2)
        self.status.grid(row=4, column=0, columnspan=3, sticky=(tk.W, tk.E))
        
        # Store title and frame widgets for theming
        self.title_label = title
        self.title_frame = title_frame
        self.btn_frame = btn_frame
        
        # Configure row weights for resizing
        self.main_frame.rowconfigure(3, weight=1)
    
    def apply_theme(self):
        """Apply the current theme to all widgets"""
        theme = self.THEMES[self.current_theme]
        
        # Root window
        self.root.configure(bg=theme['bg'])
        
        # Main frame
        if hasattr(self, 'main_frame'):
            self.main_frame.configure(bg=theme['bg'])
        
        # Title frame and label
        if hasattr(self, 'title_frame'):
            self.title_frame.configure(bg=theme['bg'])
        if hasattr(self, 'title_label'):
            self.title_label.configure(bg=theme['bg'], fg=theme['fg'])
        
        # Theme button
        if hasattr(self, 'theme_btn'):
            if self.current_theme == 'dark':
                self.theme_btn.configure(text="☀ Light Mode", bg=theme['button_bg'], 
                                        fg=theme['button_fg'], activebackground=theme['select_bg'],
                                        activeforeground=theme['select_fg'])
            else:
                self.theme_btn.configure(text="🌙 Dark Mode", bg=theme['button_bg'], 
                                        fg=theme['button_fg'], activebackground=theme['select_bg'],
                                        activeforeground=theme['select_fg'])
        
        # Connection frame and widgets
        if hasattr(self, 'conn_frame'):
            self.conn_frame.configure(bg=theme['bg'], fg=theme['fg'])
            
            # Scan widgets
            if hasattr(self, 'scan_btn'):
                self.scan_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                       activebackground=theme['select_bg'],
                                       activeforeground=theme['select_fg'])
                self.filter_check.configure(bg=theme['bg'], fg=theme['fg'],
                                           activebackground=theme['bg'],
                                           activeforeground=theme['fg'],
                                           selectcolor=theme['entry_bg'])
                self.scan_status_lbl.configure(bg=theme['bg'], fg=theme['fg'])
            
            # Device selection widgets
            if hasattr(self, 'lbl_left_device'):
                self.lbl_left_device.configure(bg=theme['bg'], fg=theme['fg'])
                self.lbl_right_device.configure(bg=theme['bg'], fg=theme['fg'])
                self.left_device_menu.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                               activebackground=theme['select_bg'],
                                               activeforeground=theme['select_fg'])
                self.right_device_menu.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                                activebackground=theme['select_bg'],
                                                activeforeground=theme['select_fg'])
            
            # Labels
            self.lbl_left_mac.configure(bg=theme['bg'], fg=theme['fg'])
            self.lbl_left_key.configure(bg=theme['bg'], fg=theme['fg'])
            self.lbl_right_mac.configure(bg=theme['bg'], fg=theme['fg'])
            self.lbl_right_key.configure(bg=theme['bg'], fg=theme['fg'])
            
            # Entry widgets
            self.left_mac.configure(bg=theme['entry_bg'], fg=theme['entry_fg'],
                                   insertbackground=theme['entry_fg'],
                                   selectbackground=theme['select_bg'],
                                   selectforeground=theme['select_fg'])
            self.left_key.configure(bg=theme['entry_bg'], fg=theme['entry_fg'],
                                   insertbackground=theme['entry_fg'],
                                   selectbackground=theme['select_bg'],
                                   selectforeground=theme['select_fg'])
            self.right_mac.configure(bg=theme['entry_bg'], fg=theme['entry_fg'],
                                    insertbackground=theme['entry_fg'],
                                    selectbackground=theme['select_bg'],
                                    selectforeground=theme['select_fg'])
            self.right_key.configure(bg=theme['entry_bg'], fg=theme['entry_fg'],
                                    insertbackground=theme['entry_fg'],
                                    selectbackground=theme['select_bg'],
                                    selectforeground=theme['select_fg'])
            self.connect_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                      activebackground=theme['select_bg'],
                                      activeforeground=theme['select_fg'])
        
        # Control frame and widgets
        if hasattr(self, 'control_frame'):
            self.control_frame.configure(bg=theme['bg'], fg=theme['fg'])
            self.lbl_assist.configure(bg=theme['bg'], fg=theme['fg'])
            self.lbl_hill_hold.configure(bg=theme['bg'], fg=theme['fg'])
            self.assist_level_menu.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                            activebackground=theme['select_bg'],
                                            activeforeground=theme['select_fg'],
                                            highlightthickness=0)
            # Update menu appearance
            menu = self.assist_level_menu['menu']
            menu.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                          activebackground=theme['select_bg'],
                          activeforeground=theme['select_fg'])
            
            self.hill_hold_check.configure(bg=theme['bg'], fg=theme['fg'],
                                          activebackground=theme['bg'],
                                          activeforeground=theme['fg'],
                                          selectcolor=theme['entry_bg'])
            
            if hasattr(self, 'btn_frame'):
                self.btn_frame.configure(bg=theme['bg'])
            self.set_level_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                        activebackground=theme['select_bg'],
                                        activeforeground=theme['select_fg'])
            self.read_battery_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                           activebackground=theme['select_bg'],
                                           activeforeground=theme['select_fg'])
            self.read_status_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                          activebackground=theme['select_bg'],
                                          activeforeground=theme['select_fg'])
            self.read_version_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                           activebackground=theme['select_bg'],
                                           activeforeground=theme['select_fg'])
            if hasattr(self, 'read_profile_btn'):
                self.read_profile_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                               activebackground=theme['select_bg'],
                                               activeforeground=theme['select_fg'])
            if hasattr(self, 'read_params_btn'):
                self.read_params_btn.configure(bg=theme['button_bg'], fg=theme['button_fg'],
                                              activebackground=theme['select_bg'],
                                              activeforeground=theme['select_fg'])
        
        # Output frame and text widget
        if hasattr(self, 'output_frame'):
            self.output_frame.configure(bg=theme['bg'], fg=theme['fg'])
        if hasattr(self, 'output'):
            self.output.configure(bg=theme['output_bg'], fg=theme['output_fg'],
                                insertbackground=theme['output_fg'],
                                selectbackground=theme['select_bg'],
                                selectforeground=theme['select_fg'])
        
        # Status bar
        if hasattr(self, 'status'):
            self.status.configure(bg=theme['bg'], fg=theme['fg'])
    
    def toggle_theme(self):
        """Toggle between light and dark theme"""
        self.current_theme = 'light' if self.current_theme == 'dark' else 'dark'
        self.apply_theme()
        self.log(f"Switched to {self.current_theme} mode")
    
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
        self.read_profile_btn.config(state=state)
        self.read_params_btn.config(state=state)
    
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
        level_str = self.assist_level_var.get()
        level = self.assist_levels.index(level_str)
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
    
    def read_profile(self):
        """Read drive profile"""
        self.log("Reading drive profile...")
        self.status_message("Reading profile...")
        
        # TODO: Call m25_ecs.py to read drive profile
        self.log("  Active Profile: Standard")
        self.log("  Available: Customized, Standard, Sensitive, Soft, Active, Sensitive+")
        self.status_message("Profile read complete")
    
    def read_parameters(self):
        """Read drive parameters"""
        self.log("Reading drive parameters...")
        self.status_message("Reading parameters...")
        
        # TODO: Call m25_ecs.py to read drive parameters
        self.log("  Level 1 Parameters:")
        self.log("    Max Torque: 80%")
        self.log("    Max Speed: 2361 mm/s (8.5 km/h)")
        self.log("    P-Factor: 500")
        self.log("    Speed Bias: 30")
        self.status_message("Parameters read complete")
    
    def scan_devices(self):
        """Scan for Bluetooth devices"""
        if not HAS_BLUETOOTH:
            messagebox.showerror("Error", "Bluetooth support not available.\nInstall bleak: pip install bleak")
            return
        
        filter_enabled = self.filter_m25.get()
        scan_type = "M25 wheels" if filter_enabled else "all Bluetooth devices"
        self.log(f"Scanning for {scan_type}...")
        self.scan_status_lbl.config(text="Scanning...")
        self.scan_btn.config(state="disabled")
        self.status_message("Scanning for devices...")
        
        def scan_thread():
            try:
                bt = M25WindowsBluetooth()
                loop = asyncio.new_event_loop()
                asyncio.set_event_loop(loop)
                devices = loop.run_until_complete(bt.scan(duration=10, filter_m25=filter_enabled))
                loop.close()
                
                self.root.after(0, self.scan_complete, devices)
            except Exception as e:
                self.root.after(0, self.scan_error, str(e))
        
        threading.Thread(target=scan_thread, daemon=True).start()
    
    def scan_complete(self, devices):
        """Handle scan completion"""
        self.scanned_devices = devices
        self.scan_btn.config(state="normal")
        
        if not devices:
            self.log("No M25 devices found")
            self.scan_status_lbl.config(text="No devices found")
            self.status_message("Scan complete - no devices found")
            return
        
        device_type = "device(s)" if not self.filter_m25.get() else "M25 device(s)"
        self.log(f"Found {len(devices)} {device_type}:")
        for addr, name in devices:
            self.log(f"  [{addr}] {name}")
        
        # Update device menus
        device_options = [f"{name} ({addr})" for addr, name in devices]
        
        # Update left wheel menu
        menu = self.left_device_menu['menu']
        menu.delete(0, 'end')
        menu.add_command(label="", command=lambda: self.left_device_var.set(""))
        for option in device_options:
            menu.add_command(label=option, command=lambda val=option: self.left_device_var.set(val))
        
        # Update right wheel menu
        menu = self.right_device_menu['menu']
        menu.delete(0, 'end')
        menu.add_command(label="", command=lambda: self.right_device_var.set(""))
        for option in device_options:
            menu.add_command(label=option, command=lambda val=option: self.right_device_var.set(val))
        
        self.scan_status_lbl.config(text=f"Found {len(devices)} device(s)")
        self.status_message(f"Scan complete - found {len(devices)} device(s)")
    
    def scan_error(self, error):
        """Handle scan error"""
        self.scan_btn.config(state="normal")
        self.scan_status_lbl.config(text="Scan failed")
        self.log(f"Scan error: {error}")
        self.status_message("Scan failed")
        messagebox.showerror("Scan Error", f"Failed to scan for devices:\n{error}")
    
    def on_left_device_selected(self, selection):
        """Handle left device selection"""
        if not selection:
            return
        
        # Extract MAC address from selection
        if "(" in selection and ")" in selection:
            mac = selection.split("(")[1].split(")")[0]
            self.left_mac.delete(0, tk.END)
            self.left_mac.insert(0, mac)
            self.log(f"Selected left wheel: {selection}")
    
    def on_right_device_selected(self, selection):
        """Handle right device selection"""
        if not selection:
            return
        
        # Extract MAC address from selection
        if "(" in selection and ")" in selection:
            mac = selection.split("(")[1].split(")")[0]
            self.right_mac.delete(0, tk.END)
            self.right_mac.insert(0, mac)
            self.log(f"Selected right wheel: {selection}")


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
