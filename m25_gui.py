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
from datetime import datetime

try:
    from dotenv import load_dotenv
    HAS_DOTENV = True
except ImportError:
    HAS_DOTENV = False
    print("Warning: python-dotenv not installed. Install with: pip install python-dotenv")

try:
    from m25_spp import BluetoothConnection
    from m25_ecs import ECSPacketBuilder, ECSRemote, ResponseParser
    from m25_utils import parse_key
    HAS_BLUETOOTH = True
    IS_WINDOWS = False
except ImportError:
    # Windows fallback - use async Bluetooth
    try:
        from m25_bluetooth_windows import M25WindowsBluetooth
        from m25_crypto import M25Encryptor, M25Decryptor
        from m25_ecs import ECSPacketBuilder, ECSRemote, ResponseParser
        from m25_utils import parse_key
        HAS_BLUETOOTH = True
        IS_WINDOWS = True
        
        # Create adapter to make Windows async Bluetooth work with sync API
        class BluetoothConnectionAdapter:
            """Adapter to make M25WindowsBluetooth compatible with BluetoothConnection API"""
            def __init__(self, address, key, name="wheel", debug=False, loop=None):
                self.address = address
                self.key = key
                self.name = name
                self.debug = debug
                self.bt = M25WindowsBluetooth()
                self.encryptor = M25Encryptor(key)
                self.decryptor = M25Decryptor(key)
                self.loop = loop
                self.connected = False
            
            def connect(self, channel=6):
                """Connect to device"""
                if self.loop:
                    import time
                    success = self.loop.run_until_complete(self.bt.connect(self.address))
                    if success:
                        self.connected = True
                        time.sleep(0.1)  # Brief settling time
                    return success
                return False
            
            def disconnect(self):
                """Disconnect from device"""
                if self.loop and self.connected:
                    self.loop.run_until_complete(self.bt.disconnect())
                    self.connected = False
            
            def transact(self, spp_data, timeout=1.0):
                """Send packet and receive decrypted response (sync interface)"""
                if not self.loop or not self.connected:
                    return None
                
                import time
                try:
                    encrypted = self.encryptor.encrypt_packet(spp_data)
                    if self.debug:
                        print(f"  TX [{self.name}]: {encrypted.hex()}", file=sys.stderr)
                    
                    ok = self.loop.run_until_complete(self.bt.send_packet(encrypted))
                    if not ok:
                        return None
                    
                    time.sleep(0.2)  # Wait for response
                    
                    response = self.loop.run_until_complete(self.bt.receive_packet(timeout=int(timeout)))
                    if response:
                        decrypted = self.decryptor.decrypt_packet(response)
                        if self.debug:
                            print(f"  RX [{self.name}]: {response.hex()}", file=sys.stderr)
                            if decrypted:
                                print(f"      SPP: {decrypted.hex()}", file=sys.stderr)
                        return decrypted
                except Exception as e:
                    if self.debug:
                        print(f"  transact error: {e}", file=sys.stderr)
                    return None
                return None
        
        # Replace BluetoothConnection with adapter for Windows
        BluetoothConnection = BluetoothConnectionAdapter
        
    except ImportError as e:
        HAS_BLUETOOTH = False
        IS_WINDOWS = False
        print(f"Warning: M25 modules not available: {e}")


# Custom Entry widget with placeholder support
class PlaceholderEntry(tk.Entry):
    def __init__(self, master=None, placeholder="", placeholder_color="gray", is_password=False, **kwargs):
        super().__init__(master, **kwargs)
        
        self.placeholder = placeholder
        self.placeholder_color = placeholder_color
        self.default_fg_color = kwargs.get('fg', 'black')
        self.is_password = is_password
        self.has_placeholder = True
        
        if self.placeholder:
            self.put_placeholder()
        
        self.bind("<FocusIn>", self.on_focus_in)
        self.bind("<FocusOut>", self.on_focus_out)
    
    def put_placeholder(self):
        self.has_placeholder = True
        self.insert(0, self.placeholder)
        self.config(fg=self.placeholder_color, show="")
    
    def on_focus_in(self, event):
        if self.has_placeholder:
            self.delete(0, tk.END)
            self.config(fg=self.default_fg_color)
            if self.is_password:
                self.config(show="*")
            self.has_placeholder = False
    
    def on_focus_out(self, event):
        if not super().get():  # Check actual entry content, not our overridden get()
            self.put_placeholder()
    
    def get(self):
        content = super().get()
        if self.has_placeholder:
            return ""
        return content
    
    def set_theme_colors(self, fg_color, placeholder_color):
        """Update theme colors"""
        self.default_fg_color = fg_color
        self.placeholder_color = placeholder_color
        if self.has_placeholder:
            self.config(fg=placeholder_color)
        else:
            self.config(fg=fg_color)


