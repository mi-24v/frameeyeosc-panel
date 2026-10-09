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
  void raw(vr::VROverlayHandle_t,const std::vector<unsigned char>&,int,int)override{}
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
#ifdef PANEL_CHILD_TEST_HOOK
    if(mode=="pre-prctl" && argc==7)child.set_test_gate(std::stoi(argv[5]),std::stoi(argv[6]));
#endif
    // Verify the pre-exec path resets ignored and blocked termination signals.
    signal(SIGTERM,SIG_IGN); signal(SIGINT,SIG_IGN);
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask,SIGTERM); sigprocmask(SIG_BLOCK,&mask,nullptr);
    child.start(argv[2],argv[3],argc>4?argv[4]:"");
    std::cout<<"PID "<<child.pid()<<std::endl;
    IpcClient ipc(argv[3],child);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool patched=false, fresh_again=false;
    int calibration_stage=0;bool saw_stale=false;
    std::chrono::steady_clock::time_point crashed{};
    std::vector<unsigned char> initial_crash_image;
    DriverRuntime save_runtime;Overlay save_overlay(save_runtime);save_overlay.init();int save_stage=0;
    auto apply_ui=[&] {vr::VREvent_t event{};event.eventType=vr::VREvent_MouseButtonDown;event.data.mouse={650,20,vr::VRMouseButton_Left,0};save_runtime.events.push_back(event);save_overlay.step(ipc,child);};
    while(std::chrono::steady_clock::now()<deadline) {
      child.poll(); ipc.tick();
      if(mode.rfind("calibration",0)==0) {
        if(!ipc.model.fresh && calibration_stage>0) {
          saw_stale=true;
          if(ipc.model.calibration_allowed("calibration.start") || ipc.model.calibration_allowed("calibration.cancel"))return 20;
        }
        if(ipc.model.fresh && !ipc.model.pending()) {
          const auto cal=ipc.model.state.value("calibration",Json());
          if(calibration_stage==0) {
            save_overlay.ui.calibration_page=true;
            save_runtime.events.push_back(vr::VREvent_t{});
            auto click=[&](float x){vr::VREvent_t e{};e.eventType=vr::VREvent_MouseButtonDown;e.data.mouse={x,Ui::height-890,vr::VRMouseButton_Left,0};save_runtime.events.push_back(e);};
            click(30);if(mode=="calibration-cancel" || mode=="calibration-startfailed")click(1000);
            if(!save_overlay.step(ipc,child) || !ipc.model.pending())return 21;
            calibration_stage=1;
          } else if(mode=="calibration-startfailed") {
            if(cal["session_id"]!="other-active" || cal["state"]!="waiting" || ipc.model.status.find("busy")==std::string::npos)return 27;
            std::cout<<"failed start did not cancel another active session\n";child.stop();return 0;
          } else if(mode=="calibration-cancel") {
            if(cal["state"]!="cancelled" || ipc.model.state["active_profile"].is_null())return 22;
            std::cout<<"pending start cancelled in order\n";child.stop();return 0;
          } else if(mode=="calibration-disconnect" && ipc.connections()>1) {
            if(!saw_stale || cal["state"]!="cancelled" || cal["result"]["reason"]!="connection_lost" || ipc.model.state["active_profile"].is_null())return 23;
            std::cout<<"disconnect reconciled terminal state without replay\n";child.stop();return 0;
          } else if(cal["state"]=="waiting") {
            const auto operation=cal["stage"]=="wide" && mode!="calibration-wide"?"calibration.skip_wide":"calibration.begin_stage";
            if(!ipc.calibrate(operation))return 24;
            ++calibration_stage;
          } else if(cal["state"]=="completed" || cal["state"]=="failed") {
            if(mode=="calibration-failed") {
              if(cal["state"]!="failed" || cal["result"]["reason"]!="save_failed" || ipc.model.state["active_profile"]["left"]["closed"]!=0.1 || ipc.model.status.find("save_failed")==std::string::npos)return 25;
            } else if(cal["state"]!="completed" || ipc.model.state["active_profile"]["left"]["closed"]!=0.2)return 26;
            std::cout<<"calibration stages and applied profile confirmed\n";child.stop();return 0;
          }
        }
      }
      if(mode=="save-ui" && ipc.model.fresh && !ipc.model.pending()) {
        save_overlay.step(ipc,child);
        if(save_stage==0){if(!save_overlay.ui.edit("eyelid_output_cap","0.6",ipc.model))return 11;apply_ui();save_stage=1;}
        else if(save_stage==1){if(ipc.model.state["settings"]["current"]["eyelid_output_cap"]!=0.6 || ipc.model.state["settings"]["saved"].contains("eyelid_output_cap"))return 12;save_overlay.ui.save=true;if(save_overlay.ui.patch(ipc.model)!=Json{{"eyelid_output_cap",0.6}})return 13;apply_ui();save_stage=2;}
        else if(save_stage==2){if(ipc.model.state["settings"]["saved"]["eyelid_output_cap"]!=0.6)return 14;save_overlay.ui.edit("prefix",save_overlay.ui.value("prefix",ipc.model),ipc.model);if(save_overlay.ui.patch(ipc.model).size()!=1 || !save_overlay.ui.patch(ipc.model).contains("prefix"))return 15;apply_ui();save_stage=3;}
        else {if(ipc.model.state["settings"]["saved"]["prefix"]!=ipc.model.state["settings"]["current"]["prefix"] || !save_overlay.ui.patch(ipc.model).empty())return 16;std::cout<<"UI trial then save and selected current field confirmed\n";child.stop();return 0;}
      }
      if(mode=="state-error" && ipc.connections()>1 && ipc.model.fresh && !ipc.model.pending()){std::cout<<"state error recovered by state.get\n";child.stop();return 0;}
      if(mode=="longsnapshot" && ipc.model.fresh) {if(ipc.model.state["settings"]["current"]["prefix"].get<std::string>().size()!=40001)return 9;std::cout<<"long valid snapshot accepted\n";child.stop();return 0;}
      if(mode=="lifecycle" && ipc.model.fresh) break;
      if((mode=="race" || mode=="reconnect-race") && ipc.ownership_failed()) { child.stop(); std::cout<<"foreign peer rejected\n"; return 0; }
      if(mode=="protocol" && (ipc.model.status.find("Invalid backend JSON")!=std::string::npos || ipc.model.status.find("1 MiB")!=std::string::npos || ipc.model.status.find("buffer limit")!=std::string::npos || ipc.model.status.find("Invalid backend UTF-8")!=std::string::npos || ipc.model.status.find("Duplicate backend JSON key")!=std::string::npos || ipc.model.status.find("Backend JSON nesting")!=std::string::npos || ipc.model.status.find("Nonfinite backend number")!=std::string::npos)) {std::cout<<ipc.model.status<<"\n";child.stop();return 0;}
      if(mode=="crash" && !child.running()) {
        Ui crash_ui;
        if(crashed==std::chrono::steady_clock::time_point{}){crashed=std::chrono::steady_clock::now();initial_crash_image=crash_ui.render(ipc.model,"Backend running");}
        if(ipc.model.status.find("exit 23")==std::string::npos || ipc.model.fresh || ipc.model.pending())return 17;
        if(crash_ui.render(ipc.model,child.reason())==initial_crash_image)return 18;
        if(std::chrono::steady_clock::now()-crashed>=std::chrono::seconds(1)){std::cout<<child.reason()<<"; continued IPC and rendering after exit\n";return 0;}
      }
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
        else if(command=="uiclose") {event.eventType=vr::VREvent_MouseButtonDown;event.data.mouse={30,20,vr::VRMouseButton_Left,0};}
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
