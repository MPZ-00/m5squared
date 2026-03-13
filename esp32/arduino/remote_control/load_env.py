"""
PlatformIO pre-build script: load_env.py

Reads .env and injects M25 wheel keys and MAC addresses
as compiler build flags.  This means you never have to edit device_config.h
-- just keep .env up to date and rebuild.

Priority order:
    1) Local  : .env
    2) Fallback: repo-root/.env

Mapping:
  .env key          -> C preprocessor define
  M25_LEFT_KEY      -> ENV_ENCRYPTION_KEY_LEFT   (byte-array initializer)
  M25_RIGHT_KEY     -> ENV_ENCRYPTION_KEY_RIGHT  (byte-array initializer)
  M25_LEFT_MAC      -> ENV_LEFT_WHEEL_MAC        (string literal)
  M25_RIGHT_MAC     -> ENV_RIGHT_WHEEL_MAC       (string literal)
  (all four present)-> ENV_PROFILE_AVAILABLE=1
"""

import pathlib

Import("env")  # type: ignore # noqa: F821  (PlatformIO injects this)

# -----------------------------------------------------------------------
# Locate .env (prefer local project config over repo-wide fallback)
# -----------------------------------------------------------------------
try:
    _script_dir = pathlib.Path(__file__).resolve().parent
except NameError:
    # Some script runners do not define __file__. Fall back to CWD.
    _script_dir = pathlib.Path.cwd()
_local_env_path    = _script_dir / ".env"
_repo_root         = _script_dir
if len(_script_dir.parents) >= 3:
    _repo_root = _script_dir.parents[2]
_repo_env_path     = _repo_root / ".env"

if _local_env_path.exists():
    _env_path = _local_env_path
elif _repo_env_path.exists():
    _env_path = _repo_env_path
else:
    print(
        "[load_env] No .env found (checked local and repo-root) -- using compiled defaults"
    )
    Return()  # type: ignore # noqa: F821
    quit()

print(f"[load_env] Using .env: {_env_path}")

# -----------------------------------------------------------------------
# Parse .env (KEY=VALUE, strip comments and blanklines)
# -----------------------------------------------------------------------
cfg = {}
for raw in _env_path.read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    k, _, v = line.partition("=")
    cfg[k.strip()] = v.strip()

# -----------------------------------------------------------------------
# Helper: "e78bad67..." -> "0xE7,0x8B,0xAD,0x67,..."
# -----------------------------------------------------------------------
def _hex_to_c_bytes(hex_str):
    b = bytes.fromhex(hex_str)
    if len(b) != 16:
        raise ValueError(f"AES key must be 32 hex chars (got {len(hex_str)})")
    return ",".join(f"0x{x:02X}" for x in b)

flags = []
loaded = []

# Build optional env profile only when all required values are present.
required = ("M25_LEFT_KEY", "M25_RIGHT_KEY", "M25_LEFT_MAC", "M25_RIGHT_MAC")
missing = [k for k in required if not cfg.get(k)]

if missing:
    print("[load_env] env profile disabled: incomplete M25_* values in .env")
    print("[load_env] Using build defaults (default profile) unless overridden by NVS")
else:
    try:
        left_key = _hex_to_c_bytes(cfg["M25_LEFT_KEY"])
        right_key = _hex_to_c_bytes(cfg["M25_RIGHT_KEY"])
    except ValueError as e:
        print(f"[load_env] env profile disabled: invalid key format -- {e}")
        print("[load_env] Using build defaults (default profile) unless overridden by NVS")
    else:
        flags.extend([
            f"-DENV_ENCRYPTION_KEY_LEFT={{{left_key}}}",
            f"-DENV_ENCRYPTION_KEY_RIGHT={{{right_key}}}",
            f'-DENV_LEFT_WHEEL_MAC=\\"{cfg["M25_LEFT_MAC"]}\\"',
            f'-DENV_RIGHT_WHEEL_MAC=\\"{cfg["M25_RIGHT_MAC"]}\\"',
            "-DENV_PROFILE_AVAILABLE=1",
        ])
        loaded.extend([
            "ENV_ENCRYPTION_KEY_LEFT",
            "ENV_ENCRYPTION_KEY_RIGHT",
            "ENV_LEFT_WHEEL_MAC",
            "ENV_RIGHT_WHEEL_MAC",
            "ENV_PROFILE_AVAILABLE",
        ])

if flags:
    env.Append(BUILD_FLAGS=flags)  # type: ignore # noqa: F821
    print(f"[load_env] Injected from .env: {', '.join(loaded)}")
