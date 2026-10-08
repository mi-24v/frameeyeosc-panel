#!/usr/bin/env python3
import json,os,signal,socket,sys,time
from pathlib import Path
args=sys.argv[1:]
sock=args[args.index("--control-socket")+1]
config=json.loads(Path(args[args.index("--config")+1]).read_text())
scenario=config["scenario"]
def log(s):
    with open(config["log"],"a") as f:f.write(s+"\n")
log("START "+" ".join(args))
if scenario=="crash":sys.exit(23)
if scenario=="ignore":signal.signal(signal.SIGTERM,signal.SIG_IGN)
if scenario=="delay":time.sleep(5)
fixture=[json.loads(s) for s in Path(__file__).with_name("fixtures").joinpath("ipc-v1-subset.jsonl").read_text().splitlines()]
server=socket.socket(socket.AF_UNIX);server.bind(sock);server.listen(4)
while True:
    conn,_=server.accept()
    try:
        with conn.makefile("rb") as stream:
            for raw in stream:
                request=json.loads(raw);log(request["method"])
                if request["method"]=="state.get":
                    response=dict(fixture[1]);response["id"]=request["id"]
                    response["extra"]=True
                    if scenario=="longsnapshot":
                        response["result"]["settings"]["current"]["prefix"]="/"+"x"*40000
                        response["result"]["settings"]["saved"]["prefix"]="/"+"x"*40000
                    if scenario=="oversized":conn.sendall(b"x"*(1024*1024+1)+b"\n");continue
                    if scenario=="malformed":conn.sendall(b"{not-json}\n");continue
                    if scenario=="duplicate":conn.sendall(json.dumps(response).replace('"v": 1','"v": 1, "v": 1').encode()+b"\n");continue
                    # Multiple lines and arbitrary fragmentation; unknown event must be ignored.
                    payload=json.dumps(response,sort_keys=True,indent=None).encode()+b"\n"+b'{"v":1,"type":"event","event":"future","data":{}}\n'
                    conn.sendall(payload[:17]);time.sleep(.01);conn.sendall(payload[17:])
                elif request["method"]=="settings.patch":
                    response=dict(fixture[8]);response["id"]=request["id"]
                    response["result"]=dict(response["result"]);response["result"]["current"]=fixture[1]["result"]["settings"]["current"]
                    if scenario=="requestlimit":
                        response["result"]={};response["error"]={"code":"request_limit","message":"reconnect"}
                    conn.sendall(json.dumps(response).encode()+b"\n")
                    if scenario=="foreignreconnect":
                        conn.shutdown(socket.SHUT_RDWR);conn.close();server.close();os.unlink(sock);log("DROP");time.sleep(5);sys.exit(0)
                    if scenario=="reconnect":break
    except (BrokenPipeError,ConnectionResetError):pass
    finally:conn.close()
