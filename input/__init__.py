"""Input provider base module"""

from input.mock_input import MockInput, TestScripts

try:
    from input.gamepad_input import GamepadInput
    __all__ = ["MockInput", "TestScripts", "GamepadInput"]
except ImportError:
    # pygame not available
    __all__ = ["MockInput", "TestScripts"]

