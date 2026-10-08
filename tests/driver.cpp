#include "../src/child.hpp"
#include "../src/ipc_client.hpp"
#include "../src/overlay.hpp"
#include <deque>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
using namespace panel;
struct DriverRuntime:OverlayRuntime {
  std::deque<vr::VREvent_t> events;
  void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)override{main=10;thumb=11;}
  void shutdown(vr::VROverlayHandle_t,vr::VROverlayHandle_t)override{}
  bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& event)override {
    if(events.empty() || (origin==EventOrigin::System)!=(events.front().eventType==vr::VREvent_Quit || events.front().eventType==vr::VREvent_ProcessQuit))return false;
    if(origin==EventOrigin::OwnedOverlay && handle!=10)return false;
    event=events.front();events.pop_front();return true;
  }
  bool visible(vr::VROverlayHandle_t)override{return false;}
  void raw(vr::VROverlayHandle_t,const std::vector<unsigned char>&)override{}
  std::string keyboard(vr::VROverlayHandle_t,const std::string&,const std::string&,uint64_t)override{return "unavailable";}
  std::string keyboard_text()override{return "";}
  void hide_keyboard()override{}
  void acknowledge_quit()override{}
};
int main(int argc, char** argv) {
  if (argc<4) return 2;
  std::string mode=argv[1];
  try {
    OwnedChild child;
    // Verify the pre-exec path resets ignored and blocked termination signals.
    signal(SIGTERM,SIG_IGN); signal(SIGINT,SIG_IGN);
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask,SIGTERM); sigprocmask(SIG_BLOCK,&mask,nullptr);
    child.start(argv[2],argv[3],argc>4?argv[4]:"");
    std::cout<<"PID "<<child.pid()<<std::endl;
    IpcClient ipc(argv[3],child);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool patched=false, fresh_again=false;
    while(std::chrono::steady_clock::now()<deadline) {
      child.poll(); ipc.tick();
      if(mode=="longsnapshot" && ipc.model.fresh) {if(ipc.model.state["settings"]["current"]["prefix"].get<std::string>().size()!=40001)return 9;std::cout<<"long valid snapshot accepted\n";child.stop();return 0;}
      if(mode=="lifecycle" && ipc.model.fresh) break;
      if((mode=="race" || mode=="reconnect-race") && ipc.ownership_failed()) { child.stop(); std::cout<<"foreign peer rejected\n"; return 0; }
      if(mode=="protocol" && (ipc.model.status.find("Invalid backend JSON")!=std::string::npos || ipc.model.status.find("1 MiB")!=std::string::npos || ipc.model.status.find("buffer limit")!=std::string::npos)) {std::cout<<ipc.model.status<<"\n";child.stop();return 0;}
      if(mode=="crash" && !child.running()) { std::cout<<child.reason()<<"\n"; return 0; }
      if(mode=="real" && ipc.model.fresh && !patched) {
        if(ipc.model.state["settings"]["current"]["target_mode"]!="manual")return 7;
        patched=ipc.patch(Json{{"eyelid_output_cap",0.6},{"prefix","/Panel"},{"one_euro",true},{"one_euro_min_cutoff",0.75},{"one_euro_beta",2.0},{"one_euro_d_cutoff",1.0},{"target_mode","manual"},{"target_host","127.0.0.1"},{"target_port",19001},{"shm_version","auto"}},true);
      } else if(mode=="real" && patched && !ipc.model.pending()) {
        if(ipc.model.state["settings"]["current"]["eyelid_output_cap"]!=0.6 || ipc.model.state["settings"]["saved"]["prefix"]!="/Panel" || ipc.model.state["osc_target"]!="127.0.0.1:19001")return 8;
        std::cout<<"real backend settings/save/actual OSC confirmed\n";child.stop();return 0;
      }
      if(mode=="ipc" || mode=="reconnect-race") {
        if(ipc.model.fresh && !patched) { ipc.patch(Json{{"eyelid_output_cap",0.5}},true); patched=true; }
        if(patched && ipc.connections()>1 && ipc.model.fresh && !ipc.model.pending()) {fresh_again=true; break;}
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(mode=="ipc") {
      if(!fresh_again || ipc.model.state["settings"]["current"]["eyelid_output_cap"]!=1.0) return 5;
      std::cout<<"reconciled actual state; no patch replay\n";
    } else if(mode=="lifecycle") {
      if(!ipc.model.fresh) return 4;
      std::cout<<"READY\n"<<std::flush;
      DriverRuntime runtime;Overlay overlay(runtime);overlay.init();
      std::string command;
      while(std::getline(std::cin,command)) {
        vr::VREvent_t event{};
        if(command=="hide")event.eventType=vr::VREvent_OverlayHidden;
        else if(command=="remotequit")event.eventType=vr::VREvent_ProcessQuit;
        else if(command=="localquit")event.eventType=vr::VREvent_Quit;
        else if(command=="close")event.eventType=vr::VREvent_OverlayClosed;
        else return 6;
        runtime.events.push_back(event);
        if(!overlay.step(ipc,child))break;
        child.poll();if(!child.running())return 10;
        std::cout<<"RESIDENT "<<child.pid()<<std::endl;
      }
      child.stop(); // Same lifecycle order as main: stop child before overlay destruction.
    } else return 3;
    child.stop(); std::cout<<"STOP "<<child.reason()<<"\n";
  } catch(const std::exception& e) { std::cout<<"ERROR "<<e.what()<<"\n"; return 1; }
}
