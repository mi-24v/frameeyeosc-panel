#include "../src/overlay.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace panel;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
struct FakeRuntime:OverlayRuntime {
  bool shown=false,fail_keyboard=false;
  unsigned renders=0,shutdowns=0,acknowledgments=0,hides=0;
  uint64_t token=0;
  std::string typed,initial;
  std::deque<vr::VREvent_t> system,overlay,thumbnail;
  void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)override{main=10;thumb=11;}
  void shutdown(vr::VROverlayHandle_t,vr::VROverlayHandle_t)override{++shutdowns;}
  bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& e)override{
    auto& queue=origin==EventOrigin::System?system:handle==11?thumbnail:overlay;if(queue.empty())return false;e=queue.front();queue.pop_front();return true;
  }
  bool visible(vr::VROverlayHandle_t handle)override{return shown && handle==10;}
  void raw(vr::VROverlayHandle_t,const std::vector<unsigned char>& bytes)override{CHECK(bytes.size()==Ui::width*Ui::height*4);++renders;}
  std::string keyboard(vr::VROverlayHandle_t,const std::string&,const std::string& existing,uint64_t value)override{initial=existing;token=value;return fail_keyboard?"unavailable":"";}
  std::string keyboard_text()override{return typed;}
  void hide_keyboard()override{++hides;}
  void acknowledge_quit()override{++acknowledgments;}
  void emit(uint32_t type,EventOrigin origin=EventOrigin::System){vr::VREvent_t e{};e.eventType=type;(origin==EventOrigin::System?system:overlay).push_back(e);}
  void click(float x,float y){vr::VREvent_t e{};e.eventType=vr::VREvent_MouseButtonDown;e.data.mouse={x,Ui::height-y,vr::VRMouseButton_Left,0};overlay.push_back(e);}
  void done(uint64_t handle=10,uint64_t value=0){vr::VREvent_t e{};e.eventType=vr::VREvent_KeyboardDone;e.data.keyboard.overlayHandle=handle;e.data.keyboard.uUserValue=value?value:token;overlay.push_back(e);}
};
int main(){try{
  std::ifstream f(FIXTURE_PATH);std::string line;std::getline(f,line);std::getline(f,line);auto response=Json::parse(line);
  Model model;model.expect("s1","state.get",Json::object());model.receive(response);
  FakeRuntime fake;Json submitted;bool submitted_save=false;
  auto send=[&](const Json& patch,bool save){submitted=patch;submitted_save=save;model.expect("p","settings.patch",Json{{"patch",patch},{"save",save}});return true;};
  {
    Overlay overlay(fake);overlay.init();
    CHECK(overlay.step(model,"Backend running",send));CHECK(fake.renders==0);
    fake.shown=true;CHECK(overlay.step(model,"Backend running",send));CHECK(fake.renders==1);
    CHECK(overlay.step(model,"Backend running",send));CHECK(fake.renders==1);
    fake.shown=false;model.status="hidden changed";CHECK(overlay.step(model,"Backend running",send));CHECK(fake.renders==1);
    fake.emit(vr::VREvent_DashboardDeactivated);fake.emit(vr::VREvent_OverlayHidden,EventOrigin::OwnedOverlay);fake.emit(vr::VREvent_ProcessQuit);fake.emit(vr::VREvent_ProcessDisconnected);
    CHECK(overlay.step(model,"Backend running",send));CHECK(fake.renders==1);
    fake.click(30,Ui::rows_top+Ui::row_height+5);CHECK(overlay.step(model,"Backend running",send));CHECK(fake.initial=="/FT");
    fake.typed="/lowerCase";fake.done(999);CHECK(overlay.step(model,"Backend running",send));CHECK(overlay.ui.patch(model).empty());
    fake.done(10,fake.token+1);CHECK(overlay.step(model,"Backend running",send));CHECK(overlay.ui.patch(model).empty());
    fake.done();CHECK(overlay.step(model,"Backend running",send));CHECK(overlay.ui.patch(model)["prefix"]=="/lowerCase");CHECK(fake.hides==1);
    fake.click(300,Ui::height-20);fake.click(650,Ui::height-20);CHECK(overlay.step(model,"Backend running",send));CHECK((submitted==Json{{"prefix","/lowerCase"}}));CHECK(submitted_save);CHECK(model.pending());
    fake.emit(vr::VREvent_OverlayClosed);CHECK(overlay.step(model,"Backend running",send));
    fake.emit(vr::VREvent_OverlayClosed,EventOrigin::OwnedOverlay);CHECK(!overlay.step(model,"Backend running",send));
  }
  CHECK(fake.shutdowns==1);
  FakeRuntime quit;
  {Overlay overlay(quit);overlay.init();quit.emit(vr::VREvent_Quit);CHECK(!overlay.step(model,"Backend running",send));CHECK(quit.acknowledgments==1);}
  FakeRuntime thumb;
  {Overlay overlay(thumb);overlay.init();vr::VREvent_t e{};e.eventType=vr::VREvent_OverlayClosed;thumb.thumbnail.push_back(e);CHECK(!overlay.step(model,"Backend running",send));}
  FakeRuntime overlay_quit;
  {Overlay overlay(overlay_quit);overlay.init();overlay_quit.emit(vr::VREvent_Quit,EventOrigin::OwnedOverlay);CHECK(!overlay.step(model,"Backend running",send));CHECK(overlay_quit.acknowledgments==1);}
  FakeRuntime keyboard_runtime;Model keyboard_model;keyboard_model.expect("s1","state.get",Json::object());keyboard_model.receive(response);
  {Overlay overlay(keyboard_runtime);overlay.init();
    keyboard_runtime.fail_keyboard=true;keyboard_runtime.click(30,Ui::rows_top+Ui::row_height+5);CHECK(overlay.step(keyboard_model,"Backend running",send));CHECK(overlay.ui.error.find("Keyboard failed")!=std::string::npos);CHECK(overlay.ui.patch(keyboard_model).empty());
    keyboard_runtime.fail_keyboard=false;keyboard_runtime.click(30,Ui::rows_top+Ui::row_height+5);CHECK(overlay.step(keyboard_model,"Backend running",send));
    auto old_token=keyboard_runtime.token;keyboard_runtime.typed="/First";keyboard_runtime.done();CHECK(overlay.step(keyboard_model,"Backend running",send));
    keyboard_runtime.click(30,Ui::rows_top+Ui::row_height+5);CHECK(overlay.step(keyboard_model,"Backend running",send));
    vr::VREvent_t stale_close{};stale_close.eventType=vr::VREvent_KeyboardClosed_Global;stale_close.data.keyboard.overlayHandle=10;stale_close.data.keyboard.uUserValue=old_token;keyboard_runtime.system.push_front(stale_close);
    keyboard_runtime.typed="/Second";keyboard_runtime.done();CHECK(overlay.step(keyboard_model,"Backend running",send));CHECK(overlay.ui.patch(keyboard_model)["prefix"]=="/Second");
  }
  FakeRuntime cancelled;Model cancel_model;cancel_model.expect("s1","state.get",Json::object());cancel_model.receive(response);
  {Overlay overlay(cancelled);overlay.init();
    cancelled.click(30,Ui::rows_top+5);CHECK(overlay.step(cancel_model,"Backend running",send));
    vr::VREvent_t opened{};opened.eventType=vr::VREvent_KeyboardOpened_Global;opened.data.keyboard.overlayHandle=10;cancelled.system.push_back(opened);
    vr::VREvent_t closed{};closed.eventType=vr::VREvent_KeyboardClosed_Global;closed.data.keyboard.overlayHandle=10;closed.data.keyboard.uUserValue=0;cancelled.system.push_back(closed);
    CHECK(overlay.step(cancel_model,"Backend running",send));
    cancelled.click(30,Ui::rows_top+Ui::row_height+5);CHECK(overlay.step(cancel_model,"Backend running",send));CHECK(cancelled.initial=="/FT");
    cancelled.click(30,Ui::height-20);CHECK(!overlay.step(cancel_model,"Backend running",send));CHECK(cancelled.hides>=1);
  }
  FakeRuntime direct_close;
  {Overlay overlay(direct_close);overlay.init();direct_close.click(30,Ui::height-20);CHECK(!overlay.step(cancel_model,"Backend running",send));}
  FakeRuntime submission;Model submission_model;submission_model.expect("s1","state.get",Json::object());submission_model.receive(response);
  {Overlay overlay(submission);overlay.init();overlay.ui.edit("prefix","/Change",submission_model);
    submission.click(650,Ui::height-20);
    auto refused=[&](const Json&,bool){submission_model.disconnected("Reconnecting at request limit; apply again after synchronization");return false;};
    CHECK(overlay.step(submission_model,"Backend running",refused));CHECK(overlay.ui.error.empty());CHECK(submission_model.status.find("Reconnecting at request limit")!=std::string::npos);
  }
  // Case preservation must be visible in the actual bitmap, not only model storage.
  Ui ui;Model lower=model;lower.state["settings"]["current"]["prefix"]="/ft";
  CHECK(ui.render(model,"Backend running")!=ui.render(lower,"Backend running"));
  std::cout<<"overlay runtime and keyboard contracts passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
