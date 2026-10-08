import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

GENERATOR = Path(__file__).parent.parent / "assets/generate_manifest.py"

class Manifest(unittest.TestCase):
    def test_installed_paths_panel_only_and_spaces(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/"manifest.json"
            paths=["/opt/Panel Site/bin/frameeyeosc-panel","/opt/Backend Site/bin/frameeyeosc","/run/user/123/panel/control.sock","/home/user/Config Site/settings.json"]
            result=subprocess.run([sys.executable,str(GENERATOR),str(output),*paths],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            app=json.loads(output.read_text())["applications"][0]
            self.assertEqual(app["app_key"],"frameeyeosc.panel")
            self.assertTrue(app["is_dashboard_overlay"])
            self.assertEqual(app["binary_path_linux"],paths[0]);self.assertEqual(app["binary_path_linux_arm"],paths[0])
            self.assertEqual(shlex.split(app["arguments"]),["--backend",paths[1],"--control-socket",paths[2],"--config",paths[3]])
    def test_invalid_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            for bad in ("relative", "/bad\npath", "/bad\tpath"):
                result=subprocess.run([sys.executable,str(GENERATOR),str(Path(tmp)/"manifest.json"),bad,"/backend","/run/private/control.sock",""],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)

if __name__=="__main__":unittest.main()
