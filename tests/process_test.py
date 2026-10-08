import ctypes
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

DRIVER = sys.argv.pop(1)
FAKE = Path(__file__).with_name("fake_backend.py")

class Processes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sock = str(self.root / "control.sock")
        self.config = self.root / "config.json"
        self.children = []
        self.buffers = {}
        self.owned = set()
        self.assertEqual(ctypes.CDLL(None).prctl(36,1,0,0,0),0)
    def tearDown(self):
        for p in self.children:
            if p.poll() is None:
                path=Path(f"/proc/{p.pid}/task/{p.pid}/children")
                try:self.owned.update(map(int,path.read_text().split()))
                except FileNotFoundError:pass
                p.kill()
            p.wait(timeout=5)
        for pid in self.owned:
            try:
                result,_=os.waitpid(pid,os.WNOHANG)
                if not result:
                    os.kill(pid,signal.SIGKILL)
                    os.waitpid(pid,0)
            except ChildProcessError:pass
        for p in self.children:p.communicate(timeout=5)
        self.tmp.cleanup()
    def launch(self, mode="lifecycle", scenario="normal"):
        self.config.write_text(json.dumps({"scenario": scenario, "log":str(self.root/"log")}))
        p=subprocess.Popen([DRIVER, mode, str(FAKE), self.sock, str(self.config)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
        self.children.append(p)
        return p
    def read(self,p):
        # Read bytes directly so multiple output lines never hide in TextIO buffering.
        pending=self.buffers.get(p.pid,b"")
        deadline=time.monotonic()+7
        while b"\n" not in pending:
            r,_,_=select.select([p.stdout],[],[],max(0,deadline-time.monotonic()))
            self.assertTrue(r,"driver output timed out")
            chunk=os.read(p.stdout.fileno(),4096)
            self.assertTrue(chunk,"driver closed output before expected line")
            pending+=chunk
        line,pending=pending.split(b"\n",1)
        self.buffers[p.pid]=pending
        if line.startswith(b"PID "):self.owned.add(int(line[4:]))
        return line.decode()
    def ready(self,p):
        pidline=self.read(p); self.assertTrue(pidline.startswith("PID "),pidline)
        pid=int(pidline[4:]); self.assertEqual(self.read(p),"READY")
        return pid
    def test_owned_close_and_config(self):
        p=self.launch(); pid=self.ready(p)
        self.assertTrue(Path(f"/proc/{pid}").exists())
        p.stdin.write("close\n");p.stdin.flush()
        out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out)
        self.assertFalse(Path(f"/proc/{pid}").exists())
        self.assertIn("--panel-mode",(self.root/"log").read_text())
    def test_hide_and_remote_quit_keep_child_local_quit_stops(self):
        p=self.launch();pid=self.ready(p)
        for command in ("hide","remotequit"):
            p.stdin.write(command+"\n");p.stdin.flush()
            self.assertEqual(self.read(p),f"RESIDENT {pid}")
            self.assertTrue(Path(f"/proc/{pid}").exists())
        p.stdin.write("localquit\n");p.stdin.flush()
        out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out)
        self.assertFalse(Path(f"/proc/{pid}").exists())
    def test_ui_close_stops_owned_child(self):
        p=self.launch();pid=self.ready(p)
        p.stdin.write("uiclose\n");p.stdin.flush()
        out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out)
        self.assertFalse(Path(f"/proc/{pid}").exists())
    def test_exec_failure(self):
        p=subprocess.run([DRIVER,"lifecycle","/missing/backend",self.sock],capture_output=True,text=True,timeout=5)
        self.assertEqual(p.returncode,1);self.assertIn("exec",p.stdout);self.assertNotIn("PID ",p.stdout)
    def test_crash_reason_and_no_restart(self):
        p=self.launch("crash","crash");out,err=p.communicate(timeout=7)
        self.assertEqual(p.returncode,0,err+out);self.assertIn("exit 23",out)
        self.assertEqual((self.root/"log").read_text().count("START"),1)
    def test_sigkill_escalation(self):
        p=self.launch(scenario="ignore");pid=self.ready(p)
        start=time.monotonic();p.stdin.write("close\n");p.stdin.flush()
        out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out)
        self.assertGreaterEqual(time.monotonic()-start,1.9);self.assertLess(time.monotonic()-start,3.5)
        self.assertIn("signal 9",out);self.assertFalse(Path(f"/proc/{pid}").exists())
    def test_parent_crash_kills_child(self):
        # Become subreaper so this test also reaps the orphan rather than leaving a zombie.
        self.assertEqual(ctypes.CDLL(None).prctl(36,1,0,0,0),0)
        p=self.launch();pid=self.ready(p);p.kill();p.communicate(timeout=3)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            result,status=os.waitpid(pid,os.WNOHANG)
            if result:
                self.assertTrue(os.WIFSIGNALED(status));self.assertEqual(os.WTERMSIG(status),signal.SIGTERM);break
            time.sleep(.01)
        else:self.fail("owned child survived parent death")
    def test_parent_death_during_startup(self):
        self.assertEqual(ctypes.CDLL(None).prctl(36,1,0,0,0),0)
        for _ in range(12):
            p=self.launch()
            path=Path(f"/proc/{p.pid}/task/{p.pid}/children")
            deadline=time.monotonic()+3
            child=None
            while time.monotonic()<deadline:
                try:
                    ids=path.read_text().split()
                except FileNotFoundError:ids=[]
                if ids:child=int(ids[0]);break
                time.sleep(.0005)
            self.assertIsNotNone(child,"startup did not fork")
            p.kill();p.communicate(timeout=4)
            deadline=time.monotonic()+3
            while time.monotonic()<deadline:
                try:result,status=os.waitpid(child,os.WNOHANG)
                except ChildProcessError:result=child;status=0
                if result:break
                time.sleep(.005)
            else:self.fail("child survived parent death during startup")
            self.assertFalse(Path(f"/proc/{child}").exists())
    def test_preexisting_backend_untouched(self):
        listener=socket.socket(socket.AF_UNIX);listener.bind(self.sock);listener.listen(4);listener.settimeout(5)
        try:
            p=self.launch();out,err=p.communicate(timeout=5)
            self.assertEqual(p.returncode,1,err+out);self.assertIn("existing",out)
            self.assertFalse((self.root/"log").exists())
            conn,_=listener.accept();self.assertEqual(conn.recv(32),b"");conn.close()
        finally:listener.close()
    def test_racing_foreign_peer_rejected(self):
        p=self.launch("race","delay")
        pidline=self.read(p);self.assertTrue(pidline.startswith("PID "),pidline)
        listener=socket.socket(socket.AF_UNIX);listener.bind(self.sock);listener.listen(4);listener.settimeout(5)
        try:
            conn,_=listener.accept();conn.settimeout(3)
            self.assertEqual(conn.recv(1024),b"")
            out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out)
            self.assertIn("foreign peer rejected",out);conn.close()
        finally:listener.close()
    def test_valid_snapshot_larger_than_request_limit(self):
        p=self.launch("longsnapshot","longsnapshot");out,err=p.communicate(timeout=7)
        self.assertEqual(p.returncode,0,err+out);self.assertIn("long valid snapshot",out)
    def test_invalid_and_oversized_backend_lines(self):
        for scenario in ("malformed","oversized","duplicate"):
            with self.subTest(scenario=scenario):
                if Path(self.sock).exists():Path(self.sock).unlink()
                p=self.launch("protocol",scenario);out,err=p.communicate(timeout=7)
                self.assertEqual(p.returncode,0,err+out)
                self.assertTrue("Invalid backend JSON" in out or "1 MiB" in out or "buffer limit" in out)
    def test_foreign_peer_on_reconnect_is_not_attached(self):
        p=self.launch("reconnect-race","foreignreconnect")
        line=self.read(p);self.assertTrue(line.startswith("PID "),line)
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            if (self.root/"log").exists() and "DROP" in (self.root/"log").read_text():break
            time.sleep(.005)
        else:self.fail("owned backend did not drop socket")
        listener=socket.socket(socket.AF_UNIX);listener.bind(self.sock);listener.listen(4);listener.settimeout(5)
        try:
            conn,_=listener.accept();conn.settimeout(3);self.assertEqual(conn.recv(1024),b"")
            out,err=p.communicate(timeout=5);self.assertEqual(p.returncode,0,err+out);self.assertIn("foreign peer rejected",out)
            conn.close()
        finally:listener.close()
    def test_request_limit_reconnects_without_patch_replay(self):
        p=self.launch("ipc","requestlimit");out,err=p.communicate(timeout=7)
        self.assertEqual(p.returncode,0,err+out);self.assertIn("reconciled",out)
        log=(self.root/"log").read_text();self.assertEqual(log.count("settings.patch"),1);self.assertEqual(log.count("state.get"),2)
    def test_fragmented_fixture_failure_reconnect_no_replay(self):
        p=self.launch("ipc","reconnect");out,err=p.communicate(timeout=7)
        self.assertEqual(p.returncode,0,err+out);self.assertIn("reconciled",out)
        log=(self.root/"log").read_text();self.assertEqual(log.count("settings.patch"),1)
        self.assertEqual(log.count("state.get"),2)

if __name__=="__main__":unittest.main()
