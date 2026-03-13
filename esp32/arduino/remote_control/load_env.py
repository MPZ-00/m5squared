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
  M25_LEFT_KEY      -> ENCRYPTION_KEY_LEFT   (byte-array initializer)
  M25_RIGHT_KEY     -> ENCRYPTION_KEY_RIGHT  (byte-array initializer)
  M25_LEFT_MAC      -> LEFT_WHEEL_MAC        (string literal)
  M25_RIGHT_MAC     -> RIGHT_WHEEL_MAC       (string literal)
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

# Keys (byte-array initializers wrapped in braces)
for env_key, define in (("M25_LEFT_KEY",  "ENCRYPTION_KEY_LEFT"),
                         ("M25_RIGHT_KEY", "ENCRYPTION_KEY_RIGHT")):
    if env_key in cfg and cfg[env_key]:
        try:
            byte_str = _hex_to_c_bytes(cfg[env_key])
            flags.append(f"-D{define}={{{byte_str}}}")
            loaded.append(define)
        except ValueError as e:
            print(f"[load_env] WARNING: {env_key} invalid -- {e}")

# MACs (string literals)
for env_key, define in (("M25_LEFT_MAC",  "LEFT_WHEEL_MAC"),
                         ("M25_RIGHT_MAC", "RIGHT_WHEEL_MAC")):
    if env_key in cfg and cfg[env_key]:
        mac = cfg[env_key]
        flags.append(f'-D{define}=\\"{mac}\\"')
        loaded.append(define)

if flags:
    env.Append(BUILD_FLAGS=flags)  # type: ignore # noqa: F821
    print(f"[load_env] Injected from .env: {', '.join(loaded)}")
else:
    print("[load_env] No M25 keys/MACs found in .env -- using compiled defaults")
