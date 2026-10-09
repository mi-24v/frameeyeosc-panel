#include "ipc_client.hpp"
#include <cerrno>
#include <cstring>
#include <cmath>
#include <unordered_set>
#include <vector>
#include <stdexcept>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace panel {
namespace {
constexpr size_t request_limit=64*1024;
constexpr size_t line_limit=1024*1024;
bool settings(const Json& value,bool full) {
  if(!value.is_object()) return false;
  for(const char* key:{"eye_widening","eye_widening_margin_left","eye_widening_margin_right","min_base_width","min_wide_width","eyelid_output_cap","prefix","one_euro","one_euro_min_cutoff","one_euro_beta","one_euro_d_cutoff","target_mode","target_host","target_port","shm_version"}) {
    if(!value.contains(key)) {if(full) return false; else continue;}
    const auto& item=value[key]; const std::string name=key;
    if(name=="one_euro" || name=="eye_widening") {if(!item.is_boolean()) return false;}
    else if(name=="target_port") {if(!item.is_number_integer() || item<1 || item>65535) return false;}
    else if(name=="eyelid_output_cap" || name.find("one_euro_")==0 || name.find("eye_widening_margin_")==0 || name=="min_base_width" || name=="min_wide_width") {
      if(!item.is_number()) return false;
      const double number=item.get<double>(); if(!std::isfinite(number)) return false;
      if(name=="eyelid_output_cap" && (number<0 || number>1)) return false;
      if((name=="one_euro_beta" || name.find("eye_widening_margin_")==0) && number<0) return false;
      if((name=="one_euro_min_cutoff" || name=="one_euro_d_cutoff" || name=="min_base_width" || name=="min_wide_width") && number<=0) return false;
    } else if(!item.is_string()) return false;
  }
  return true;
}
bool sources(const Json& j) {
  if(!j.is_object()) return false;
  for(const char* key:{"eye_widening","eye_widening_margin_left","eye_widening_margin_right","min_base_width","min_wide_width","eyelid_output_cap","prefix","one_euro","one_euro_min_cutoff","one_euro_beta","one_euro_d_cutoff","target_mode","target_host","target_port","shm_version"}) if(!j.contains(key)||!j[key].is_string() || (j[key]!="cli" && j[key]!="saved" && j[key]!="default" && j[key]!="panel")) return false;
  return true;
}
bool target_output(const Json& j) {
  return j.contains("osc_target") && (j["osc_target"].is_null()||j["osc_target"].is_string()) && j.contains("output") && j["output"].is_object() && j["output"].contains("state") && j["output"]["state"].is_string() && j["output"].contains("reason") && (j["output"]["reason"].is_null()||j["output"]["reason"].is_string());
}
bool nullable_string(const Json& j) {return j.is_null() || j.is_string();}
bool cause(const Json& j) {
  return j.is_null() || (j.is_object() && j.contains("connection_id") && j["connection_id"].is_string() && !j["connection_id"].get<std::string>().empty() && j.contains("request_id") && j["request_id"].is_string() && !j["request_id"].get<std::string>().empty());
}
bool stage(const Json& j) {return j=="closed" || j=="relaxed" || j=="wide";}
bool calibration(const Json& j) {
  if(j.is_null())return true;
  if(!j.is_object() || !j.contains("session_id") || !j["session_id"].is_string() || j["session_id"].get<std::string>().empty() || !j.contains("state") || !j.contains("stage") || !j.contains("remaining_ms") || !j["remaining_ms"].is_number_integer() || j["remaining_ms"]<0 || !j.contains("caused_by") || !cause(j["caused_by"]) || !j.contains("result"))return false;
  const bool active=j["state"]=="waiting" || j["state"]=="preparing" || j["state"]=="measuring";
  const bool terminal=j["state"]=="completed" || j["state"]=="cancelled" || j["state"]=="failed";
  if((!active && !terminal) || (active && !stage(j["stage"])) || (terminal && !j["stage"].is_null()) || ((terminal || j["state"]=="waiting") && j["remaining_ms"]!=0))return false;
  const auto& r=j["result"];
  if(r.is_null())return true;
  if(!r.is_object() || !r.contains("kind") || (r["kind"]!="stage_completed" && r["kind"]!="stage_failed" && r["kind"]!="completed" && r["kind"]!="cancelled" && r["kind"]!="failed") || !r.contains("stage") || (!r["stage"].is_null() && !stage(r["stage"])) || !r.contains("reason") || !nullable_string(r["reason"]) || !r.contains("caused_by") || !cause(r["caused_by"]) || !r.contains("wide_available"))return false;
  const auto& wide=r["wide_available"];
  return wide.is_null() || (wide.is_array() && wide.size()==2 && wide[0].is_boolean() && wide[1].is_boolean());
}
bool profile(const Json& j) {
  if(j.is_null())return true;
  if(!j.is_object())return false;
  for(const char* side:{"left","right"}) {
    if(!j.contains(side) || !j[side].is_object())return false;
    const auto& eye=j[side];
    for(const char* key:{"closed","relaxed","relaxed_max","wide","d","wide_start"}) {
      if(!eye.contains(key))return false;
      if(std::string(key)=="wide" && eye[key].is_null())continue;
      if(!eye[key].is_number() || !std::isfinite(eye[key].get<double>()))return false;
    }
    if(!eye.contains("wide_available") || !eye["wide_available"].is_boolean() || !eye.contains("wide_unavailable_reason") || !nullable_string(eye["wide_unavailable_reason"]))return false;
  }
  return true;
}
bool save_result(const Json& j) {return j.contains("save") && (j["save"]=="saved" || j["save"]=="not_requested" || j["save"]=="failed");}
bool snapshot(const Json& j) {
  if(!j.is_object() || !j.contains("settings") || !j["settings"].is_object()) return false;
  const auto& s=j["settings"];
  return s.contains("current")&&settings(s["current"],true)&&s.contains("saved")&&settings(s["saved"],false)&&s.contains("sources")&&sources(s["sources"])&&j.contains("instance_id")&&j["instance_id"].is_string()&&j.contains("connection_id")&&j["connection_id"].is_string()&&j.contains("tracking")&&j["tracking"].is_string()&&j.contains("stream")&&j["stream"].is_object()&&j["stream"].contains("state")&&j["stream"]["state"].is_string()&&j["stream"].contains("peer")&&(j["stream"]["peer"].is_null()||j["stream"]["peer"].is_string())&&target_output(j)&&j.contains("active_profile")&&profile(j["active_profile"])&&j.contains("calibration")&&calibration(j["calibration"]);
}
}
void Model::expect(const std::string& id,const std::string& method,const Json& params) {
  reconnect_requested=false;id_=id;method_=method;params_=params;status=method=="state.get"?"Synchronizing actual state":"Pending backend response";
}
void Model::disconnected(const std::string& reason) {
  reconnect_requested=false;status=reason+(pending()?"; operation outcome unconfirmed":"");fresh=false;id_.clear();method_.clear();params_=Json::object();
}
void Model::receive(const Json& j) {
  if(!j.is_object() || !j.contains("v") || !j["v"].is_number_integer() || j["v"]!=1 || !j.contains("type") || !j["type"].is_string()) return;
  if(j["type"]=="event") {
    if(!fresh || !j.contains("event") || !j.contains("data") || !j.contains("caused_by") || !cause(j["caused_by"]))return;
    const auto& data=j["data"];
    if(!data.is_object() || !data.contains("instance_id") || data["instance_id"]!=state["instance_id"])return;
    if(j["event"]=="state.changed" && snapshot(data))state=data;
    else if(j["event"]=="calibration.changed" && data.contains("calibration") && calibration(data["calibration"]))state["calibration"]=data["calibration"];
    return;
  }
  if(j["type"]!="response" || !pending() || !j.contains("id") || !j["id"].is_string() || j["id"]!=id_ || !j.contains("ok") || !j["ok"].is_boolean() || !j.contains("result") || !j["result"].is_object() || !j.contains("error")) return;
  const bool ok=j["ok"].get<bool>(); const auto& result=j["result"];
  if((ok && !j["error"].is_null()) || (!ok && (!j["error"].is_object() || !j["error"].contains("code") || !j["error"]["code"].is_string() || !j["error"].contains("message") || !j["error"]["message"].is_string()))) return;
  if(method_=="state.get") {
    if(ok) {if(!snapshot(result)) return; state=result;fresh=true;}
  } else if(method_=="settings.patch" && (ok || !result.empty())) {
    if(!result.contains("current") || !settings(result["current"],true) || !result.contains("sources") || !sources(result["sources"]) || !target_output(result) || !save_result(result)) return;
    state["settings"]["current"]=result["current"];state["settings"]["sources"]=result["sources"];
    state["osc_target"]=result["osc_target"];state["output"]=result["output"];
    if(ok && result["save"]=="saved") for(auto it=params_["patch"].begin();it!=params_["patch"].end();++it) state["settings"]["saved"][it.key()]=result["current"][it.key()];
  }
  else if(method_.find("calibration.")==0 && (ok || !result.empty())) {
    if(!result.contains("calibration") || !calibration(result["calibration"]) || !save_result(result))return;
    state["calibration"]=result["calibration"];
  }
  reconnect_requested=!ok && (method_=="state.get" || j["error"]["code"]=="request_limit");
  status=ok?(method_=="settings.patch"?"Applied; save: "+result["save"].get<std::string>():method_.find("calibration.")==0?"Operation accepted; actual calibration state confirmed":"Actual state confirmed"):"Error: "+j["error"]["code"].get<std::string>()+": "+j["error"]["message"].get<std::string>();
  id_.clear();method_.clear();params_=Json::object();
}
IpcClient::~IpcClient(){if(fd_>=0) close(fd_);}
void IpcClient::drop(const std::string& reason) {
  if(fd_>=0) close(fd_);
  fd_=-1;connecting_=false;input_.clear();output_.clear();offset_=0;
  model.disconnected(reason);retry_=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
}
bool IpcClient::connected() {
  child_.poll(); if(!child_.running()) {drop(child_.reason()); return false;}
  ucred credentials{};socklen_t size=sizeof(credentials);
  if(getsockopt(fd_,SOL_SOCKET,SO_PEERCRED,&credentials,&size)<0 || size!=sizeof(credentials) || credentials.pid!=child_.pid() || credentials.uid!=getuid()) {
    foreign_=true;drop("Socket peer is not the owned backend; refusing attachment");child_.stop();return false;
  }
  connecting_=false;sequence_=0;++connections_;request("state.get",Json::object());return true;
}
void IpcClient::request(const std::string& method,const Json& params) {
  const std::string id="c"+std::to_string(connections_)+"r"+std::to_string(++sequence_);
  output_=Json{{"v",1},{"type","request"},{"id",id},{"method",method},{"params",params}}.dump()+"\n";offset_=0;
  model.expect(id,method,params);response_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(3);
}
bool IpcClient::patch(const Json& patch,bool save) {
  if(fd_<0 || connecting_ || !model.fresh || model.pending() || patch.empty() || !patch.is_object()) return false;
  if(sequence_>=4096) {drop("Reconnecting at request limit; apply again after synchronization");return false;}
  const auto params=Json{{"patch",patch},{"save",save}};
  if(params.dump().size()>request_limit-512) {model.status="Patch exceeds IPC size limit";return false;}
  request("settings.patch",params);return true;
}
void IpcClient::tick() {
  child_.poll();
  if(!child_.running()) {if(fd_>=0 || model.fresh || model.pending())drop(child_.reason()); else if(!foreign_)model.status=child_.reason();return;}
  if(foreign_) return;
  const auto now=std::chrono::steady_clock::now();
  if(fd_<0) {
    if(now<retry_)return;
    sockaddr_un address{};address.sun_family=AF_UNIX;
    if(path_.size()>=sizeof(address.sun_path)){foreign_=true;model.status="Control socket path too long";return;}
    std::memcpy(address.sun_path,path_.c_str(),path_.size()+1);
    fd_=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd_<0){drop("IPC socket unavailable");return;}
    const int rc=connect(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address));
    if(rc==0) {if(!connected())return;}
    else if(errno==EINPROGRESS) {connecting_=true;response_deadline_=now+std::chrono::seconds(3);}
    else {drop("IPC disconnected; last confirmed state is stale");return;}
  }
  pollfd item{fd_,static_cast<short>(POLLIN|((connecting_ || offset_<output_.size())?POLLOUT:0)),0};
  const int rc=::poll(&item,1,0);if(rc<0){if(errno!=EINTR)drop("IPC poll failed");return;}
  if(connecting_ && item.revents) {
    int error=0;socklen_t size=sizeof(error);
    if(getsockopt(fd_,SOL_SOCKET,SO_ERROR,&error,&size)<0 || error!=0) {drop("IPC connection failed");return;}
    if(!connected())return;
  }
  if(connecting_) {if(now>response_deadline_)drop("IPC connect timed out");return;}
  if(offset_<output_.size() && (item.revents&POLLOUT)) {
    const ssize_t n=send(fd_,output_.data()+offset_,output_.size()-offset_,MSG_NOSIGNAL);
    if(n>0) {offset_+=static_cast<size_t>(n);if(offset_==output_.size()){output_.clear();offset_=0;}}
    else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){drop("IPC write failed; operation outcome unconfirmed");return;}
  }
  bool eof=false;
  if(item.revents&(POLLIN|POLLHUP|POLLERR)) {
    char bytes[16384];const ssize_t n=recv(fd_,bytes,sizeof(bytes),0);
    if(n>0) input_.append(bytes,static_cast<size_t>(n));
    else if(n==0) eof=true;
    else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR){drop("IPC read failed");return;}
  }
  for(unsigned i=0;i<8;++i) {
    auto lf=input_.find('\n');if(lf==std::string::npos)break;
    if(lf+1>line_limit) {drop("IPC line exceeds 1 MiB");return;}
    std::vector<std::unordered_set<std::string>> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json& parsed) {
      if(depth>64)throw std::runtime_error("Backend JSON nesting exceeds 64");
      if(event==Json::parse_event_t::value && parsed.is_number_float() && !std::isfinite(parsed.get<double>()))throw std::runtime_error("Nonfinite backend number");
      if(event==Json::parse_event_t::object_start)keys.emplace_back();
      else if(event==Json::parse_event_t::key && !keys.empty() && !keys.back().insert(parsed.get<std::string>()).second)throw std::runtime_error("Duplicate backend JSON key");
      else if(event==Json::parse_event_t::object_end && !keys.empty())keys.pop_back();
      return true;
    };
    Json message;
    try {message=Json::parse(input_.begin(),input_.begin()+static_cast<std::ptrdiff_t>(lf),callback,true);}
    catch(const Json::out_of_range&) {drop("Nonfinite backend number");return;}
    catch(const Json::parse_error& error) {
      drop(std::string(error.what()).find("UTF-8")!=std::string::npos?"Invalid backend UTF-8":"Invalid backend JSON");return;
    }
    catch(const std::runtime_error& error) {drop(error.what());return;}
    input_.erase(0,lf+1);
    model.receive(message);
    if(model.reconnect_requested){drop(model.status+"; reconnecting for state.get. Pending edits require manual Apply.");return;}
  }
  if(input_.size()>line_limit+16384){drop("IPC input buffer limit");return;}
  if(input_.find('\n')==std::string::npos && input_.size()>=line_limit){drop("IPC line exceeds 1 MiB");return;}
  if(eof){drop("IPC disconnected; last confirmed state is stale");return;}
  if(model.pending() && now>response_deadline_)drop("IPC response timed out; operation outcome unconfirmed");
}
}