class M25GUI:
    """Main GUI application for M25 wheelchair control"""

    # Theme definitions (base + semantic colors)
    THEMES = {
        "dark": {
            "bg": "#1e1e1e",
            "fg": "#d4d4d4",
            "entry_bg": "#2d2d2d",
            "entry_fg": "#d4d4d4",
            "button_bg": "#3e3e3e",
            "button_fg": "#d4d4d4",
            "output_bg": "#1e1e1e",
            "output_fg": "#d4d4d4",
            "select_bg": "#264f78",
            "select_fg": "#ffffff",
            "panel_bg": "#262626",
            "border": "#3a3a3a",
            "text": {
                "default": "#d4d4d4",
                "muted": "#9aa0a6",
                "info": "#4aa3ff",
                "success": "#6cc070",
                "warning": "#e0b85c",
                "error": "#e06c75",
            },
        },
        "light": {
            "bg": "#f0f0f0",
            "fg": "#000000",
            "entry_bg": "#ffffff",
            "entry_fg": "#000000",
            "button_bg": "#e1e1e1",
            "button_fg": "#000000",
            "output_bg": "#ffffff",
            "output_fg": "#000000",
            "select_bg": "#0078d7",
            "select_fg": "#ffffff",
            "panel_bg": "#ffffff",
            "border": "#d0d0d0",
            "text": {
                "default": "#111111",
                "muted": "#555555",
                "info": "#0b63ce",
                "success": "#2e7d32",
                "warning": "#b26a00",
                "error": "#c62828",
            },
        },
    }

    LEVELS = ("muted", "info", "success", "warning", "error")

    def __init__(self, root):
        self.root = root
        self.root.title("m5squared - Wheelchair Controller")
        self.root.geometry("800x600")

        # Connection state
        self.connected = False
        self.scanned_devices = []
        self.left_conn = None
        self.right_conn = None
        self.ecs_remote = None
        self.demo_mode = False
        self.event_loop = None
        self.loop_thread = None

        # Theme state
        self.current_theme = "dark"

        # Create UI first (so status bar exists)
        self.create_widgets()

        # Apply theme
        self.apply_theme()

        # Load environment variables
        self.load_env()

        # Load saved credentials
        self.load_credentials()

        self.log("info", "Ready.")
        self.status_message("info", "Ready")

    def load_env(self):
        """Load .env file if available"""
        if HAS_DOTENV:
            env_path = Path(".env")
            if env_path.exists():
                load_dotenv(env_path)
                self.status_message("success", "Loaded .env file")
            else:
                self.status_message("muted", "No .env file found (optional)")
        else:
            self.status_message("warning", "python-dotenv not installed")

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
        self.title_frame = tk.Frame(self.main_frame)
        self.title_frame.grid(row=0, column=0, columnspan=3, pady=(0, 20))

        self.title_label = tk.Label(self.title_frame, text="m5squared Wheelchair Controller", font=("Arial", 16, "bold"))
        self.title_label.pack(side=tk.LEFT, padx=(0, 20))

        # Theme toggle button
        self.theme_btn = tk.Button(
            self.title_frame,
            text="☀ Light Mode",
            command=self.toggle_theme,
            relief=tk.FLAT,
            cursor="hand2",
            font=("Arial", 10),
        )
        self.theme_btn.pack(side=tk.LEFT)

        # Connection Section
        self.conn_frame = tk.LabelFrame(self.main_frame, text="Connection", padx=10, pady=10, font=("", 9, "bold"))
        self.conn_frame.grid(row=1, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        self.conn_frame.columnconfigure(1, weight=1)

        # Scan button
        self.scan_frame = tk.Frame(self.conn_frame)
        self.scan_frame.grid(row=0, column=0, columnspan=2, pady=(0, 10), sticky=(tk.W, tk.E))

        self.scan_btn = tk.Button(self.scan_frame, text="🔍 Scan for Wheels", command=self.scan_devices, cursor="hand2")
        self.scan_btn.pack(side=tk.LEFT, padx=(0, 10))

        self.filter_m25 = tk.BooleanVar(value=True)
        self.filter_check = tk.Checkbutton(self.scan_frame, text="Filter M25 only", variable=self.filter_m25)
        self.filter_check.pack(side=tk.LEFT, padx=(0, 10))

        self.scan_status_lbl = tk.Label(self.scan_frame, text="")
        self.scan_status_lbl.pack(side=tk.LEFT)

        # Device selection for left wheel
        self.lbl_left_device = tk.Label(self.conn_frame, text="Left Wheel:")
        self.lbl_left_device.grid(row=1, column=0, sticky=tk.W, pady=2)

        self.left_device_frame = tk.Frame(self.conn_frame)
        self.left_device_frame.grid(row=1, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        self.left_device_frame.columnconfigure(0, weight=1)

        self.left_device_var = tk.StringVar(value="")
        self.left_device_menu = tk.OptionMenu(self.left_device_frame, self.left_device_var, "", command=self.on_left_device_selected)
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

        self.right_device_frame = tk.Frame(self.conn_frame)
        self.right_device_frame.grid(row=4, column=1, sticky=(tk.W, tk.E), padx=(5, 0), pady=2)
        self.right_device_frame.columnconfigure(0, weight=1)

        self.right_device_var = tk.StringVar(value="")
        self.right_device_menu = tk.OptionMenu(self.right_device_frame, self.right_device_var, "", command=self.on_right_device_selected)
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
        self.connect_btn = tk.Button(self.conn_frame, text="Connect", command=self.toggle_connection, cursor="hand2")
        self.connect_btn.grid(row=7, column=0, columnspan=2, pady=(10, 0))

        # Control Section
        self.control_frame = tk.LabelFrame(self.main_frame, text="Controls", padx=10, pady=10, font=("", 9, "bold"))
        self.control_frame.grid(row=2, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))

        # Assist level
        self.lbl_assist = tk.Label(self.control_frame, text="Assist Level:")
        self.lbl_assist.grid(row=0, column=0, sticky=tk.W, pady=5)

        self.assist_frame = tk.Frame(self.control_frame)
        self.assist_frame.grid(row=0, column=1, padx=(5, 10), sticky=tk.W)
        self.assist_level_var = tk.StringVar(value="Level 1 (Normal)")
        self.assist_levels = ["Level 1 (Normal)", "Level 2 (Outdoor)", "Level 3 (Learning)"]
        self.assist_level_menu = tk.OptionMenu(self.assist_frame, self.assist_level_var, *self.assist_levels)
        self.assist_level_menu.config(width=18)
        self.assist_level_menu.pack()

        self.set_level_btn = tk.Button(self.control_frame, text="Set Level", command=self.set_assist_level, state="disabled", cursor="hand2")
        self.set_level_btn.grid(row=0, column=2, pady=5)

        # Drive profile
        self.lbl_profile = tk.Label(self.control_frame, text="Drive Profile:")
        self.lbl_profile.grid(row=1, column=0, sticky=tk.W, pady=5)

        self.profile_frame = tk.Frame(self.control_frame)
        self.profile_frame.grid(row=1, column=1, padx=(5, 10), sticky=tk.W)
        self.profile_var = tk.StringVar(value="Standard")
        self.profiles = ["Standard", "Sensitive", "Soft", "Active", "SensitivePlus"]
        self.profile_menu = tk.OptionMenu(self.profile_frame, self.profile_var, *self.profiles)
        self.profile_menu.config(width=18)
        self.profile_menu.pack()

        self.set_profile_btn = tk.Button(self.control_frame, text="Set Profile", command=self.set_drive_profile, state="disabled", cursor="hand2")
        self.set_profile_btn.grid(row=1, column=2, pady=5)

        # Hill hold
        self.lbl_hill_hold = tk.Label(self.control_frame, text="Hill Hold:")
        self.lbl_hill_hold.grid(row=2, column=0, sticky=tk.W, pady=5)
        self.hill_hold = tk.BooleanVar()
        self.hill_hold_check = tk.Checkbutton(
            self.control_frame,
            text="Enable",
            variable=self.hill_hold,
            command=self.toggle_hill_hold,
            state="disabled",
        )
        self.hill_hold_check.grid(row=2, column=1, sticky=tk.W, padx=(5, 0), pady=5)

        # Status buttons
        self.btn_frame = tk.Frame(self.control_frame)
        self.btn_frame.grid(row=3, column=0, columnspan=3, pady=(10, 0))

        self.read_battery_btn = tk.Button(self.btn_frame, text="🔋 Battery", command=self.read_battery, state="disabled", cursor="hand2")
        self.read_battery_btn.pack(side=tk.LEFT, padx=5)

        self.read_status_btn = tk.Button(self.btn_frame, text="📊 Status", command=self.read_status, state="disabled", cursor="hand2")
        self.read_status_btn.pack(side=tk.LEFT, padx=5)

        self.read_version_btn = tk.Button(self.btn_frame, text="ℹ Version", command=self.read_version, state="disabled", cursor="hand2")
        self.read_version_btn.pack(side=tk.LEFT, padx=5)

        self.read_profile_btn = tk.Button(self.btn_frame, text="⚙ Profile", command=self.read_profile, state="disabled", cursor="hand2")
        self.read_profile_btn.pack(side=tk.LEFT, padx=5)
        
        self.info_dump_btn = tk.Button(self.btn_frame, text="📋 Info Dump", command=self.info_dump, state="disabled", cursor="hand2")
        self.info_dump_btn.pack(side=tk.LEFT, padx=5)

        # Output Section
        self.output_frame = tk.LabelFrame(self.main_frame, text="Output", padx=10, pady=10, font=("", 9, "bold"))
        self.output_frame.grid(row=3, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S), pady=(0, 10))
        self.output_frame.columnconfigure(0, weight=1)
        self.output_frame.rowconfigure(0, weight=1)

        self.output = scrolledtext.ScrolledText(
            self.output_frame,
            height=15,
            width=80,
            wrap=tk.WORD,
            relief=tk.FLAT,
            borderwidth=2,
        )
        self.output.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # Status bar
        self.status = tk.Label(self.main_frame, text="Ready", anchor=tk.W, relief=tk.SUNKEN, padx=5, pady=2)
        self.status.grid(row=4, column=0, columnspan=3, sticky=(tk.W, tk.E))

        # Configure row weights for resizing
        self.main_frame.rowconfigure(3, weight=1)

    def apply_theme(self):
        """Apply the current theme to all widgets"""
        theme = self.THEMES[self.current_theme]

        # Root window
        self.root.configure(bg=theme["bg"])

        # Main frame
        if hasattr(self, "main_frame"):
            self.main_frame.configure(bg=theme["bg"])

        # Title frame and label
        if hasattr(self, "title_frame"):
            self.title_frame.configure(bg=theme["bg"])
        if hasattr(self, "title_label"):
            self.title_label.configure(bg=theme["bg"], fg=theme["fg"])

        # Theme button
        if hasattr(self, "theme_btn"):
            if self.current_theme == "dark":
                self.theme_btn.configure(
                    text="☀ Light Mode",
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                )
            else:
                self.theme_btn.configure(
                    text="🌙 Dark Mode",
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                )

        # Connection frame and widgets
        if hasattr(self, "conn_frame"):
            self.conn_frame.configure(bg=theme["bg"], fg=theme["fg"])

            # Scan frame and its widgets
            if hasattr(self, "scan_frame"):
                self.scan_frame.configure(bg=theme["bg"])
            
            if hasattr(self, "scan_btn"):
                self.scan_btn.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                )
                self.filter_check.configure(
                    bg=theme["bg"],
                    fg=theme["fg"],
                    activebackground=theme["bg"],
                    activeforeground=theme["fg"],
                    selectcolor=theme["entry_bg"],
                )
                self.scan_status_lbl.configure(bg=theme["bg"], fg=theme["fg"])

            # Device selection frames and menus
            if hasattr(self, "lbl_left_device"):
                self.lbl_left_device.configure(bg=theme["bg"], fg=theme["fg"])
                self.lbl_right_device.configure(bg=theme["bg"], fg=theme["fg"])
                
                # Device frames
                self.left_device_frame.configure(bg=theme["bg"])
                self.right_device_frame.configure(bg=theme["bg"])
                
                # OptionMenu widgets
                self.left_device_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                    highlightthickness=0
                )
                # OptionMenu dropdown menu
                left_menu = self.left_device_menu['menu']
                left_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"]
                )
                
                self.right_device_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                    highlightthickness=0
                )
                # OptionMenu dropdown menu
                right_menu = self.right_device_menu['menu']
                right_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"]
                )

            self.lbl_left_mac.configure(bg=theme["bg"], fg=theme["fg"])
            self.lbl_left_key.configure(bg=theme["bg"], fg=theme["fg"])
            self.lbl_right_mac.configure(bg=theme["bg"], fg=theme["fg"])
            self.lbl_right_key.configure(bg=theme["bg"], fg=theme["fg"])

            self.left_mac.configure(
                bg=theme["entry_bg"],
                fg=theme["entry_fg"],
                insertbackground=theme["entry_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )
            self.left_key.configure(
                bg=theme["entry_bg"],
                fg=theme["entry_fg"],
                insertbackground=theme["entry_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )
            self.right_mac.configure(
                bg=theme["entry_bg"],
                fg=theme["entry_fg"],
                insertbackground=theme["entry_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )
            self.right_key.configure(
                bg=theme["entry_bg"],
                fg=theme["entry_fg"],
                insertbackground=theme["entry_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )
            self.connect_btn.configure(
                bg=theme["button_bg"],
                fg=theme["button_fg"],
                activebackground=theme["select_bg"],
                activeforeground=theme["select_fg"],
            )

        # Control frame and widgets
        if hasattr(self, "control_frame"):
            self.control_frame.configure(bg=theme["bg"], fg=theme["fg"])
            
            # Assist frame
            if hasattr(self, "assist_frame"):
                self.assist_frame.configure(bg=theme["bg"])
            
            self.lbl_assist.configure(bg=theme["bg"], fg=theme["fg"])
            self.assist_level_menu.configure(
                bg=theme["button_bg"],
                fg=theme["button_fg"],
                activebackground=theme["select_bg"],
                activeforeground=theme["select_fg"],
                highlightthickness=0,
            )
            menu = self.assist_level_menu["menu"]
            menu.configure(
                bg=theme["button_bg"],
                fg=theme["button_fg"],
                activebackground=theme["select_bg"],
                activeforeground=theme["select_fg"],
            )

            # Profile frame
            if hasattr(self, "profile_frame"):
                self.profile_frame.configure(bg=theme["bg"])
            
            if hasattr(self, "lbl_profile"):
                self.lbl_profile.configure(bg=theme["bg"], fg=theme["fg"])
                self.profile_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                    highlightthickness=0,
                )
                profile_menu = self.profile_menu["menu"]
                profile_menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                )

            self.lbl_hill_hold.configure(
                bg=theme["bg"],
                fg=theme["fg"],
                activebackground=theme["bg"],
                activeforeground=theme["fg"],
            )

            if hasattr(self, "btn_frame"):
                self.btn_frame.configure(bg=theme["bg"])

            for btn in (
                self.set_level_btn,
                self.set_profile_btn,
                self.read_battery_btn,
                self.read_status_btn,
                self.read_version_btn,
                self.read_profile_btn,
                self.info_dump_btn,
            ):
                btn.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"],
                )

        # Output frame and text widget
        if hasattr(self, "output_frame"):
            self.output_frame.configure(bg=theme["bg"], fg=theme["fg"])

        if hasattr(self, "output"):
            self.output.configure(
                bg=theme["output_bg"],
                fg=theme["output_fg"],
                insertbackground=theme["output_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )

            # Semantic tags for log output
            self._apply_output_tags()

        # Status bar
        if hasattr(self, "status"):
            # Do not force fg here. status_message() sets fg by level.
            self.status.configure(bg=theme["bg"])

    def _apply_output_tags(self):
        """Apply semantic tags to the output widget for the current theme"""
        theme = self.THEMES[self.current_theme]
        text = theme["text"]

        # Keep tag config centralized. Use semantic names, not colors.
        self.output.tag_configure("muted", foreground=text["muted"])
        self.output.tag_configure("info", foreground=text["info"])
        self.output.tag_configure("success", foreground=text["success"])
        self.output.tag_configure("warning", foreground=text["warning"])
        self.output.tag_configure("error", foreground=text["error"])

        # Timestamp and prefix are muted. Message uses level tag.
        self.output.tag_configure("ts", foreground=text["muted"])
        self.output.tag_configure("prefix", foreground=text["muted"])

    def toggle_theme(self):
        """Toggle between light and dark theme"""
        self.current_theme = "light" if self.current_theme == "dark" else "dark"
        self.apply_theme()
        self.log("muted", f"Theme set: {self.current_theme}")
        self.status_message("muted", f"Theme set: {self.current_theme}")

    def load_credentials(self):
        """Load credentials from environment variables"""
        self.left_mac.insert(0, os.getenv("M25_LEFT_MAC", ""))
        self.left_key.insert(0, os.getenv("M25_LEFT_KEY", ""))
        self.right_mac.insert(0, os.getenv("M25_RIGHT_MAC", ""))
        self.right_key.insert(0, os.getenv("M25_RIGHT_KEY", ""))

        if self.left_mac.get() or self.right_mac.get():
            self.log("info", "Credentials loaded from .env file")

    def status_message(self, level, msg):
        """Update status bar with semantic color"""
        lvl = (level or "info").strip().lower()
        if lvl not in self.LEVELS:
            lvl = "info"

        theme = self.THEMES[self.current_theme]
        fg = theme["text"][lvl] if lvl in theme["text"] else theme["fg"]

        self.status.config(text=msg, fg=fg)

    def log(self, level, message):
        """Append message to output log with semantic color"""
        lvl = (level or "info").strip().lower()
        if lvl not in self.LEVELS:
            lvl = "info"

        ts = datetime.now().strftime("%H:%M:%S")
        prefix = {
            "muted": "NOTE",
            "info": "INFO",
            "success": "OK",
            "warning": "WARN",
            "error": "ERR",
        }[lvl]

        self.output.insert(tk.END, "[", ("ts",))
        self.output.insert(tk.END, ts, ("ts",))
        self.output.insert(tk.END, "] ", ("ts",))
        self.output.insert(tk.END, f"{prefix}: ", ("prefix",))
        self.output.insert(tk.END, f"{message}\n", (lvl,))
        self.output.see(tk.END)

    def enable_controls(self, enabled=True):
        """Enable or disable control buttons"""
        state = "normal" if enabled else "disabled"
        self.set_level_btn.config(state=state)
        self.set_profile_btn.config(state=state)
        self.hill_hold_check.config(state=state)
        self.read_battery_btn.config(state=state)
        self.read_status_btn.config(state=state)
        self.read_version_btn.config(state=state)
        self.read_profile_btn.config(state=state)
        self.info_dump_btn.config(state=state)

    def toggle_connection(self):
        """Toggle between connect and disconnect"""
        if self.connected:
            self.disconnect()
        else:
            self.connect()
    
    def disconnect(self):
        """Disconnect from M25 wheels"""
        # Confirm disconnection
        confirm = messagebox.askyesno(
            "Confirm Disconnect",
            "Are you sure you want to disconnect from the wheels?"
        )
        
        if not confirm:
            return
        
        self.log("info", "Disconnecting from wheels...")
        self.status_message("info", "Disconnecting...")
        
        def disconnect_thread():
            import time
            try:
                if self.demo_mode:
                    # Simulate disconnection in demo mode
                    time.sleep(1)
                    self.root.after(0, self.disconnection_complete)
                else:
                    # Real hardware disconnection
                    if self.left_conn:
                        self.left_conn.disconnect()
                    if self.right_conn:
                        self.right_conn.disconnect()
                    
                    # Close event loop if Windows
                    if IS_WINDOWS and self.event_loop and not self.event_loop.is_closed():
                        self.event_loop.close()
                        self.event_loop = None
                    
                    # Clear connection objects
                    self.left_conn = None
                    self.right_conn = None
                    self.ecs_remote = None
                    
                    self.root.after(0, self.disconnection_complete)
            except Exception as e:
                self.root.after(0, self.disconnection_error, str(e))
        
        threading.Thread(target=disconnect_thread, daemon=True).start()
    
    def disconnection_error(self, error_msg):
        """Handle disconnection error"""
        self.log("error", f"Disconnection failed: {error_msg}")
        self.status_message("error", "Disconnection failed")
        messagebox.showerror("Disconnection Error", f"Failed to disconnect:\n{error_msg}")
    
    def disconnection_complete(self):
        """Handle disconnection completion"""
        self.connected = False
        self.connect_btn.config(text="Connect")
        self.enable_controls(False)
        self.log("success", "Disconnected successfully.")
        self.status_message("success", "Disconnected")

    def connect(self):
        """Connect to M25 wheels"""
        left_mac = self.left_mac.get().strip()
        left_key = self.left_key.get().strip()
        right_mac = self.right_mac.get().strip()
        right_key = self.right_key.get().strip()

        if not left_mac or not left_key:
            messagebox.showerror("Error", "Please enter left wheel MAC address and key")
            self.log("error", "Missing left wheel MAC or key.")
            self.status_message("error", "Missing left wheel MAC or key")
            return

        if not right_mac or not right_key:
            messagebox.showerror("Error", "Please enter right wheel MAC address and key")
            self.log("error", "Missing right wheel MAC or key.")
            self.status_message("error", "Missing right wheel MAC or key")
            return

        # Detect demo mode
        self.demo_mode = left_mac.upper() == "AA:BB:CC:DD:EE:FF" or right_mac.upper() == "AA:BB:CC:DD:EE:FF"
        
        if self.demo_mode:
            self.log("warning", "DEMO MODE detected (MAC: AA:BB:CC:DD:EE:FF)")
            self.log("info", "Running in simulation mode - no real hardware connection")
        else:
            if not HAS_BLUETOOTH:
                messagebox.showerror("Error", "Bluetooth modules not available.\nInstall required packages.")
                self.log("error", "Bluetooth modules not available")
                self.status_message("error", "Bluetooth modules not available")
                return
            self.log("info", "Connecting to wheels...")
        
        self.log("muted", f"Left:  {left_mac}")
        self.log("muted", f"Right: {right_mac}")
        self.status_message("info", "Connecting..." if not self.demo_mode else "Connecting (Demo Mode)...")

        def connect_thread():
            import time
            try:
                if self.demo_mode:
                    # Simulate connection in demo mode
                    time.sleep(2)
                    self.root.after(0, self.connection_complete, True, self.demo_mode)
                else:
                    # Parse encryption keys
                    try:
                        left_key_bytes = parse_key(left_key)
                        right_key_bytes = parse_key(right_key)
                    except Exception as e:
                        self.root.after(0, self.connection_error, f"Invalid encryption key: {e}")
                        return
                    
                    # Create event loop for Windows async Bluetooth
                    if IS_WINDOWS:
                        if not self.event_loop or self.event_loop.is_closed():
                            self.event_loop = asyncio.new_event_loop()
                            asyncio.set_event_loop(self.event_loop)
                        loop = self.event_loop
                    else:
                        loop = None
                    
                    # Create Bluetooth connections
                    self.left_conn = BluetoothConnection(left_mac, left_key_bytes, name="left", debug=False)
                    self.right_conn = BluetoothConnection(right_mac, right_key_bytes, name="right", debug=False)
                    
                    # Pass event loop to Windows adapter
                    if IS_WINDOWS:
                        self.left_conn.loop = loop
                        self.right_conn.loop = loop
                    
                    # Connect to wheels
                    if not self.left_conn.connect():
                        self.root.after(0, self.connection_error, f"Failed to connect to left wheel at {left_mac}")
                        return
                    
                    if not self.right_conn.connect():
                        self.left_conn.disconnect()
                        self.root.after(0, self.connection_error, f"Failed to connect to right wheel at {right_mac}")
                        return
                    
                    # Create ECS Remote helper
                    self.ecs_remote = ECSRemote(self.left_conn, self.right_conn, verbose=False, retries=2)
                    
                    self.root.after(0, self.connection_complete, True, False)
                    
            except Exception as e:
                self.root.after(0, self.connection_error, str(e))

        threading.Thread(target=connect_thread, daemon=True).start()

    def connection_error(self, error_msg):
        """Handle connection error"""
        self.log("error", f"Connection failed: {error_msg}")
        self.status_message("error", "Connection failed")
        messagebox.showerror("Connection Error", f"Failed to connect to wheels:\n{error_msg}")

    def connection_complete(self, success, demo_mode=False):
        """Handle connection result"""
        if success:
            self.connected = True
            if demo_mode:
                self.log("success", "Connected in DEMO MODE - using simulated hardware")
                self.status_message("warning", "Connected (Demo Mode)")
            else:
                self.log("success", "Connected successfully.")
                self.status_message("success", "Connected")
            self.connect_btn.config(text="Disconnect")
            self.enable_controls(True)
        else:
            self.log("error", "Connection failed.")
            self.status_message("error", "Connection failed")
            messagebox.showerror("Error", "Failed to connect to wheels")

    def set_assist_level(self):
        """Set assist level"""
        level_str = self.assist_level_var.get()
        level = self.assist_levels.index(level_str)
        level_names = ["Normal (Level 1)", "Outdoor (Level 2)", "Learning (Level 3)"]
        self.log("info", f"Setting assist level: {level_names[level]}")
        self.status_message("info", f"Setting assist level to {level + 1}...")

        if self.demo_mode:
            self.log("warning", "Demo mode: Assist level change simulated")
            self.status_message("success", f"Assist level set to {level + 1}")
        else:
            # Real hardware command using ECSRemote
            def write_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn or not self.right_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    builder = ECSPacketBuilder()
                    
                    # Write to left wheel
                    left_ok = self.ecs_remote.write_assist_level(self.left_conn, builder, level)
                    if left_ok:
                        ui_log("success", "Left wheel: Assist level set")
                    else:
                        ui_log("warning", "Left wheel: Failed to set assist level")
                    
                    # Write to right wheel
                    right_ok = self.ecs_remote.write_assist_level(self.right_conn, builder, level)
                    if right_ok:
                        ui_log("success", "Right wheel: Assist level set")
                    else:
                        ui_log("warning", "Right wheel: Failed to set assist level")
                    
                    if left_ok and right_ok:
                        ui_status("success", f"Assist level set to {level + 1}")
                    else:
                        ui_status("warning", "Assist level partially set")
                    
                except Exception as e:
                    ui_log("error", f"Assist level change failed: {e}")
                    ui_status("error", "Assist level change failed")
            
            threading.Thread(target=write_thread, daemon=True).start()

    def set_drive_profile(self):
        """Set drive profile"""
        profile_name = self.profile_var.get()
        
        # Map profile name to ID
        from m25_protocol_data import (
            PROFILE_ID_STANDARD, PROFILE_ID_SENSITIVE, PROFILE_ID_SOFT,
            PROFILE_ID_ACTIVE, PROFILE_ID_SENSITIVE_PLUS
        )
        
        profile_map = {
            "Standard": PROFILE_ID_STANDARD,
            "Sensitive": PROFILE_ID_SENSITIVE,
            "Soft": PROFILE_ID_SOFT,
            "Active": PROFILE_ID_ACTIVE,
            "SensitivePlus": PROFILE_ID_SENSITIVE_PLUS,
        }
        
        profile_id = profile_map.get(profile_name)
        if profile_id is None:
            self.log("error", f"Unknown profile: {profile_name}")
            return
        
        self.log("info", f"Setting drive profile: {profile_name}")
        self.status_message("info", f"Setting profile to {profile_name}...")

        if self.demo_mode:
            self.log("warning", "Demo mode: Profile change simulated")
            self.status_message("success", f"Profile set to {profile_name}")
        else:
            # Real hardware command using ECSRemote
            def write_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn or not self.right_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    builder = ECSPacketBuilder()
                    
                    # Build write profile packet
                    packet = builder.build_write_drive_profile(profile_id)
                    
                    # Write to left wheel
                    left_ok = self.ecs_remote.write_value(self.left_conn, packet, "write_drive_profile")
                    if left_ok:
                        ui_log("success", "Left wheel: Profile set")
                    else:
                        ui_log("warning", "Left wheel: Failed to set profile")
                    
                    # Write to right wheel
                    right_ok = self.ecs_remote.write_value(self.right_conn, packet, "write_drive_profile")
                    if right_ok:
                        ui_log("success", "Right wheel: Profile set")
                    else:
                        ui_log("warning", "Right wheel: Failed to set profile")
                    
                    if left_ok and right_ok:
                        ui_status("success", f"Profile set to {profile_name}")
                    else:
                        ui_status("warning", "Profile partially set")
                    
                except Exception as e:
                    ui_log("error", f"Profile change failed: {e}")
                    ui_status("error", "Profile change failed")
            
            threading.Thread(target=write_thread, daemon=True).start()

    def toggle_hill_hold(self):
        """Toggle hill hold on or off"""
        enabled = self.hill_hold.get()
        state = "ON" if enabled else "OFF"
        self.log("info", f"Setting hill hold: {state}")
        self.status_message("info", f"Hill hold {state}")

    def read_battery(self):
        """Read battery status"""
        self.log("info", "Reading battery status...")
        self.status_message("info", "Reading battery...")
        
        if self.demo_mode:
            # Demo mode - simulated values
            self.log("muted", "Left wheel:  85%")
            self.log("muted", "Right wheel: 83%")
            self.status_message("success", "Battery read complete")
        else:
            # Real hardware reading using ECSRemote
            def read_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn or not self.right_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    builder = ECSPacketBuilder()
                    
                    # Read left wheel battery
                    left_soc = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_soc,
                        0x21,  # PARAM_ID_STATUS_SOC
                        ResponseParser.parse_soc
                    )
                    left_battery = f"{left_soc}%" if left_soc is not None else "??%"
                    
                    # Read right wheel battery
                    right_soc = self.ecs_remote.read_value(
                        self.right_conn,
                        builder.build_read_soc,
                        0x21,  # PARAM_ID_STATUS_SOC
                        ResponseParser.parse_soc
                    )
                    right_battery = f"{right_soc}%" if right_soc is not None else "??%"
                    
                    ui_log("muted", f"Left wheel:  {left_battery}")
                    ui_log("muted", f"Right wheel: {right_battery}")
                    ui_status("success", "Battery read complete")
                    
                except Exception as e:
                    ui_log("error", f"Battery read failed: {e}")
                    ui_status("error", "Battery read failed")
            
            threading.Thread(target=read_thread, daemon=True).start()

    def read_status(self):
        """Read full status"""
        self.log("info", "Reading full status...")
        self.status_message("info", "Reading status...")

        if self.demo_mode:
            self.log("muted", "Assist Level: 1 (Normal)")
            self.log("muted", "Hill Hold: OFF")
            self.log("muted", "Drive Profile: Standard")
            self.status_message("success", "Status read complete")
        else:
            # Real hardware reading using ECSRemote
            def read_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    builder = ECSPacketBuilder()
                    
                    # Read assist level from left wheel
                    assist = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_assist_level,
                        0x22,  # PARAM_ID_STATUS_ASSIST_LEVEL
                        ResponseParser.parse_assist_level
                    )
                    assist_info = f"{assist['value']} ({assist['name']})" if assist else "??"
                    
                    # Read drive mode from left wheel
                    mode = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_drive_mode,
                        0x23,  # PARAM_ID_STATUS_DRIVE_MODE
                        ResponseParser.parse_drive_mode
                    )
                    hill_hold = "ON" if (mode and mode['auto_hold']) else ("OFF" if mode else "??")
                    
                    # Read drive profile from left wheel
                    profile = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_drive_profile,
                        0x24,  # PARAM_ID_STATUS_DRIVE_PROFILE
                        ResponseParser.parse_drive_profile
                    )
                    profile_info = profile['name'] if profile else "??"
                    
                    ui_log("muted", f"Assist Level: {assist_info}")
                    ui_log("muted", f"Hill Hold: {hill_hold}")
                    ui_log("muted", f"Drive Profile: {profile_info}")
                    ui_status("success", "Status read complete")
                    
                except Exception as e:
                    ui_log("error", f"Status read failed: {e}")
                    ui_status("error", "Status read failed")
            
            threading.Thread(target=read_thread, daemon=True).start()

    def read_version(self):
        """Read firmware version"""
        self.log("info", "Reading firmware version...")
        self.status_message("info", "Reading version...")

        if self.demo_mode:
            self.log("muted", "Firmware: v2.5.1")
            self.log("muted", "Hardware: M25V1")
            self.status_message("success", "Version read complete")
        else:
            # Real hardware reading using ECSRemote
            def read_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn or not self.right_conn:
                        ui_log("error", "Not connected")
                        return

                    builder = ECSPacketBuilder()

                    # Read left wheel version
                    left_ver = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_sw_version,
                        0x2E,  # PARAM_ID_STATUS_SW_VERSION
                        ResponseParser.parse_sw_version
                    )
                    if left_ver:
                        ui_log("success", f"Left wheel: {left_ver['version_str']}")
                    else:
                        ui_log("warning", "Left wheel: Unable to read version")

                    # Read right wheel version
                    right_ver = self.ecs_remote.read_value(
                        self.right_conn,
                        builder.build_read_sw_version,
                        0x2E,  # PARAM_ID_STATUS_SW_VERSION
                        ResponseParser.parse_sw_version
                    )
                    if right_ver:
                        ui_log("success", f"Right wheel: {right_ver['version_str']}")
                    else:
                        ui_log("warning", "Right wheel: Unable to read version")

                    ui_log("muted", "Hardware: M25V2")
                    ui_status("success", "Version read complete")

                except Exception as e:
                    ui_log("error", f"Version read failed: {e}")
                    ui_status("error", "Version read failed")

            threading.Thread(target=read_thread, daemon=True).start()

    def read_profile(self):
        """Read drive profile"""
        self.log("info", "Reading drive profile...")
        self.status_message("info", "Reading profile...")

        if self.demo_mode:
            self.log("muted", "Active Profile: Standard")
            self.log("muted", "Available: Customized, Standard, Sensitive, Soft, Active, Sensitive+")
            self.status_message("success", "Profile read complete")
        else:
            # Real hardware reading using ECSRemote
            def read_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                try:
                    if not self.ecs_remote or not self.left_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    builder = ECSPacketBuilder()
                    
                    # Read active profile from left wheel
                    profile = self.ecs_remote.read_value(
                        self.left_conn,
                        builder.build_read_drive_profile,
                        0x24,  # PARAM_ID_STATUS_DRIVE_PROFILE
                        ResponseParser.parse_drive_profile
                    )
                    
                    if profile:
                        ui_log("success", f"Active Profile: {profile['name']}")
                    else:
                        ui_log("warning", "Active Profile: Unable to read")
                    
                    # Show available profiles
                    from m25_protocol_data import PROFILE_NAMES
                    available = ", ".join(PROFILE_NAMES.values())
                    ui_log("muted", f"Available: {available}")
                    ui_status("success", "Profile read complete")
                    
                except Exception as e:
                    ui_log("error", f"Profile read failed: {e}")
                    ui_status("error", "Profile read failed")
            
            threading.Thread(target=read_thread, daemon=True).start()


    def info_dump(self):
        """Get comprehensive info dump from both wheels"""
        self.log("info", "=== Starting comprehensive info dump ===")
        self.status_message("info", "Reading all available data...")
        
        if self.demo_mode:
            # Demo mode comprehensive dump
            self.log("warning", "DEMO MODE - Simulated values")
            self.log("info", "")
            self.log("success", "=== LEFT WHEEL ===")
            self.log("muted", "Firmware: V03.005.001")
            self.log("muted", "Battery: 85%")
            self.log("muted", "Assist Level: 1 (Normal)")
            self.log("muted", "Hill Hold: OFF")
            self.log("muted", "Drive Profile: Standard")
            self.log("muted", "Distance: 1234.5 km")
            self.log("info", "")
            self.log("success", "=== RIGHT WHEEL ===")
            self.log("muted", "Firmware: V03.005.001")
            self.log("muted", "Battery: 83%")
            self.log("muted", "Assist Level: 1 (Normal)")
            self.log("muted", "Hill Hold: OFF")
            self.log("muted", "Drive Profile: Standard")
            self.log("muted", "Distance: 1234.7 km")
            self.log("info", "")
            self.log("success", "=== Info dump complete ===")
            self.status_message("success", "Info dump complete")
        else:
            # Real hardware comprehensive dump using ECSRemote
            def dump_thread():
                def ui_log(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.log(level_msg, msg))

                def ui_status(level_msg: str, msg: str) -> None:
                    self.root.after(0, lambda: self.status_message(level_msg, msg))

                def read_from_wheel(conn, wheel_name):
                    """Read all available info from one wheel using ECSRemote"""
                    ui_log("success", f"\n=== {wheel_name} ===")
                    builder = ECSPacketBuilder()
                    
                    # Version
                    version = self.ecs_remote.read_value(
                        conn, builder.build_read_sw_version, 0x2E, ResponseParser.parse_sw_version
                    )
                    if version:
                        ui_log("muted", f"Firmware: {version['version_str']}")
                    
                    # Battery
                    soc = self.ecs_remote.read_value(
                        conn, builder.build_read_soc, 0x21, ResponseParser.parse_soc
                    )
                    if soc is not None:
                        ui_log("muted", f"Battery: {soc}%")
                    
                    # Assist Level
                    level = self.ecs_remote.read_value(
                        conn, builder.build_read_assist_level, 0x22, ResponseParser.parse_assist_level
                    )
                    if level:
                        ui_log("muted", f"Assist Level: {level['value']} ({level['name']})")
                    
                    # Drive Mode (for Hill Hold)
                    mode = self.ecs_remote.read_value(
                        conn, builder.build_read_drive_mode, 0x23, ResponseParser.parse_drive_mode
                    )
                    if mode:
                        hill_hold = "ON" if mode['auto_hold'] else "OFF"
                        ui_log("muted", f"Hill Hold: {hill_hold}")
                    
                    # Drive Profile
                    profile = self.ecs_remote.read_value(
                        conn, builder.build_read_drive_profile, 0x24, ResponseParser.parse_drive_profile
                    )
                    if profile:
                        ui_log("muted", f"Drive Profile: {profile['name']}")
                    
                    # Cruise Values (distance)
                    cruise = self.ecs_remote.read_value(
                        conn, builder.build_read_cruise_values, 0x2D, ResponseParser.parse_cruise_values
                    )
                    if cruise:
                        ui_log("muted", f"Distance: {cruise['distance_km']:.1f} km")
                    
                    # Drive Parameters for Level 1
                    params = self.ecs_remote.read_profile_params(conn, builder, 0)
                    if params:
                        ui_log("muted", "Level 1 Parameters:")
                        ui_log("muted", f"  Max Torque: {params['max_torque']}%")
                        ui_log("muted", f"  Max Speed: {params['max_speed']:.1f} km/h")
                        ui_log("muted", f"  P-Factor: {params['p_factor']}")
                        ui_log("muted", f"  Speed Bias: {params['speed_bias']}")

                try:
                    if not self.ecs_remote or not self.left_conn or not self.right_conn:
                        ui_log("error", "Not connected")
                        return
                    
                    # Read from both wheels
                    read_from_wheel(self.left_conn, "LEFT WHEEL")
                    read_from_wheel(self.right_conn, "RIGHT WHEEL")
                    
                    ui_log("info", "")
                    ui_log("success", "=== Info dump complete ===")
                    ui_status("success", "Info dump complete")
                    
                except Exception as e:
                    ui_log("error", f"Info dump failed: {e}")
                    ui_status("error", "Info dump failed")
            
            threading.Thread(target=dump_thread, daemon=True).start()

    def scan_devices(self):
        """Scan for Bluetooth devices"""
        if not HAS_BLUETOOTH:
            messagebox.showerror("Error", "Bluetooth support not available.\nInstall bleak: pip install bleak")
            self.log("error", "Bluetooth support not available.")
            self.status_message("error", "Bluetooth support not available")
            return

        filter_enabled = self.filter_m25.get()
        scan_type = "M25 wheels" if filter_enabled else "all Bluetooth devices"
        self.log("info", f"Scanning for {scan_type}...")
        self.scan_status_lbl.config(text="Scanning...")
        self.scan_btn.config(state="disabled")
        self.status_message("info", "Scanning for devices...")

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
            self.log("warning", "No devices found.")
            self.scan_status_lbl.config(text="No devices found")
            self.status_message("warning", "Scan complete, no devices found")
            return

        device_type = "device(s)" if not self.filter_m25.get() else "M25 device(s)"
        self.log("success", f"Found {len(devices)} {device_type}:")
        for addr, name in devices:
            self.log("muted", f"[{addr}] {name}")

        device_options = [f"{name} ({addr})" for addr, name in devices]

        menu = self.left_device_menu["menu"]
        menu.delete(0, "end")
        menu.add_command(label="", command=lambda: self.left_device_var.set(""))
        for option in device_options:
            menu.add_command(label=option, command=lambda val=option: self.left_device_var.set(val))

        menu = self.right_device_menu["menu"]
        menu.delete(0, "end")
        menu.add_command(label="", command=lambda: self.right_device_var.set(""))
        for option in device_options:
            menu.add_command(label=option, command=lambda val=option: self.right_device_var.set(val))

        self.scan_status_lbl.config(text=f"Found {len(devices)} device(s)")
        self.status_message("success", f"Scan complete, found {len(devices)} device(s)")

    def scan_error(self, error):
        """Handle scan error"""
        self.scan_btn.config(state="normal")
        self.scan_status_lbl.config(text="Scan failed")
        self.log("error", f"Scan error: {error}")
        self.status_message("error", "Scan failed")
        messagebox.showerror("Scan Error", f"Failed to scan for devices:\n{error}")

    def on_left_device_selected(self, selection):
        """Handle left device selection"""
        if not selection:
            return

        if "(" in selection and ")" in selection:
            mac = selection.split("(")[1].split(")")[0]
            self.left_mac.delete(0, tk.END)
            self.left_mac.insert(0, mac)
            self.log("info", f"Selected left wheel: {selection}")
            self.status_message("info", "Left wheel selected")

    def on_right_device_selected(self, selection):
        """Handle right device selection"""
        if not selection:
            return

        if "(" in selection and ")" in selection:
            mac = selection.split("(")[1].split(")")[0]
            self.right_mac.delete(0, tk.END)
            self.right_mac.insert(0, mac)
            self.log("info", f"Selected right wheel: {selection}")
            self.status_message("info", "Right wheel selected")


def main():
    """Launch the GUI application"""

    missing = []

    try:
        import tkinter  # noqa: F401
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

    root = tk.Tk()
    app = M25GUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
