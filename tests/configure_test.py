"""Configure-only checks; the ARM case does not compile or run ARM code."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CMAKE,SOURCE,JSON,OPENVR=sys.argv[1:]
sys.argv=sys.argv[:1]

class Configure(unittest.TestCase):
    def configure(self,root,name,*options):
        return subprocess.run([CMAKE,"-S",SOURCE,"-B",str(root/name),"-DBUILD_TESTING=OFF",f"-DFETCHCONTENT_SOURCE_DIR_JSON={JSON}",f"-DFETCHCONTENT_SOURCE_DIR_OPENVR={OPENVR}",*options],capture_output=True,text=True,timeout=30)
    def test_manifest_options_and_architecture(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for i,options in enumerate((["-DPANEL_BACKEND=/backend"],["-DPANEL_CONTROL_SOCKET=/control.sock"],["-DPANEL_CONFIG=/config"],["-DPANEL_BACKEND=/backend","-DPANEL_CONFIG=/config"])):
                result=self.configure(root,f"partial{i}",*options)
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("requires both PANEL_BACKEND and PANEL_CONTROL_SOCKET",result.stderr)
            result=self.configure(root,"valid","-DPANEL_BACKEND=/backend","-DPANEL_CONTROL_SOCKET=/control.sock",f"-DCMAKE_INSTALL_PREFIX={root}/install")
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertTrue((root/"valid/frameeyeosc-panel.vrmanifest").exists())
            result=self.configure(root,"space","-DPANEL_BACKEND=/backend","-DPANEL_CONTROL_SOCKET=/control.sock",f"-DCMAKE_INSTALL_PREFIX={root}/space install")
            self.assertNotEqual(result.returncode,0)
            self.assertIn("requiring no shell quotation",result.stderr)
            toolchain=root/"arm.cmake"
            toolchain.write_text('set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\nset(CMAKE_CXX_COMPILER /usr/bin/g++)\n')
            result=self.configure(root,"arm",f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn("/lib/linuxarm64/libopenvr_api.so",(root/"arm/CMakeCache.txt").read_text())

if __name__=="__main__":unittest.main()
