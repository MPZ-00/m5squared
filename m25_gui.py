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
import argparse
import os
import sys
import threading
import asyncio
import time
from pathlib import Path
from datetime import datetime

try:
    from dotenv import load_dotenv
    HAS_DOTENV = True
except ImportError:
    HAS_DOTENV = False
    print("Warning: python-dotenv not installed. Install with: pip install python-dotenv")

# Import core architecture
try:
    from core.types import MapperConfig, SupervisorConfig, SupervisorState
    from core.mapper import Mapper
    from core.supervisor import Supervisor
    from core.transport import MockTransport
    HAS_CORE = True
except ImportError:
    HAS_CORE = False
    print("Warning: Core architecture not available")

try:
    from m25_ecs import ECSPacketBuilder, ECSRemote, ResponseParser
    from m25_utils import parse_key
    HAS_M25_PROTOCOL = True
except ImportError as e:
    HAS_M25_PROTOCOL = False
    print(f"Warning: M25 protocol modules not available: {e}")

try:
    from m25_spp import BluetoothConnection as RFCOMMBluetoothConnection
    HAS_RFCOMM = True
except ImportError:
    RFCOMMBluetoothConnection = None
    HAS_RFCOMM = False

try:
    from m25_bluetooth_ble import M25BluetoothBLE, detect_m25_ble_profile, scan_devices as ble_scan_devices
    HAS_BLE = True
except ImportError:
    M25BluetoothBLE = None
    detect_m25_ble_profile = None
    ble_scan_devices = None
    HAS_BLE = False

from m25_transport import (
    M25_VERSION_AUTO,
    M25_VERSION_V1,
    M25_VERSION_V2,
    TRANSPORT_AUTO,
    TRANSPORT_BLE,
    TRANSPORT_RFCOMM,
    describe_m25_version,
    normalize_m25_version,
    preferred_transport_for_version,
)

HAS_BLUETOOTH = HAS_M25_PROTOCOL and (HAS_BLE or HAS_RFCOMM)
IS_WINDOWS = sys.platform.startswith("win")


