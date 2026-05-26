"""Theme data for the M25 GUI (colour palettes and log levels)."""

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
