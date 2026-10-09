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
fixture=[json.loads(s) for s in Path(__file__).with_name("fixtures").joinpath("ipc-v1.jsonl").read_text().splitlines()]
server=socket.socket(socket.AF_UNIX);server.bind(sock);server.listen(4)
first_state=True
state=fixture[1]["result"]
old_eye={"closed":.1,"relaxed":.6,"relaxed_max":.65,"wide":.9,"d":.15,"wide_start":.75,"wide_available":True,"wide_unavailable_reason":None}
if scenario.startswith("calibration"):
    state["active_profile"]={"left":dict(old_eye),"right":dict(old_eye)}
def cal_result(kind,stage,reason,cause):
    return {"kind":kind,"stage":stage,"reason":reason,"wide_available":None,"caused_by":cause}
if scenario in ("calibration-cancel","calibration-startfailed"):
    state["calibration"]={"session_id":"oldsession","state":"completed","stage":None,"remaining_ms":0,"caused_by":None,"result":cal_result("completed",None,None,None)}
def cal_event(conn):
    cal=state["calibration"]
    conn.sendall(json.dumps({"v":1,"type":"event","event":"calibration.changed","data":{"instance_id":state["instance_id"],"calibration":cal},"caused_by":cal["caused_by"]}).encode()+b"\n")
def state_event(conn,cause):
    conn.sendall(json.dumps({"v":1,"type":"event","event":"state.changed","data":state,"caused_by":cause}).encode()+b"\n")
