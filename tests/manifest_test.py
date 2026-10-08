import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

GENERATOR = Path(__file__).parent.parent / "assets/generate_manifest.py"

class Manifest(unittest.TestCase):
    def test_installed_paths_panel_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/"manifest.json"
            paths=["/opt/Panel/bin/frameeyeosc-panel","/opt/Backend/bin/frameeyeosc","/run/user/123/panel/control.sock","/home/user/Config/settings.json"]
            result=subprocess.run([sys.executable,str(GENERATOR),str(output),*paths],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            app=json.loads(output.read_text())["applications"][0]
            self.assertEqual(app["app_key"],"frameeyeosc.panel")
            self.assertTrue(app["is_dashboard_overlay"])
            self.assertEqual(app["binary_path_linux"],paths[0]);self.assertEqual(app["binary_path_linux_arm"],paths[0])
            self.assertEqual(shlex.split(app["arguments"]),["--backend",paths[1],"--control-socket",paths[2],"--config",paths[3]])
    def test_invalid_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            for bad in ("relative", "/bad\npath", "/bad\tpath", "/bad path", "/bad\"path", "/bad\\path", "/bad$path", "/bad`path", "/bad;path", "/bad&path", "/bad日本語"):
                for index in range(4):
                    paths=["/panel", "/backend", "/run/private/control.sock", "/config"]
                    paths[index]=bad
                    result=subprocess.run([sys.executable,str(GENERATOR),str(Path(tmp)/"manifest.json"),*paths],capture_output=True,text=True)
                    self.assertNotEqual(result.returncode,0,(index,bad))

if __name__=="__main__":unittest.main()
