"""Custom Tkinter widgets for the M25 GUI."""

import tkinter as tk


class PlaceholderEntry(tk.Entry):
    """Entry widget that shows placeholder text when empty."""

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
        if not super().get():
            self.put_placeholder()

    def get(self):
        content = super().get()
        if self.has_placeholder:
            return ""
        return content

    def set_theme_colors(self, fg_color, placeholder_color):
        """Update fg and placeholder colors for the active theme."""
        self.default_fg_color = fg_color
        self.placeholder_color = placeholder_color
        if self.has_placeholder:
            self.config(fg=placeholder_color)
        else:
            self.config(fg=fg_color)