while True:
    conn,_=server.accept()
    try:
        with conn.makefile("rb") as stream:
            for raw in stream:
                request=json.loads(raw);log(request["method"])
                if request["method"]=="state.get":
                    response=dict(fixture[1]);response["id"]=request["id"]
                    response["extra"]=True
                    if scenario=="stateerror" and first_state:
                        response["ok"]=False;response["result"]={};response["error"]={"code":"temporarily_unavailable","message":"try state again"}
                    first_state=False
                    if scenario=="longsnapshot":
                        response["result"]["settings"]["current"]["prefix"]="/"+"x"*40000
                        response["result"]["settings"]["saved"]["prefix"]="/"+"x"*40000
                    if scenario=="oversized":conn.sendall(b"x"*(1024*1024+1)+b"\n");continue
                    if scenario=="malformed":conn.sendall(b"{not-json}\n");continue
                    if scenario=="utf8":conn.sendall(b'{"v":1,"type":"event","event":"future","data":{"bad":"\xff"}}\n');continue
                    if scenario=="depth":conn.sendall(b'{"v":1,"type":"event","event":"future","data":{"deep":'+b'['*65+b'0'+b']'*65+b'}}\n');continue
                    if scenario=="nonfinite":conn.sendall(b'{"v":1,"type":"event","event":"future","data":{"bad":1e999}}\n');continue
                    if scenario=="duplicate":conn.sendall(json.dumps(response).replace('"v": 1','"v": 1, "v": 1').encode()+b"\n");continue
                    # Multiple lines and arbitrary fragmentation; unknown event must be ignored.
                    payload=json.dumps(response,sort_keys=True,indent=None).encode()+b"\n"+b'{"v":1,"type":"event","event":"future","data":{}}\n'
                    conn.sendall(payload[:17]);time.sleep(.01);conn.sendall(payload[17:])
                elif request["method"].startswith("calibration."):
                    log(json.dumps({"method":request["method"],"params":request["params"]},sort_keys=True))
                    cause={"connection_id":state["connection_id"],"request_id":request["id"]}
                    method=request["method"];params=request["params"]
                    cal=state["calibration"]
                    save="not_requested";error=None
                    if method=="calibration.start":
                        if params!={}:raise ValueError("start params")
                        if scenario=="calibration-startfailed":
                            conn.sendall(json.dumps({"v":1,"type":"response","id":request["id"],"ok":False,"result":{"calibration":cal,"save":"not_requested"},"error":{"code":"busy","message":"cannot start"}}).encode()+b"\n")
                            continue
                        cal={"session_id":"newsession","state":"waiting","stage":"closed","remaining_ms":0,"caused_by":cause,"result":None}
                        state["calibration"]=cal
                    else:
                        if params.get("session_id")!="newsession":raise ValueError("wrong session")
                        cal["caused_by"]=cause
                        if method=="calibration.cancel":
                            if params!={"session_id":"newsession"}:raise ValueError("cancel params")
                            cal.update(state="cancelled",stage=None,remaining_ms=0,result=cal_result("cancelled",None,None,cause))
                        elif method=="calibration.skip_wide":
                            if params!={"session_id":"newsession"} or cal["stage"]!="wide":raise ValueError("skip params/stage")
                            if scenario=="calibration-failed":
                                save="failed";error={"code":"save_failed","message":"profile save failed"}
                                cal.update(state="failed",stage=None,remaining_ms=0,result=cal_result("failed",None,"save_failed",cause))
                            else:
                                save="saved";cal.update(state="completed",stage=None,remaining_ms=0,result=cal_result("completed",None,None,cause))
                                new_eye=dict(old_eye);new_eye.update(closed=.2,wide=None,wide_available=False,wide_unavailable_reason="not_measured")
                                state["active_profile"]={"left":dict(new_eye),"right":dict(new_eye)}
                        elif method=="calibration.begin_stage":
                            if params!={"session_id":"newsession","stage":cal["stage"]}:raise ValueError("begin params/stage")
                            cal.update(state="preparing",remaining_ms=2000)
                            if scenario=="calibration-disconnect":
                                cal.update(state="cancelled",stage=None,remaining_ms=0,result=cal_result("cancelled",None,"connection_lost",cause))
                                break
                        else:raise ValueError("unexpected method")
                    conn.sendall(json.dumps({"v":1,"type":"response","id":request["id"],"ok":error is None,"result":{"calibration":cal,"save":save},"error":error}).encode()+b"\n")
                    if method=="calibration.begin_stage":
                        cal.update(state="measuring",remaining_ms=5000);cal_event(conn)
                        stage=cal["stage"]
                        if stage=="wide":
                            cal.update(state="completed",stage=None,remaining_ms=0,result=cal_result("completed",None,None,cause))
                            new_eye=dict(old_eye);new_eye["closed"]=.2
                            state["active_profile"]={"left":dict(new_eye),"right":dict(new_eye)}
                        else:
                            cal.update(state="waiting",stage="relaxed" if stage=="closed" else "wide",remaining_ms=0,result=cal_result("stage_completed",stage,None,cause))
                        cal_event(conn)
                    state_event(conn,cause)
                elif request["method"]=="settings.patch":
                    response=dict(fixture[9]);response["id"]=request["id"]
                    response["result"]=dict(response["result"]);response["result"]["current"]=fixture[1]["result"]["settings"]["current"]
                    if scenario=="saveui":
                        patch=request["params"]["patch"];state=fixture[1]["result"]
                        state["settings"]["current"].update(patch)
                        state["settings"]["sources"].update({key:"panel" for key in patch})
                        if request["params"]["save"]:state["settings"]["saved"].update(patch)
                        response["ok"]=True;response["error"]=None
                        response["result"]={"current":state["settings"]["current"],"sources":state["settings"]["sources"],"osc_target":state["osc_target"],"output":state["output"],"save":"saved" if request["params"]["save"] else "not_requested"}
                        log(json.dumps(request["params"],sort_keys=True))
                    if scenario=="requestlimit":
                        response["result"]={};response["error"]={"code":"request_limit","message":"reconnect"}
                    conn.sendall(json.dumps(response).encode()+b"\n")
                    if scenario=="foreignreconnect":
                        conn.shutdown(socket.SHUT_RDWR);conn.close();server.close();os.unlink(sock);log("DROP");time.sleep(5);sys.exit(0)
                    if scenario=="reconnect":break
    except (BrokenPipeError,ConnectionResetError):pass
    finally:conn.close()
