"""Optional compiled-backend IPC integration; no Cargo or Rust source dependency."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

backend = os.environ.get("FRAMEEYEOSC_BACKEND")
if not backend:
    print("SKIP: set FRAMEEYEOSC_BACKEND to an existing backend executable")
    sys.exit(77)
backend = Path(backend).resolve()
if not backend.is_file() or not os.access(backend, os.X_OK):
    sys.exit(f"FRAMEEYEOSC_BACKEND is not an executable file: {backend}")
with tempfile.TemporaryDirectory(prefix="panel-real-") as temporary:
    root = Path(temporary)
    control = root / "control"
    control.mkdir(mode=0o700)
    config = root / "settings.json"
    config.write_text(json.dumps({"target_mode": "manual", "target_host": "127.0.0.1", "target_port": 19000}))
    completed = subprocess.run([sys.argv[1], str(backend), str(control / "control.sock"), str(config)], capture_output=True, text=True, timeout=30)
    print(completed.stdout, end="")
    print(completed.stderr, end="", file=sys.stderr)
    if completed.returncode:
        sys.exit(completed.returncode)
    saved = json.loads(config.read_text())
    assert saved["eye_widening"] is False
    assert saved["prefix"] == "/Panel"
    assert saved["eyelid_output_cap"] == 0.6
