#include "../src/ipc_client.hpp"
#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace panel;
#define CHECK(x) do { if(!(x))throw std::runtime_error(#x); } while(0)

// A second real IPC connection owns a competing session. It does not replace
// the panel model/client; requests and snapshots cross the backend socket.
class Peer {
  int fd_=-1;
  unsigned sequence_=0;
public:
  explicit Peer(const std::string& path) {
    sockaddr_un address{};address.sun_family=AF_UNIX;
    CHECK(path.size()<sizeof(address.sun_path));
    std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    fd_=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);CHECK(fd_>=0);
    if(connect(fd_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0){close(fd_);fd_=-1;throw std::runtime_error("connect second IPC peer");}
    timeval timeout{3,0};CHECK(setsockopt(fd_,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    CHECK(setsockopt(fd_,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))==0);
  }
  ~Peer(){if(fd_>=0)close(fd_);}
  Json request(const std::string& method,const Json& params=Json::object()) {
    const auto id="peer"+std::to_string(++sequence_);
    const auto wire=Json{{"v",1},{"type","request"},{"id",id},{"method",method},{"params",params}}.dump()+"\n";
    size_t offset=0;
    while(offset<wire.size()) {
      const auto written=send(fd_,wire.data()+offset,wire.size()-offset,MSG_NOSIGNAL);
      if(written<0 && errno==EINTR)continue;
      CHECK(written>0);offset+=static_cast<size_t>(written);
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    for(;;) {
      CHECK(std::chrono::steady_clock::now()<deadline);
      std::string line;char byte=0;
      do {const auto read=recv(fd_,&byte,1,0);if(read<0 && errno==EINTR)continue;CHECK(read==1);line+=byte;CHECK(line.size()<1024*1024);}while(byte!='\n');
      const auto reply=Json::parse(line);CHECK(reply["v"]==1);
      if(reply["type"]=="response" && reply["id"]==id)return reply;
      CHECK(reply["type"]=="event");
    }
  }
};
void wait(IpcClient& ipc,const std::function<bool(const Model&)>& ready) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  do {
    ipc.tick();if(ready(ipc.model))return;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }while(std::chrono::steady_clock::now()<deadline);
  throw std::runtime_error("IPC condition timed out: "+ipc.model.status+"; "+ipc.model.state.dump());
}
int main(int argc,char** argv) {try {
  if(argc!=4)return 2;
  OwnedChild child;child.start(argv[1],argv[2],argv[3]);
  auto ipc=std::make_unique<IpcClient>(argv[2],child);
  wait(*ipc,[](const Model& m){return m.fresh && !m.pending();});
  CHECK(ipc->model.state["calibration"].is_null());
  const auto old_profile=ipc->model.state["active_profile"];
  CHECK(ipc->patch(Json{{"eyelid_output_cap",0.6},{"prefix","/Panel"},{"eye_widening",false}},true));
  wait(*ipc,[](const Model& m){return !m.pending();});
  CHECK(ipc->model.state["settings"]["current"]["eye_widening"]==false);
  CHECK(ipc->model.state["settings"]["saved"]["prefix"]=="/Panel");
  CHECK(ipc->model.state["settings"]["saved"]["eyelid_output_cap"]==0.6);

  Peer other(argv[2]);CHECK(other.request("state.get")["ok"]==true);
  const auto external=other.request("calibration.start");CHECK(external["ok"]==true);
  const auto other_id=external["result"]["calibration"]["session_id"];
  // The panel has not polled the new competing session yet. Start and Cancel
  // exercise its real busy-response/queued-cancel path against that session.
  CHECK(ipc->calibrate("calibration.start"));CHECK(ipc->calibrate("calibration.cancel"));
  wait(*ipc,[](const Model& m){return !m.pending();});
  CHECK(ipc->model.status.find("busy")!=std::string::npos);
  CHECK(ipc->model.state["calibration"]["session_id"]==other_id);
  const auto still_active=other.request("state.get")["result"]["calibration"];
  CHECK(still_active["session_id"]==other_id);CHECK(still_active["state"]=="waiting");
  CHECK(other.request("calibration.cancel",Json{{"session_id",other_id}})["ok"]==true);
  wait(*ipc,[&](const Model& m){return m.state["calibration"]["state"]=="cancelled";});

  CHECK(ipc->calibrate("calibration.start"));CHECK(ipc->calibrate("calibration.cancel"));
  wait(*ipc,[](const Model& m){return !m.pending() && m.state["calibration"]["state"]=="cancelled";});
  const auto cancelled=ipc->model.state["calibration"];
  CHECK(cancelled["session_id"]!=other_id);CHECK(cancelled["result"]["reason"].is_null());
  CHECK(cancelled["result"]["caused_by"]["connection_id"]==ipc->model.state["connection_id"]);
  CHECK(ipc->model.state["active_profile"]==old_profile);

  CHECK(ipc->calibrate("calibration.start"));
  wait(*ipc,[](const Model& m){return !m.pending() && m.state["calibration"]["state"]=="waiting";});
  const auto owner_connection=ipc->model.state["connection_id"];
  const auto session=ipc->model.state["calibration"]["session_id"];
  CHECK(ipc->calibrate("calibration.begin_stage"));
  wait(*ipc,[](const Model& m){return !m.pending() && m.state["calibration"]["state"]=="preparing";});
  CHECK(ipc->model.state["calibration"]["stage"]=="closed");
  CHECK(ipc->model.state["calibration"]["remaining_ms"]>0);
  ipc.reset(); // Real fd closure; no test-only disconnect hook in production.
  ipc=std::make_unique<IpcClient>(argv[2],child);
  CHECK(!ipc->model.fresh);CHECK(!ipc->model.calibration_allowed("calibration.cancel"));
  wait(*ipc,[](const Model& m){return m.fresh && !m.pending() && m.state["calibration"]["state"]=="cancelled";});
  CHECK(ipc->model.state["connection_id"]!=owner_connection);
  CHECK(ipc->model.state["calibration"]["session_id"]==session);
  CHECK(ipc->model.state["calibration"]["result"]["reason"]=="connection_lost");
  CHECK(ipc->model.state["active_profile"]==old_profile);
  const auto final=other.request("state.get")["result"];
  CHECK(final["calibration"]["session_id"]==session);CHECK(final["calibration"]["state"]=="cancelled");
  std::cout<<"actual Rust settings/save, busy isolation, queued cancel, begin and disconnect reconciliation passed\n";
  child.stop();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
