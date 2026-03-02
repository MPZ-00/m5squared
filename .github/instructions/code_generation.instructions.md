---
applyTo: 'esp32/**'
---

## Code Generation Rules

### No Demo Files
- Do not create demo sketches or example files
- Implementation only

### No Unnecessary Documentation
- Do not create documentation that can be explained in 1-2 sentences in chat
- Keep documentation minimal and essential only
- Integration instructions should be brief or conversational

### Arduino Sketch Structure
- One .ino file per folder (Arduino compiler requirement)
- Tests go in dedicated `esp32/arduino/tests/test_*/` folders
- Each test folder is self-contained with local copies of dependencies

### Testing
- Automate via `run_tests.ps1` or GitHub Actions
- No manual testing instructions in docs