class BLEConnectionAdapter:
    """Sync adapter for the async BLE transport used by ECSRemote."""

    def __init__(self, address, key, name="wheel", debug=False, loop=None):
        if not HAS_BLE:
            raise RuntimeError("BLE transport not available")
        self.address = address
        self.key = key
        self.name = name
        self.debug = debug
        self.loop = loop
        self.bt = M25BluetoothBLE(address=address, key=key, name=name, debug=debug)
        self.connected = False
        self.notifications_started = False
        self._loop_lock = threading.RLock()

    def _run(self, coro):
        """Run one coroutine on the shared loop, serialized across threads."""
        if not self.loop:
            return None
        with self._loop_lock:
            return self.loop.run_until_complete(coro)

    def _drain_notifications(self):
        """Drop stale notifications so next transact() reads the current ACK/response."""
        if not self.connected:
            return
        for _ in range(8):
            data = self._run(self.bt.wait_notification(timeout=0.01))
            if data is None:
                break

    def connect(self, channel=6):
        """Connect to device and enable notifications."""
        del channel
        if not self.loop:
            return False
        success = self._run(self.bt.connect(timeout=10))
        if not success:
            return False

        self.notifications_started = self._run(self.bt.start_notifications())
        if not self.notifications_started:
            self._run(self.bt.disconnect())
            return False

        self.connected = True
        time.sleep(0.1)
        return True

    def disconnect(self):
        """Disconnect from device."""
        if self.loop and self.connected:
            self._run(self.bt.disconnect())
        self.connected = False
        self.notifications_started = False

    def transact(self, spp_data, timeout=1.0):
        """Send packet and receive decrypted response (sync interface)."""
        if not self.loop or not self.connected:
            return None

        try:
            self._drain_notifications()
            ok = self._run(self.bt.send_packet(spp_data))
            if not ok:
                return None
            return self._run(self.bt.wait_notification(timeout=max(1.5, timeout)))
        except Exception as e:
            if self.debug:
                print(f"  transact error [{self.name}]: {e}", file=sys.stderr)
            return None


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
            "entry_bg": "#3c3c3c",
            "entry_fg": "#e8e8e8",
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

    def __init__(self, root, default_m25_version=M25_VERSION_AUTO):
        self.root = root
        self.root.title("m5squared - Wheelchair Controller")
        
        # Set window size to use max screen height
        screen_width = root.winfo_screenwidth()
        screen_height = root.winfo_screenheight()
        window_width = min(800, screen_width - 100)  # Leave some margin
        window_height = screen_height - 100  # Leave space for taskbar
        
        # Center the window
        x_position = (screen_width - window_width) // 2
        y_position = 0  # Start at top (leaving space for title bar)
        
        self.root.geometry(f"{window_width}x{window_height}+{x_position}+{y_position}")

        # Connection state
        self.connected = False
        self.scanned_devices = []
        self.left_conn = None
        self.right_conn = None
        self.ecs_remote = None
        self.demo_mode = False
        self.event_loop = None  # For Windows async Bluetooth
        self.default_m25_version = normalize_m25_version(default_m25_version or os.getenv("M25_VERSION"))
        self.connected_transport_summary = "Not connected"
        
        # Core architecture components
        self.use_core_architecture = False
        self.supervisor = None
        self.mapper = None
        self.transport = None
        self.deadman_disabled = False
        
        # System information
        self.bluetooth_mode = "Unknown"
        self.input_device = "None"
        self.input_connected = False

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
        
        # Initialize profile description with default profile
        self.update_profile_description()

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

        self.m25_version_var = tk.StringVar(value=self.default_m25_version)
        self.m25_version_menu = tk.OptionMenu(
            self.scan_frame,
            self.m25_version_var,
            M25_VERSION_AUTO,
            M25_VERSION_V1,
            M25_VERSION_V2,
        )
        self.m25_version_menu.config(width=10)
        self.m25_version_menu.pack(side=tk.LEFT, padx=(0, 10))

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
        
        # Core architecture mode checkbox
        if HAS_CORE:
            self.core_mode_var = tk.BooleanVar(value=True)
            self.core_mode_check = tk.Checkbutton(
                self.conn_frame,
                text="Use Core Architecture (Supervisor with safety)",
                variable=self.core_mode_var,
                command=self.update_system_info,
            )
            self.core_mode_check.grid(row=7, column=0, columnspan=2, sticky=tk.W, pady=(10, 5))
            
            # Deadman disable checkbox
            self.deadman_disable_var = tk.BooleanVar(value=False)
            self.deadman_disable_check = tk.Checkbutton(
                self.conn_frame,
                text="Disable Deadman Requirement ⚠ (USE WITH CAUTION)",
                variable=self.deadman_disable_var,
                command=self.toggle_deadman_disable,
                fg="red"
            )
            self.deadman_disable_check.grid(row=8, column=0, columnspan=2, sticky=tk.W, pady=(0, 5))

        # Connect button
        self.connect_btn = tk.Button(self.conn_frame, text="Connect", command=self.toggle_connection, cursor="hand2")
        self.connect_btn.grid(row=9, column=0, columnspan=2, pady=(10, 0))

        # Control Section
        self.control_frame = tk.LabelFrame(self.main_frame, text="Controls", padx=10, pady=10, font=("", 9, "bold"))
        self.control_frame.grid(row=2, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(0, 10))
        
        # Prevent column 2 from affecting columns 0 and 1
        self.control_frame.columnconfigure(0, weight=0, minsize=100)
        self.control_frame.columnconfigure(1, weight=0, minsize=350)
        self.control_frame.columnconfigure(2, weight=1)
        
        # Keep rows compact (don't expand to fill rowspan height)
        self.control_frame.rowconfigure(0, weight=0)
        self.control_frame.rowconfigure(1, weight=0)

        # Assist level
        self.lbl_assist = tk.Label(self.control_frame, text="Assist Level:")
        self.lbl_assist.grid(row=0, column=0, sticky=tk.W, pady=(5, 0))

        self.assist_frame = tk.Frame(self.control_frame)
        self.assist_frame.grid(row=0, column=1, padx=(5, 5), sticky=tk.W)
        self.assist_level_var = tk.StringVar(value="Level 1 (Normal)")
        self.assist_levels = ["Level 1 (Normal)", "Level 2 (Outdoor)", "Level 3 (Learning)"]
        self.assist_level_menu = tk.OptionMenu(self.assist_frame, self.assist_level_var, *self.assist_levels)
        self.assist_level_menu.config(width=18)
        self.assist_level_menu.pack(side=tk.LEFT)
        
        self.set_level_btn = tk.Button(self.assist_frame, text="Set Level", command=self.set_assist_level, state="disabled", cursor="hand2", width=10)
        self.set_level_btn.pack(side=tk.LEFT, padx=(5, 0))

        # Drive profile
        self.lbl_profile = tk.Label(self.control_frame, text="Drive Profile:")
        self.lbl_profile.grid(row=1, column=0, sticky=tk.W, pady=(0, 5))

        self.profile_frame = tk.Frame(self.control_frame)
        self.profile_frame.grid(row=1, column=1, padx=(5, 5), sticky=tk.W)
        self.profile_var = tk.StringVar(value="Standard")
        self.profile_var.trace_add("write", self.update_profile_description)
        self.profiles = ["Standard", "Sensitive", "Soft", "Active", "SensitivePlus"]
        self.profile_menu = tk.OptionMenu(self.profile_frame, self.profile_var, *self.profiles)
        self.profile_menu.config(width=18)
        self.profile_menu.pack(side=tk.LEFT)
        
        self.set_profile_btn = tk.Button(self.profile_frame, text="Set Profile", command=self.set_drive_profile, state="disabled", cursor="hand2", width=10)
        self.set_profile_btn.pack(side=tk.LEFT, padx=(5, 0))

        # Profile description (multi-line) - spans both rows independently
        self.profile_desc_frame = tk.Frame(self.control_frame)
        self.profile_desc_frame.grid(row=0, column=2, rowspan=2, sticky=(tk.W, tk.E, tk.N), padx=(20, 0), pady=5)
        self.profile_desc_text = tk.Text(self.profile_desc_frame, font=("TkDefaultFont", 9), height=6, width=50, wrap=tk.WORD, relief=tk.FLAT, borderwidth=0)
        self.profile_desc_text.pack(fill=tk.BOTH, expand=True)
        self.profile_desc_text.config(state=tk.DISABLED)

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

        # Max speed controls
        self.lbl_max_speed = tk.Label(self.control_frame, text="Max Speed (km/h):")
        self.lbl_max_speed.grid(row=3, column=0, sticky=tk.W, pady=5)
        
        # Max speed frame for Level 1 and Level 2
        self.max_speed_frame = tk.Frame(self.control_frame)
        self.max_speed_frame.grid(row=3, column=1, columnspan=2, sticky=tk.W, padx=(5, 0), pady=5)
        
        # Level 1 controls
        tk.Label(self.max_speed_frame, text="Level 1:").grid(row=0, column=0, sticky=tk.W)
        self.max_speed_level1 = tk.DoubleVar(value=4.0)
        self.max_speed_level1_minus = tk.Button(
            self.max_speed_frame, text="-", width=2,
            command=lambda: self.adjust_speed(self.max_speed_level1, -0.5),
            state="disabled", cursor="hand2"
        )
        self.max_speed_level1_minus.grid(row=0, column=1, padx=(5, 2))
        self.max_speed_level1_entry = tk.Entry(
            self.max_speed_frame, textvariable=self.max_speed_level1,
            width=5, justify=tk.CENTER, state="readonly"
        )
        self.max_speed_level1_entry.grid(row=0, column=2, padx=2)
        self.max_speed_level1_plus = tk.Button(
            self.max_speed_frame, text="+", width=2,
            command=lambda: self.adjust_speed(self.max_speed_level1, 0.5),
            state="disabled", cursor="hand2"
        )
        self.max_speed_level1_plus.grid(row=0, column=3, padx=(2, 10))
        
        # Level 2 controls
        tk.Label(self.max_speed_frame, text="Level 2:").grid(row=0, column=4, sticky=tk.W)
        self.max_speed_level2 = tk.DoubleVar(value=6.0)
        self.max_speed_level2_minus = tk.Button(
            self.max_speed_frame, text="-", width=2,
            command=lambda: self.adjust_speed(self.max_speed_level2, -0.5),
            state="disabled", cursor="hand2"
        )
        self.max_speed_level2_minus.grid(row=0, column=5, padx=(5, 2))
        self.max_speed_level2_entry = tk.Entry(
            self.max_speed_frame, textvariable=self.max_speed_level2,
            width=5, justify=tk.CENTER, state="readonly"
        )
        self.max_speed_level2_entry.grid(row=0, column=6, padx=2)
        self.max_speed_level2_plus = tk.Button(
            self.max_speed_frame, text="+", width=2,
            command=lambda: self.adjust_speed(self.max_speed_level2, 0.5),
            state="disabled", cursor="hand2"
        )
        self.max_speed_level2_plus.grid(row=0, column=7, padx=(2, 10))
        
        self.set_max_speed_btn = tk.Button(self.max_speed_frame, text="Set Max Speed", command=self.set_max_speed, state="disabled", cursor="hand2")
        self.set_max_speed_btn.grid(row=0, column=8, padx=(10, 0))

        # Status buttons
        self.btn_frame = tk.Frame(self.control_frame)
        self.btn_frame.grid(row=4, column=0, columnspan=3, pady=(10, 0))

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
        
        # Drive Test Section - grid layout for aligned rows
        self.drive_test_frame = tk.LabelFrame(self.control_frame, text="Quick Drive Test", padx=10, pady=10, font=("", 9, "bold"))
        self.drive_test_frame.grid(row=5, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=(10, 0))
        self.drive_test_frame.columnconfigure(0, weight=1)
        self.drive_test_frame.columnconfigure(1, weight=1)

        # Row 0: description spanning both columns, centered
        self.drive_test_label = tk.Label(self.drive_test_frame, text="Test sequence: Forward → Backward → Left → Right")
        self.drive_test_label.grid(row=0, column=0, columnspan=2, pady=(0, 5))

        # Row 1: Run Drive Test button centered
        self.drive_test_btn = tk.Button(self.drive_test_frame, text="Run Drive Test", command=self.run_drive_test, state="disabled", cursor="hand2")
        self.drive_test_btn.grid(row=1, column=0, columnspan=2, pady=5)

        # Row 2: single direction - label right, controls left
        self.single_dir_label = tk.Label(self.drive_test_frame, text="Single direction:", anchor=tk.E)
        self.single_dir_label.grid(row=2, column=0, sticky=tk.E, padx=(0, 8), pady=3)

        self.single_dir_frame = tk.Frame(self.drive_test_frame)
        self.single_dir_frame.grid(row=2, column=1, sticky=tk.W, pady=3)

        self.single_dir_var = tk.StringVar(value="Forward")
        self.single_dir_menu = tk.OptionMenu(self.single_dir_frame, self.single_dir_var, "Forward", "Backward")
        self.single_dir_menu.config(width=10)
        self.single_dir_menu.pack(side=tk.LEFT, padx=(0, 5))

        self.single_dir_btn = tk.Button(self.single_dir_frame, text="Run", command=self.run_single_direction_test, state="disabled", cursor="hand2", width=8)
        self.single_dir_btn.pack(side=tk.LEFT)

        # Row 3: quick buttons - label right, buttons left
        self.quick_label = tk.Label(self.drive_test_frame, text="Quick:", anchor=tk.E)
        self.quick_label.grid(row=3, column=0, sticky=tk.E, padx=(0, 8), pady=3)

        self.quick_move_frame = tk.Frame(self.drive_test_frame)
        self.quick_move_frame.grid(row=3, column=1, sticky=tk.W, pady=3)

        self.quick_fwd_btn = tk.Button(self.quick_move_frame, text="Forward", command=lambda: self.run_short_movement("forward"), state="disabled", cursor="hand2", width=10)
        self.quick_fwd_btn.pack(side=tk.LEFT, padx=(0, 5))

        self.quick_bwd_btn = tk.Button(self.quick_move_frame, text="Backward", command=lambda: self.run_short_movement("backward"), state="disabled", cursor="hand2", width=10)
        self.quick_bwd_btn.pack(side=tk.LEFT)

        # Row 4: status label centered
        self.drive_test_status = tk.Label(self.drive_test_frame, text="")
        self.drive_test_status.grid(row=4, column=0, columnspan=2, pady=(3, 0))

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
        
        # System info panel
        self.info_frame = tk.Frame(self.main_frame, relief=tk.SUNKEN, borderwidth=1, padx=5, pady=3)
        self.info_frame.grid(row=5, column=0, columnspan=3, sticky=(tk.W, tk.E))
        
        self.info_bluetooth_lbl = tk.Label(self.info_frame, text="Bluetooth: Detecting...", anchor=tk.W, font=("TkDefaultFont", 8))
        self.info_bluetooth_lbl.pack(side=tk.LEFT, padx=5)
        
        self.info_input_lbl = tk.Label(self.info_frame, text="Input: None", anchor=tk.W, font=("TkDefaultFont", 8))
        self.info_input_lbl.pack(side=tk.LEFT, padx=5)
        
        self.info_arch_lbl = tk.Label(self.info_frame, text="Mode: Legacy", anchor=tk.W, font=("TkDefaultFont", 8))
        self.info_arch_lbl.pack(side=tk.LEFT, padx=5)

        # Configure row weights for resizing
        self.main_frame.rowconfigure(3, weight=1)
        
        # Detect system configuration
        self.detect_bluetooth_mode()
        self.detect_input_device()
        
        # Start periodic input device check
        self.check_input_devices_periodically()

    def _theme_widget(self, widget, widget_type="label", **kwargs):
        """
        Helper to apply theme to a widget based on its type.
        
        Args:
            widget: The widget to theme
            widget_type: Type of widget (label, button, entry, checkbox, optionmenu, frame)
            **kwargs: Additional custom colors (e.g., fg="red" to override)
        """
        if not widget or not hasattr(widget, 'configure'):
            return
        
        theme = self.THEMES[self.current_theme]
        
        config = {}
        
        if widget_type == "label":
            config = {"bg": theme["bg"], "fg": theme["fg"]}
        
        elif widget_type == "button":
            config = {
                "bg": theme["button_bg"],
                "fg": theme["button_fg"],
                "activebackground": theme["select_bg"],
                "activeforeground": theme["select_fg"],
            }
        
        elif widget_type == "entry":
            config = {
                "bg": theme["entry_bg"],
                "fg": theme["entry_fg"],
                "insertbackground": theme["entry_fg"],
                "selectbackground": theme["select_bg"],
                "selectforeground": theme["select_fg"],
                "readonlybackground": theme["entry_bg"],
                "disabledbackground": theme["entry_bg"],
                "disabledforeground": theme["entry_fg"],
            }
        
        elif widget_type == "checkbox":
            config = {
                "bg": theme["bg"],
                "fg": theme["fg"],
                "activebackground": theme["bg"],
                "activeforeground": theme["fg"],
                "selectcolor": theme["entry_bg"],
            }
        
        elif widget_type == "optionmenu":
            config = {
                "bg": theme["button_bg"],
                "fg": theme["button_fg"],
                "activebackground": theme["select_bg"],
                "activeforeground": theme["select_fg"],
                "highlightthickness": 0,
            }
            # Also theme the dropdown menu
            try:
                menu = widget["menu"]
                menu.configure(
                    bg=theme["button_bg"],
                    fg=theme["button_fg"],
                    activebackground=theme["select_bg"],
                    activeforeground=theme["select_fg"]
                )
            except:
                pass
        
        elif widget_type == "frame":
            config = {"bg": theme["bg"]}
        
        elif widget_type == "labelframe":
            config = {"bg": theme["bg"], "fg": theme["fg"]}
        
        # Override with any custom kwargs
        config.update(kwargs)
        
        # Apply configuration
        try:
            widget.configure(**config)
        except:
            pass  # Some widgets don't support all options
    
    def apply_theme(self):
        """Apply the current theme to all widgets"""
        theme = self.THEMES[self.current_theme]

        # Root window
        self.root.configure(bg=theme["bg"])

        # Main frame
        self._theme_widget(self.main_frame, "frame")

        # Title
        self._theme_widget(self.title_frame, "frame")
        self._theme_widget(self.title_label, "label")

        # Theme button
        if hasattr(self, "theme_btn"):
            text = "☀ Light Mode" if self.current_theme == "dark" else "🌙 Dark Mode"
            self._theme_widget(self.theme_btn, "button", text=text)

        # Connection frame
        self._theme_widget(self.conn_frame, "labelframe")
        self._theme_widget(self.scan_frame, "frame")
        self._theme_widget(self.scan_btn, "button")
        self._theme_widget(self.filter_check, "checkbox")
        self._theme_widget(self.m25_version_menu, "optionmenu")
        self._theme_widget(self.scan_status_lbl, "label")
        
        # Device selection
        self._theme_widget(self.lbl_left_device, "label")
        self._theme_widget(self.lbl_right_device, "label")
        self._theme_widget(self.left_device_frame, "frame")
        self._theme_widget(self.right_device_frame, "frame")
        self._theme_widget(self.left_device_menu, "optionmenu")
        self._theme_widget(self.right_device_menu, "optionmenu")
        
        # MAC and Key entries
        self._theme_widget(self.lbl_left_mac, "label")
        self._theme_widget(self.lbl_left_key, "label")
        self._theme_widget(self.lbl_right_mac, "label")
        self._theme_widget(self.lbl_right_key, "label")
        self._theme_widget(self.left_mac, "entry")
        self._theme_widget(self.left_key, "entry")
        self._theme_widget(self.right_mac, "entry")
        self._theme_widget(self.right_key, "entry")
        
        # Core architecture checkboxes
        if HAS_CORE and hasattr(self, "core_mode_check"):
            self._theme_widget(self.core_mode_check, "checkbox")
            
            if hasattr(self, "deadman_disable_check"):
                # Keep deadman checkbox red when enabled
                if self.deadman_disable_var.get():
                    self._theme_widget(self.deadman_disable_check, "checkbox", fg="red", activeforeground="red")
                else:
                    self._theme_widget(self.deadman_disable_check, "checkbox")
        
        self._theme_widget(self.connect_btn, "button")
        
        # Controls
        self._theme_widget(self.control_frame, "labelframe")
        self._theme_widget(self.assist_frame, "frame")
        self._theme_widget(self.lbl_assist, "label")
        self._theme_widget(self.assist_level_menu, "optionmenu")
        self._theme_widget(self.set_level_btn, "button")
        
        self._theme_widget(self.profile_frame, "frame")
        self._theme_widget(self.lbl_profile, "label")
        self._theme_widget(self.profile_menu, "optionmenu")
        self._theme_widget(self.set_profile_btn, "button")
        
        self._theme_widget(self.lbl_hill_hold, "label")
        self._theme_widget(self.hill_hold_check, "checkbox")
        
        # Max speed controls
        self._theme_widget(self.max_speed_frame, "frame")
        self._theme_widget(self.lbl_max_speed, "label")
        if hasattr(self, "max_speed_level1_entry"):
            self._theme_widget(self.max_speed_level1_entry, "entry")
        if hasattr(self, "max_speed_level2_entry"):
            self._theme_widget(self.max_speed_level2_entry, "entry")
        if hasattr(self, "profile_desc_frame"):
            self._theme_widget(self.profile_desc_frame, "frame")
        if hasattr(self, "profile_desc_text"):
            theme = self.THEMES[self.current_theme]
            self.profile_desc_text.config(
                bg=theme["bg"],
                fg=theme["fg"],
                insertbackground=theme["fg"]
            )
            # Configure text tags with colors
            self.profile_desc_text.tag_configure('profile_name', foreground=theme["text"]["info"], font=("TkDefaultFont", 10, "bold"))
            self.profile_desc_text.tag_configure('level_info', foreground=theme["text"]["success"])
            self.profile_desc_text.tag_configure('best_for', foreground=theme["text"]["warning"], font=("TkDefaultFont", 9, "italic"))
        for widget in self.max_speed_frame.winfo_children():
            if isinstance(widget, tk.Label):
                self._theme_widget(widget, "label")
            elif isinstance(widget, tk.Entry):
                self._theme_widget(widget, "entry")
            elif isinstance(widget, tk.Button):
                self._theme_widget(widget, "button")
        
        # Status buttons
        self._theme_widget(self.btn_frame, "frame")
        for btn in (self.read_battery_btn, self.read_status_btn, self.read_version_btn, 
                    self.read_profile_btn, self.info_dump_btn):
            self._theme_widget(btn, "button")
        
        # Drive Test Section
        if hasattr(self, "drive_test_frame"):
            self._theme_widget(self.drive_test_frame, "labelframe")
            self._theme_widget(self.drive_test_label, "label")
            self._theme_widget(self.drive_test_btn, "button")
            self._theme_widget(self.single_dir_frame, "frame")
            self._theme_widget(self.single_dir_label, "label")
            self._theme_widget(self.single_dir_menu, "optionmenu")
            self._theme_widget(self.single_dir_btn, "button")
            self._theme_widget(self.quick_move_frame, "frame")
            self._theme_widget(self.quick_label, "label")
            self._theme_widget(self.quick_fwd_btn, "button")
            self._theme_widget(self.quick_bwd_btn, "button")
            self._theme_widget(self.drive_test_status, "label")
        
        # Output
        self._theme_widget(self.output_frame, "labelframe")
        if hasattr(self, "output"):
            self.output.configure(
                bg=theme["output_bg"],
                fg=theme["output_fg"],
                insertbackground=theme["output_fg"],
                selectbackground=theme["select_bg"],
                selectforeground=theme["select_fg"],
            )
            self._apply_output_tags()
        
        # Status bar
        if hasattr(self, "status"):
            self.status.configure(bg=theme["bg"])
        
        # System info panel
        if hasattr(self, "info_frame"):
            self._theme_widget(self.info_frame, "frame")
        if hasattr(self, "info_bluetooth_lbl"):
            self._theme_widget(self.info_bluetooth_lbl, "label")
        if hasattr(self, "info_input_lbl"):
            self._theme_widget(self.info_input_lbl, "label")
        if hasattr(self, "info_arch_lbl"):
            self._theme_widget(self.info_arch_lbl, "label")

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
    
    def update_profile_description(self, *args):
        """Update the profile description label when profile selection changes"""
        profile_descriptions = {
            "Standard": (
                "Standard (Balanced, general use)\n"
                "  Level 1: 45% torque, 4.0 km/h, medium sensitivity\n"
                "  Level 2: 75% torque, 8.5 km/h\n"
                "  Moderate startup, medium coasting\n"
                "  Best for: Everyday use, beginners"
            ),
            "Active": (
                "Active (Sporty, responsive)\n"
                "  Level 1: 45% torque, 4.5 km/h, higher sensitivity\n"
                "  Level 2: 90% torque, 8.5 km/h\n"
                "  Fast startup, longer coasting\n"
                "  Best for: Outdoor use, experienced users"
            ),
            "Sensitive": (
                "Sensitive (High support, easy control)\n"
                "  Level 1: 60% torque, 4.0 km/h\n"
                "  Level 2: 95% torque, 8.5 km/h, high sensitivity\n"
                "  Medium startup, longer coasting\n"
                "  Best for: Users with limited upper body strength"
            ),
            "Soft": (
                "Soft (Gentle, conservative)\n"
                "  Level 1: 35% torque, 3.0 km/h, low sensitivity\n"
                "  Level 2: 50% torque, 8.5 km/h\n"
                "  Slower startup, requires more push force\n"
                "  Best for: Beginners, crowded spaces, maximum control"
            ),
            "SensitivePlus": (
                "SensitivePlus (Maximum assistance)\n"
                "  Level 1: 65% torque, 5.0 km/h, highest sensitivity\n"
                "  Level 2: 100% torque, 8.5 km/h\n"
                "  Fastest response, longest coasting\n"
                "  Best for: Users needing maximum motor support"
            )
        }
        
        # Profile default speeds (Level 1, Level 2)
        profile_speeds = {
            "Standard": (4.0, 8.5),
            "Active": (4.5, 8.5),
            "Sensitive": (4.0, 8.5),
            "Soft": (3.0, 8.5),
            "SensitivePlus": (5.0, 8.5)
        }
        
        profile = self.profile_var.get()
        description = profile_descriptions.get(profile, "")
        
        # Update max speed values to profile defaults (user can override)
        if hasattr(self, 'max_speed_level1') and hasattr(self, 'max_speed_level2'):
            speeds = profile_speeds.get(profile, (4.0, 8.5))
            self.max_speed_level1.set(speeds[0])
            self.max_speed_level2.set(speeds[1])
        
        if hasattr(self, 'profile_desc_text'):
            self.profile_desc_text.config(state=tk.NORMAL)
            self.profile_desc_text.delete(1.0, tk.END)
            
            # Add formatted text with colors
            lines = description.split('\n')
            if lines:
                # First line - profile name (bold and colored)
                self.profile_desc_text.insert(tk.END, lines[0] + '\n', 'profile_name')
                # Remaining lines
                for line in lines[1:]:
                    if 'Best for:' in line:
                        self.profile_desc_text.insert(tk.END, line + '\n', 'best_for')
                    elif 'Level' in line:
                        self.profile_desc_text.insert(tk.END, line + '\n', 'level_info')
                    else:
                        self.profile_desc_text.insert(tk.END, line + '\n')
            
            self.profile_desc_text.config(state=tk.DISABLED)
    
    def update_system_info(self):
        """Update system information labels"""
        # Bluetooth mode
        selected_version = describe_m25_version(self.get_selected_m25_version())
        bt_text = f"Bluetooth: {self.bluetooth_mode} | {selected_version}"
        if hasattr(self, 'info_bluetooth_lbl'):
            self.info_bluetooth_lbl.config(text=bt_text)
        
        # Input device
        input_status = "Connected" if self.input_connected else "None"
        input_text = f"Input: {self.input_device} ({input_status})"
        if hasattr(self, 'info_input_lbl'):
            self.info_input_lbl.config(text=input_text)
        
        # Architecture mode - check both checkbox state and actual usage
        if HAS_CORE and hasattr(self, 'core_mode_var'):
            # When connected, use actual state; otherwise use checkbox state
            if self.connected:
                arch = "Core" if self.use_core_architecture else "Legacy"
            else:
                arch = "Core (ready)" if self.core_mode_var.get() else "Legacy"
        else:
            arch = "Legacy (Core N/A)" if not HAS_CORE else "Legacy"
        
        arch_text = f"Mode: {arch}"
        if hasattr(self, 'info_arch_lbl'):
            self.info_arch_lbl.config(text=arch_text)
    
    def detect_bluetooth_mode(self):
        """Detect which Bluetooth implementation is being used"""
        modes = []
        if HAS_BLE:
            modes.append("BLE")
        if HAS_RFCOMM:
            modes.append("RFCOMM")
        self.bluetooth_mode = "/".join(modes) if modes else "Unknown"
        self.update_system_info()

    def get_selected_m25_version(self):
        """Return the selected M25 generation mode."""
        return normalize_m25_version(self.m25_version_var.get())

    def _ensure_event_loop(self):
        """Create a private event loop for BLE tasks when needed."""
        if not self.event_loop or self.event_loop.is_closed():
            self.event_loop = asyncio.new_event_loop()
        return self.event_loop

    def _detect_transport_for_wheel(self, mac, selected_version, loop):
        """Choose BLE or RFCOMM for one wheel based on explicit mode or BLE probing."""
        preferred = preferred_transport_for_version(selected_version)
        if preferred == TRANSPORT_BLE:
            if not HAS_BLE:
                raise RuntimeError("BLE transport requested, but bleak is not available")
            return TRANSPORT_BLE, "explicit M25V2 selection"

        if preferred == TRANSPORT_RFCOMM:
            if IS_WINDOWS:
                raise RuntimeError("M25V1 RFCOMM mode is not supported on Windows in this app")
            if not HAS_RFCOMM:
                raise RuntimeError("RFCOMM transport requested, but PyBluez/socket support is not available")
            return TRANSPORT_RFCOMM, "explicit M25V1 selection"

        if IS_WINDOWS:
            if not HAS_BLE:
                raise RuntimeError("BLE transport is required on Windows but not available")
            return TRANSPORT_BLE, "auto: Windows BLE only"

        if HAS_BLE and detect_m25_ble_profile:
            profile = loop.run_until_complete(detect_m25_ble_profile(mac, timeout=5))
            if profile == "M25V2" or (profile and profile.startswith("Fake")):
                return TRANSPORT_BLE, f"auto detected {profile}"
            if profile == "M25V1":
                return TRANSPORT_RFCOMM, "auto detected M25V1"

        if HAS_RFCOMM:
            return TRANSPORT_RFCOMM, "auto fallback to RFCOMM"
        if HAS_BLE:
            return TRANSPORT_BLE, "auto fallback to BLE"
        raise RuntimeError("No usable Bluetooth transport available")

    def _make_connection(self, transport_kind, address, key_bytes, name, loop):
        """Create a wheel connection for the requested transport."""
        if transport_kind == TRANSPORT_BLE:
            return BLEConnectionAdapter(address, key_bytes, name=name, debug=False, loop=loop)
        return RFCOMMBluetoothConnection(address, key_bytes, name=name, debug=False)
    
    def detect_input_device(self):
        """Detect connected input devices (gamepad/joystick)"""
        try:
            import pygame
            pygame.init()
            pygame.joystick.init()
            
            joystick_count = pygame.joystick.get_count()
            if joystick_count > 0:
                joystick = pygame.joystick.Joystick(0)
                joystick.init()
                self.input_device = joystick.get_name()
                self.input_connected = True
            else:
                self.input_device = "Keyboard"
                self.input_connected = False
            
            pygame.quit()
        except ImportError:
            self.input_device = "Keyboard"
            self.input_connected = False
        except Exception as e:
            self.input_device = f"Error: {str(e)[:20]}"
            self.input_connected = False
        
        self.update_system_info()
    
    def check_input_devices_periodically(self):
        """Periodically check for input device changes"""
        self.detect_input_device()
        # Check every 5 seconds
        self.root.after(5000, self.check_input_devices_periodically)

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

    def toggle_mpp_mode(self):
        """Toggle M++ mode (8 km/h support)"""
        if self.enable_mpp.get():
            self.log("info", "M++ mode enabled: Speeds up to 8.5 km/h available")
            # Adjust current values if they would exceed new limit
            if self.max_speed_level1.get() > 6.0:
                self.max_speed_level1.set(6.0)
            if self.max_speed_level2.get() > 6.0:
                self.max_speed_level2.set(6.0)
        else:
            self.log("info", "M++ mode disabled: Speeds limited to 6.0 km/h")
    
    def adjust_speed(self, speed_var, delta):
        """Adjust speed by delta, keeping within valid range"""
        current = speed_var.get()
        new_value = current + delta
        # Clamp to valid range (2.0 to 8.5 km/h)
        new_value = max(2.0, min(8.5, new_value))
        speed_var.set(new_value)
    
    def run_drive_test(self):
        """Run a quick drive test sequence"""
        self.log("info", "Starting Quick Drive Test...")
        self.status_message("info", "Running drive test...")
        
        if self.demo_mode:
            self.log("warning", "Demo mode: Drive test simulated")
            self.status_message("success", "Drive test completed (simulated)")
            return
        
        # Real hardware test
        def test_thread():
            def ui_log(level_msg: str, msg: str) -> None:
                self.root.after(0, lambda: self.log(level_msg, msg))

            def ui_status(level_msg: str, msg: str) -> None:
                self.root.after(0, lambda: self.status_message(level_msg, msg))
            
            def ui_test_status(msg: str) -> None:
                self.root.after(0, lambda: self.drive_test_status.config(text=msg))

            try:
                if not self.ecs_remote or not self.left_conn or not self.right_conn:
                    ui_log("error", "Not connected")
                    ui_status("error", "Drive test failed: Not connected")
                    return
                
                builder = ECSPacketBuilder()
                test_speed = 30  # Low speed for safety (out of ~100)
                test_duration = 1.0  # 1 second per movement
                
                # Test sequence: Forward, Backward, Left Turn, Right Turn
                test_sequence = [
                    ("Forward", test_speed, test_speed),
                    ("Stop", 0, 0),
                    ("Backward", -test_speed, -test_speed),
                    ("Stop", 0, 0),
                    ("Left Turn", -test_speed//2, test_speed//2),
                    ("Stop", 0, 0),
                    ("Right Turn", test_speed//2, -test_speed//2),
                    ("Stop", 0, 0),
                ]
                
                ui_log("info", f"Drive test: {len([s for s in test_sequence if s[0] != 'Stop'])} movements at speed {test_speed}")
                
                for i, (label, left_speed, right_speed) in enumerate(test_sequence):
                    ui_test_status(f"Step {i+1}/{len(test_sequence)}: {label}")
                    ui_log("info", f"  -> {label} (L:{left_speed}, R:{right_speed})")
                    
                    # Build speed packets
                    left_packet = builder.build_write_remote_speed(left_speed)
                    right_packet = builder.build_write_remote_speed(right_speed)
                    
                    # Send to both wheels
                    left_ok = self.ecs_remote.write_value(self.left_conn, left_packet, "write_remote_speed")
                    right_ok = self.ecs_remote.write_value(self.right_conn, right_packet, "write_remote_speed")
                    
                    if not (left_ok and right_ok):
                        ui_log("warning", f"  Warning: Partial command failure: Left={left_ok}, Right={right_ok}")
                    
                    # Wait for movement or stop duration
                    import time
                    if label == "Stop":
                        time.sleep(0.3)  # Short pause between movements
                    else:
                        time.sleep(test_duration)
                
                # Final stop
                ui_log("info", "  -> Final stop")
                stop_packet = builder.build_write_remote_speed(0)
                self.ecs_remote.write_value(self.left_conn, stop_packet, "write_remote_speed")
                self.ecs_remote.write_value(self.right_conn, stop_packet, "write_remote_speed")
                
                ui_test_status("Drive test completed")
                ui_log("success", "Drive test completed successfully")
                ui_status("success", "Drive test completed")
                
            except Exception as e:
                ui_log("error", f"Drive test failed: {e}")
                ui_status("error", "Drive test failed")
                ui_test_status("Test failed")
                
                # Emergency stop on error
                try:
                    builder = ECSPacketBuilder()
                    stop_packet = builder.build_write_remote_speed(0)
                    if self.left_conn:
                        self.ecs_remote.write_value(self.left_conn, stop_packet, "write_remote_speed")
                    if self.right_conn:
                        self.ecs_remote.write_value(self.right_conn, stop_packet, "write_remote_speed")
                except:
                    pass
        
        threading.Thread(target=test_thread, daemon=True).start()

    def run_single_direction_test(self):
        """Run a single direction test (user-chosen: Forward or Backward)"""
        direction = self.single_dir_var.get()
        self.log("info", f"Single direction test: {direction}")
        self.status_message("info", f"Running {direction} test...")

        if self.demo_mode:
            self.log("warning", f"Demo mode: {direction} test simulated")
            self.status_message("success", f"{direction} test completed (simulated)")
            return

        def test_thread():
            def ui_log(level_msg, msg):
                self.root.after(0, lambda: self.log(level_msg, msg))
            def ui_status(level_msg, msg):
                self.root.after(0, lambda: self.status_message(level_msg, msg))
            def ui_test_status(msg):
                self.root.after(0, lambda: self.drive_test_status.config(text=msg))

            try:
                if not self.ecs_remote or not self.left_conn or not self.right_conn:
                    ui_log("error", "Not connected")
                    ui_status("error", "Test failed: Not connected")
                    return

                import time
                builder = ECSPacketBuilder()
                test_speed = 30
                test_duration = 1.5
                speed = test_speed if direction == "Forward" else -test_speed

                ui_test_status(f"{direction}...")
                ui_log("info", f"  -> {direction} (speed: {speed})")

                pkt = builder.build_write_remote_speed(speed)
                self.ecs_remote.write_value(self.left_conn, pkt, "write_remote_speed")
                self.ecs_remote.write_value(self.right_conn, pkt, "write_remote_speed")

                time.sleep(test_duration)

                stop_pkt = builder.build_write_remote_speed(0)
                self.ecs_remote.write_value(self.left_conn, stop_pkt, "write_remote_speed")
                self.ecs_remote.write_value(self.right_conn, stop_pkt, "write_remote_speed")

                ui_test_status(f"{direction} test done")
                ui_log("success", f"{direction} test completed")
                ui_status("success", f"{direction} test completed")

            except Exception as e:
                ui_log("error", f"Single direction test failed: {e}")
                ui_status("error", "Test failed")
                ui_test_status("Test failed")
                try:
                    builder = ECSPacketBuilder()
                    stop_pkt = builder.build_write_remote_speed(0)
                    if self.left_conn:
                        self.ecs_remote.write_value(self.left_conn, stop_pkt, "write_remote_speed")
                    if self.right_conn:
                        self.ecs_remote.write_value(self.right_conn, stop_pkt, "write_remote_speed")
                except:
                    pass

        threading.Thread(target=test_thread, daemon=True).start()

    def run_short_movement(self, direction: str):
        """Send a short 0.5s burst in the given direction (forward or backward)"""
        if self.demo_mode:
            self.log("warning", f"Demo mode: quick {direction} simulated")
            return

        def move_thread():
            def ui_log(level_msg, msg):
                self.root.after(0, lambda: self.log(level_msg, msg))
            def ui_test_status(msg):
                self.root.after(0, lambda: self.drive_test_status.config(text=msg))

            try:
                if not self.ecs_remote or not self.left_conn or not self.right_conn:
                    ui_log("error", "Not connected")
                    return

                import time
                builder = ECSPacketBuilder()
                speed = 30 if direction == "forward" else -30
                label = "Forward" if direction == "forward" else "Backward"

                ui_test_status(f"Quick {label}...")
                ui_log("info", f"  -> Quick {label}")

                pkt = builder.build_write_remote_speed(speed)
                self.ecs_remote.write_value(self.left_conn, pkt, "write_remote_speed")
                self.ecs_remote.write_value(self.right_conn, pkt, "write_remote_speed")
                time.sleep(0.5)
                stop_pkt = builder.build_write_remote_speed(0)
                self.ecs_remote.write_value(self.left_conn, stop_pkt, "write_remote_speed")
                self.ecs_remote.write_value(self.right_conn, stop_pkt, "write_remote_speed")

                ui_test_status(f"Quick {label} done")

            except Exception as e:
                ui_log("error", f"Quick movement failed: {e}")
                ui_test_status("Movement failed")
                try:
                    builder = ECSPacketBuilder()
                    stop_pkt = builder.build_write_remote_speed(0)
                    if self.left_conn:
                        self.ecs_remote.write_value(self.left_conn, stop_pkt, "write_remote_speed")
                    if self.right_conn:
                        self.ecs_remote.write_value(self.right_conn, stop_pkt, "write_remote_speed")
                except:
                    pass

        threading.Thread(target=move_thread, daemon=True).start()

    def enable_controls(self, enabled=True):
        """Enable or disable control buttons"""
        state = "normal" if enabled else "disabled"
        self.set_level_btn.config(state=state)
        self.set_profile_btn.config(state=state)
        self.hill_hold_check.config(state=state)
        self.max_speed_level1_minus.config(state=state)
        self.max_speed_level1_plus.config(state=state)
        self.max_speed_level2_minus.config(state=state)
        self.max_speed_level2_plus.config(state=state)
        self.set_max_speed_btn.config(state=state)
        self.read_battery_btn.config(state=state)
        self.read_status_btn.config(state=state)
        self.read_version_btn.config(state=state)
        self.read_profile_btn.config(state=state)
        self.info_dump_btn.config(state=state)
        self.drive_test_btn.config(state=state)
        self.single_dir_btn.config(state=state)
        self.quick_fwd_btn.config(state=state)
        self.quick_bwd_btn.config(state=state)
    
    def toggle_deadman_disable(self):
        """Toggle deadman requirement with confirmation"""
        if self.deadman_disable_var.get():
            # Enabling - show warning
            confirm = messagebox.askyesno(
                "SAFETY WARNING",
                "Disabling the deadman requirement removes a critical safety feature!\n\n"
                "Without deadman control:\n"
                "- The wheelchair may move without active control\n"
                "- You lose the ability to quickly stop by releasing a button\n"
                "- Risk of unintended movement increases significantly\n\n"
                "This should ONLY be used for testing in a controlled environment.\n\n"
                "Are you ABSOLUTELY SURE you want to disable the deadman requirement?",
                icon='warning'
            )
            
            if not confirm:
                # User said no, revert the checkbox
                self.deadman_disable_var.set(False)
                return
            
            self.deadman_disabled = True
            self.log("warning", "DEADMAN REQUIREMENT DISABLED - USE EXTREME CAUTION")
            self.status_message("warning", "Deadman disabled")
            # Make checkbox red to indicate danger
            if HAS_CORE:
                theme = self.THEMES[self.current_theme]
                self.deadman_disable_check.config(
                    fg="red",
                    activeforeground="red",
                    font=("", 9, "bold")
                )
        else:
            # Disabling - safe, no confirmation needed
            self.deadman_disabled = False
            self.log("success", "Deadman requirement re-enabled")
            self.status_message("success", "Deadman enabled")
            if HAS_CORE:
                # Reset checkbox appearance
                theme = self.THEMES[self.current_theme]
                self.deadman_disable_check.config(
                    fg=theme["fg"],
                    activeforeground=theme["fg"],
                    font=("", 9, "normal")
                )
        
        # Update system info display
        self.update_system_info()

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
        self.log("muted", f"Mode:  {describe_m25_version(self.get_selected_m25_version())}")
        self.status_message("info", "Connecting..." if not self.demo_mode else "Connecting (Demo Mode)...")

        def connect_thread():
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

                    selected_version = self.get_selected_m25_version()
                    loop = self._ensure_event_loop() if HAS_BLE else None

                    left_transport, left_reason = self._detect_transport_for_wheel(left_mac, selected_version, loop)
                    right_transport, right_reason = self._detect_transport_for_wheel(right_mac, selected_version, loop)

                    self.log("info", f"Left wheel transport: {left_transport} ({left_reason})")
                    self.log("info", f"Right wheel transport: {right_transport} ({right_reason})")

                    self.left_conn = self._make_connection(left_transport, left_mac, left_key_bytes, "left", loop)
                    self.right_conn = self._make_connection(right_transport, right_mac, right_key_bytes, "right", loop)
                    self.connected_transport_summary = f"L={left_transport}, R={right_transport}"
                    
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
                self.log("success", f"Connected successfully ({self.connected_transport_summary}).")
                self.status_message("success", "Connected")
            self.connect_btn.config(text="Disconnect")
            self.enable_controls(True)
            
            # Update system info to reflect connection state
            self.update_system_info()
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
        self.status_message("info", f"Setting hill hold {state}...")

        if self.demo_mode:
            self.log("warning", "Demo mode: Hill hold change simulated")
            self.status_message("success", f"Hill hold set to {state}")
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
                    left_ok = self.ecs_remote.write_auto_hold(self.left_conn, builder, enabled)
                    if left_ok:
                        ui_log("success", f"Left wheel: Hill hold {state}")
                    else:
                        ui_log("warning", f"Left wheel: Failed to set hill hold")
                    
                    # Write to right wheel
                    right_ok = self.ecs_remote.write_auto_hold(self.right_conn, builder, enabled)
                    if right_ok:
                        ui_log("success", f"Right wheel: Hill hold {state}")
                    else:
                        ui_log("warning", f"Right wheel: Failed to set hill hold")
                    
                    if left_ok and right_ok:
                        ui_status("success", f"Hill hold set to {state}")
                    else:
                        ui_status("warning", "Hill hold partially set")
                    # TODO: Verify if both wheels need to succeed for hill hold to be effective
                    
                except Exception as e:
                    ui_log("error", f"Hill hold change failed: {e}")
                    ui_status("error", "Hill hold change failed")
            
            threading.Thread(target=write_thread, daemon=True).start()

    def set_max_speed(self):
        """Set max speed for Level 1 and Level 2"""
        level1_speed = self.max_speed_level1.get()
        level2_speed = self.max_speed_level2.get()
        self.log("info", f"Setting max speeds: Level 1={level1_speed} km/h, Level 2={level2_speed} km/h")
        self.status_message("info", "Setting max speeds...")

        if self.demo_mode:
            self.log("warning", "Demo mode: Max speed change simulated")
            self.status_message("success", f"Max speeds set")
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
                    results = []
                    
                    # Write Level 1 max speed to left wheel
                    left1_ok = self.ecs_remote.write_max_speed(self.left_conn, builder, 1, level1_speed)
                    results.append(("Left wheel Level 1", left1_ok))
                    ui_log("success" if left1_ok else "warning", 
                           f"Left wheel Level 1: {level1_speed} km/h" if left1_ok else "Left wheel Level 1: Failed")
                    
                    # Write Level 2 max speed to left wheel
                    left2_ok = self.ecs_remote.write_max_speed(self.left_conn, builder, 2, level2_speed)
                    results.append(("Left wheel Level 2", left2_ok))
                    ui_log("success" if left2_ok else "warning",
                           f"Left wheel Level 2: {level2_speed} km/h" if left2_ok else "Left wheel Level 2: Failed")
                    
                    # Write Level 1 max speed to right wheel
                    right1_ok = self.ecs_remote.write_max_speed(self.right_conn, builder, 1, level1_speed)
                    results.append(("Right wheel Level 1", right1_ok))
                    ui_log("success" if right1_ok else "warning",
                           f"Right wheel Level 1: {level1_speed} km/h" if right1_ok else "Right wheel Level 1: Failed")
                    
                    # Write Level 2 max speed to right wheel
                    right2_ok = self.ecs_remote.write_max_speed(self.right_conn, builder, 2, level2_speed)
                    results.append(("Right wheel Level 2", right2_ok))
                    ui_log("success" if right2_ok else "warning",
                           f"Right wheel Level 2: {level2_speed} km/h" if right2_ok else "Right wheel Level 2: Failed")
                    
                    all_ok = all(ok for _, ok in results)
                    if all_ok:
                        ui_status("success", "Max speeds set successfully")
                    else:
                        failed = [name for name, ok in results if not ok]
                        ui_status("warning", f"Max speed partially set (failed: {', '.join(failed)})")
                    
                except Exception as e:
                    ui_log("error", f"Max speed change failed: {e}")
                    ui_status("error", "Max speed change failed")
            
            threading.Thread(target=write_thread, daemon=True).start()

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

        selected_version = self.get_selected_m25_version()

        # Show Windows limitation warning
        if IS_WINDOWS and selected_version != M25_VERSION_V1:
            result = messagebox.showinfo(
                "Windows BLE Scanning",
                "IMPORTANT: On Windows, only devices that are already paired in \n"
                "Windows Bluetooth settings will appear in the scan.\n\n"
                "To see your M25 wheels:\n"
                "1. Open Settings > Bluetooth & devices\n"
                "2. Make sure wheels are powered on\n"
                "3. Click 'Add device' and pair them first\n"
                "4. Then scan again in this app\n\n"
                "This is a Windows BLE API limitation, not a bug.",
                icon="info"
            )

        filter_enabled = self.filter_m25.get()
        scan_type = "M25 wheels" if filter_enabled else "all Bluetooth devices"
        self.log("info", f"Scanning for {scan_type} using {describe_m25_version(selected_version)}...")
        if IS_WINDOWS and selected_version != M25_VERSION_V1:
            self.log("muted", "Note: Only paired devices will appear (Windows limitation)")
        self.scan_status_lbl.config(text="Scanning...")
        self.scan_btn.config(state="disabled")
        self.status_message("info", "Scanning for devices...")

        def scan_thread():
            try:
                devices = []
                seen = set()

                def add_devices(found):
                    for addr, name in found:
                        if addr not in seen:
                            seen.add(addr)
                            devices.append((addr, name))

                if selected_version == M25_VERSION_V2:
                    if not HAS_BLE or not ble_scan_devices:
                        raise RuntimeError("BLE scanning not available")
                    add_devices(ble_scan_devices(duration=10, filter_m25=filter_enabled))
                elif selected_version == M25_VERSION_V1:
                    if IS_WINDOWS:
                        raise RuntimeError("M25V1 RFCOMM scanning is not supported on Windows in this app")
                    from m25_bluetooth import scan_devices as rfcomm_scan_devices
                    add_devices(rfcomm_scan_devices(duration=10, filter_m25=filter_enabled))
                else:
                    if HAS_BLE and ble_scan_devices:
                        add_devices(ble_scan_devices(duration=5, filter_m25=filter_enabled))
                    if not IS_WINDOWS and HAS_RFCOMM:
                        from m25_bluetooth import scan_devices as rfcomm_scan_devices
                        add_devices(rfcomm_scan_devices(duration=5, filter_m25=filter_enabled))
                
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
            if IS_WINDOWS:
                self.log("muted", "Remember: Only paired devices appear on Windows.")
                self.log("muted", "Pair devices in Windows Settings > Bluetooth & devices first.")
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


def main(default_m25_version=M25_VERSION_AUTO):
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

    cli_parser = argparse.ArgumentParser(add_help=False)
    cli_parser.add_argument(
        "--m25-version",
        default=default_m25_version,
        choices=[M25_VERSION_AUTO, M25_VERSION_V1, M25_VERSION_V2],
        help="Select wheel generation: auto, v1 (RFCOMM), or v2 (BLE)",
    )
    args, _ = cli_parser.parse_known_args()

    root = tk.Tk()
    app = M25GUI(root, default_m25_version=args.m25_version)
    root.mainloop()


if __name__ == "__main__":
    main()
