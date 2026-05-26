#!/usr/bin/env python3
# pyright: reportArgumentType=false
"""M25 GUI - compatibility entrypoint; implementation lives in gui/."""

from gui.app import M25GUI, main  # noqa: F401

if __name__ == "__main__":
    main()
