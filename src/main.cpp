#include "overlay.hpp"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
namespace {
volatile sig_atomic_t stopped=0;
void stop_handler(int){stopped=1;}
}
int main(int argc,char** argv) {
  std::string backend,socket,config;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--help") {
      std::cout<<"FrameEyeOSC Panel 0.1.0\nUsage: frameeyeosc-panel --backend PATH --control-socket PATH [--config PATH]\n\nOwns one backend child; same socket/config and --panel-mode are passed to it.\nClose stops the owned backend; relaunch manually. Existing backends on the same socket are refused; stop legacy/different-socket instances manually.\nBackend must be a direct executable or an exec-replacing wrapper (same PID).\nDashboard hiding and stream disconnection keep the panel resident.\nCalibration unavailable in this build.\n";return 0;
    }
    if(arg=="--version"){std::cout<<"0.1.0\n";return 0;}
    if((arg!="--backend" && arg!="--control-socket" && arg!="--config") || i+1>=argc){std::cerr<<"Invalid arguments; use --help\n";return 2;}
    auto& target=arg=="--backend"?backend:arg=="--control-socket"?socket:config;
    if(!target.empty()){std::cerr<<"Duplicate option: "<<arg<<"\n";return 2;}
    target=argv[++i];
  }
  if(backend.empty()||socket.empty()){std::cerr<<"--backend and --control-socket are required\n";return 2;}
  try {
    backend=std::filesystem::absolute(backend).lexically_normal().string();
    socket=std::filesystem::absolute(socket).lexically_normal().string();
    if(!config.empty())config=std::filesystem::absolute(config).lexically_normal().string();
    struct sigaction action{};action.sa_handler=stop_handler;sigemptyset(&action.sa_mask);
    if(sigaction(SIGTERM,&action,nullptr)<0 || sigaction(SIGINT,&action,nullptr)<0)throw std::runtime_error("Cannot install termination handlers");
    panel::OwnedChild child;
    // Start before VR_Init: never fork after the OpenVR runtime creates threads.
    child.start(backend,socket,config);
    panel::IpcClient ipc(socket,child);
    panel::Overlay overlay;overlay.init();
    auto startup_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    bool synchronized=false,startup_failed=false;
    while(!stopped) {
      child.poll();ipc.tick();
      synchronized=synchronized||ipc.model.fresh;
      if(!synchronized && child.running() && std::chrono::steady_clock::now()>startup_deadline) {
        child.stop();startup_failed=true;ipc.model.disconnected("Owned backend did not confirm startup state; stopped. Close and relaunch manually.");
      }
      if(startup_failed)ipc.model.status="Owned backend startup state was not confirmed; stopped. Close and relaunch manually.";
      if(!overlay.step(ipc,child))break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    child.stop(); // Owned backend exits before overlay destruction / VR_Shutdown.
  } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
