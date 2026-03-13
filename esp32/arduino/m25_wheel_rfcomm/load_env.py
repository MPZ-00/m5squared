"""
PlatformIO pre-build script: load_env.py

Reads only the local sketch .env and injects left/right wheel AES keys as
compiler build flags. If no local .env exists, compiled defaults from config.h
are used unchanged.

Mapping:
  .env key       -> C preprocessor define
  M25_LEFT_KEY   -> ENCRYPTION_KEY_LEFT
  M25_RIGHT_KEY  -> ENCRYPTION_KEY_RIGHT
"""

import pathlib
import subprocess

Import("env")  # type: ignore # noqa: F821

try:
    _script_dir = pathlib.Path(__file__).resolve().parent
except NameError:
    _script_dir = pathlib.Path.cwd()

_env_path = _script_dir / ".env"

if not _env_path.exists():
    print("[load_env] No local .env found -- using compiled defaults from config.h")
    Return()  # type: ignore # noqa: F821
    quit()

print(f"[load_env] Using local .env: {_env_path}")

cfg = {}
for raw in _env_path.read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    key, _, value = line.partition("=")
    cfg[key.strip()] = value.strip()


def _hex_to_c_bytes(hex_str):
    data = bytes.fromhex(hex_str)
    if len(data) != 16:
        raise ValueError(f"AES key must be 32 hex chars (got {len(hex_str)})")
    return ",".join(f"0x{item:02X}" for item in data)


flags = []
loaded = []
for env_key, define in (("M25_LEFT_KEY", "ENCRYPTION_KEY_LEFT"),
                        ("M25_RIGHT_KEY", "ENCRYPTION_KEY_RIGHT")):
    if env_key not in cfg or not cfg[env_key]:
        continue
    try:
        byte_str = _hex_to_c_bytes(cfg[env_key])
        flags.append(f"-D{define}={{{byte_str}}}")
        loaded.append(define)
    except ValueError as exc:
        print(f"[load_env] WARNING: {env_key} invalid -- {exc}")

if flags:
    env.Append(BUILD_FLAGS=flags)  # type: ignore # noqa: F821
    print(f"[load_env] Injected from .env: {', '.join(loaded)}")
else:
    print("[load_env] No M25 wheel keys found in local .env -- using compiled defaults")


def _find_git_root(start_dir):
    current = start_dir
    while True:
        if (current / ".git").exists():
            return current
        if current.parent == current:
            return None
        current = current.parent


def _git_cmd(repo_dir, *args):
    return subprocess.run(
        ["git", *args],
        cwd=repo_dir,
        check=False,
        capture_output=True,
        text=True,
    )


git_root = _find_git_root(_script_dir)
if not git_root:
    print("[load_env] No git repo found -- FW_GIT_HASH=nogit")
else:
    rev = _git_cmd(git_root, "rev-parse", "--short=10", "HEAD")
    if rev.returncode == 0 and rev.stdout.strip():
        git_hash = rev.stdout.strip()
        dirty = _git_cmd(git_root, "diff", "--quiet")
        git_flags = [
            f'-DFW_GIT_HASH=\\"{git_hash}\\"',
            f"-DFW_GIT_DIRTY={1 if dirty.returncode != 0 else 0}",
        ]
        env.Append(BUILD_FLAGS=git_flags)  # type: ignore # noqa: F821
        print(
            f"[load_env] Injected git metadata: hash={git_hash} dirty={1 if dirty.returncode != 0 else 0}"
        )
    else:
        print("[load_env] Git hash unavailable -- FW_GIT_HASH=nogit")