#!/usr/bin/env python3
"""Generate an installed absolute-path manifest without registering it."""
import json
from pathlib import Path
import shlex
import sys
output, binary, backend, socket, config = sys.argv[1:]
for value in (binary, backend, socket, config):
    if value and (not Path(value).is_absolute() or not value.isascii() or shlex.quote(value)!=value or any(ord(c)<32 or ord(c)==127 for c in value)):
        raise SystemExit("Manifest paths must be absolute portable ASCII paths requiring no shell quotation")
if not binary or not backend or not socket:
    raise SystemExit("Manifest binary, backend and control socket paths are required")
manifest=json.loads(Path(__file__).with_name("frameeyeosc-panel.vrmanifest.in").read_text())
app=manifest["applications"][0]
app["binary_path_linux"]=binary
app["binary_path_linux_arm"]=binary
arguments=["--backend",backend,"--control-socket",socket]
if config: arguments.extend(["--config",config])
app["arguments"]=shlex.join(arguments)
Path(output).write_text(json.dumps(manifest,indent=2)+"\n")
