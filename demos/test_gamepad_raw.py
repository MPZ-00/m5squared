"""
Raw Gamepad Test - Show all controller inputs

This script directly reads from the gamepad and displays all inputs
without going through the supervisor or mapper.
"""

import asyncio
import sys
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

try:
    import pygame
except ImportError:
    print("ERROR: pygame not installed")
    print("Install with: pip install pygame")
    sys.exit(1)


def main():
    """Test raw gamepad input"""
    print("=" * 70)
    print("RAW GAMEPAD TEST")
    print("=" * 70)
    print()
    print("This will show all controller inputs in real-time.")
    print("Press Ctrl+C to exit")
    print()
    
    # Initialize pygame
    pygame.init()
    pygame.joystick.init()
    
    # Find controller
    joystick_count = pygame.joystick.get_count()
    print(f"Found {joystick_count} controller(s)")
    
    if joystick_count == 0:
        print("ERROR: No controllers found!")
        return 1
    
    # Use first controller
    joystick = pygame.joystick.Joystick(0)
    joystick.init()
    
    print(f"Using: {joystick.get_name()}")
    print(f"  Axes: {joystick.get_numaxes()}")
    print(f"  Buttons: {joystick.get_numbuttons()}")
    print(f"  Hats: {joystick.get_numhats()}")
    print()
    print("Move sticks and press buttons...")
    print("=" * 70)
    print()
    
    try:
        clock = pygame.time.Clock()
        last_output = ""
        
        while True:
            # Process events to update joystick state
            pygame.event.pump()
            
            # Build output lines
            lines = []
            
            # Show all axes
            axes_lines = []
            for i in range(joystick.get_numaxes()):
                value = joystick.get_axis(i)
                # Only show non-zero axes or first 4 axes always
                if i < 4 or abs(value) > 0.01:
                    axes_lines.append(f"  Axis {i}: {value:+.3f}")
            
            if axes_lines:
                lines.append("Axes:")
                lines.extend(axes_lines)
            
            # Show pressed buttons
            pressed = []
            for i in range(joystick.get_numbuttons()):
                if joystick.get_button(i):
                    pressed.append(str(i))
            
            lines.append(f"Buttons Pressed: [{', '.join(pressed)}]")
            
            # Show hat (D-pad)
            if joystick.get_numhats() > 0:
                hat = joystick.get_hat(0)
                lines.append(f"D-Pad (Hat): {hat}")
            
            # Build complete output
            output = "\n".join(lines)
            
            # Only print if changed
            if output != last_output:
                print("\033[2J\033[H", end="")  # Clear screen and move to top
                print("=" * 70)
                print("RAW GAMEPAD TEST - Press Ctrl+C to exit")
                print("=" * 70)
                print()
                print(output)
                last_output = output
            
            # Run at 20 Hz
            clock.tick(20)
            
    except KeyboardInterrupt:
        print()
        print()
        print("Interrupted by user")
    
    finally:
        joystick.quit()
        pygame.quit()
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
