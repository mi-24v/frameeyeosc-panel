#include "../src/overlay.hpp"
#include <deque>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
using namespace panel;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
struct FakeRuntime:OverlayRuntime {
  bool shown=false,thumb_shown=false,fail_keyboard=false;
  unsigned renders=0,main_uploads=0,thumb_uploads=0,shutdowns=0,acknowledgments=0,hides=0;
  size_t thumb_bytes=0;
  uint64_t token=0;
  std::string typed,initial;
  std::deque<vr::VREvent_t> system,overlay,thumbnail;
  void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)override{main=10;thumb=11;}
  void shutdown(vr::VROverlayHandle_t,vr::VROverlayHandle_t)override{++shutdowns;}
  bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& e)override{
    auto& queue=origin==EventOrigin::System?system:handle==11?thumbnail:overlay;if(queue.empty())return false;e=queue.front();queue.pop_front();return true;
  }
  bool visible(vr::VROverlayHandle_t handle)override{return (shown && handle==10)||(thumb_shown && handle==11);}
  void raw(vr::VROverlayHandle_t handle,const std::vector<unsigned char>& bytes,int width,int height)override{CHECK(bytes.size()==static_cast<size_t>(width)*height*4);++renders;if(handle==10)++main_uploads;else{++thumb_uploads;thumb_bytes=bytes.size();}}
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
    for(const auto& point:std::vector<std::pair<float,float>>{{5,Ui::height-20},{15,Ui::height-20},{244,Ui::height-20},{249,Ui::height-20},{100,Ui::height-10},{100,Ui::height-1}}) {
      cancelled.click(point.first,point.second);CHECK(overlay.step(cancel_model,"Backend running",send));CHECK(cancelled.hides==0);
    }
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
  FakeRuntime images;images.shown=true;Model image_model;image_model.expect("s1","state.get",Json::object());image_model.receive(response);
  {Overlay overlay(images);overlay.init();auto no_send=[](const Json&,bool){return false;};
    CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.main_uploads==1);
    images.emit(vr::VREvent_ImageFailed,EventOrigin::OwnedOverlay);images.shown=false;
    CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.main_uploads==1);
    CHECK(overlay.ui.image_status.find("Dashboard image failed")!=std::string::npos);
    Ui clean_ui=overlay.ui;clean_ui.image_status.clear();CHECK(overlay.ui.render(image_model,"Backend running")!=clean_ui.render(image_model,"Backend running"));
    std::this_thread::sleep_for(std::chrono::milliseconds(260));images.shown=true;
    CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.main_uploads==2);
    images.emit(vr::VREvent_ImageFailed,EventOrigin::OwnedOverlay);CHECK(overlay.step(image_model,"Backend running",no_send));
    std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.main_uploads==3);
    images.emit(vr::VREvent_ImageFailed,EventOrigin::OwnedOverlay);CHECK(overlay.step(image_model,"Backend running",no_send));
    std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.main_uploads==3);
    CHECK(overlay.ui.image_status.find("retries exhausted")!=std::string::npos);
    images.thumb_shown=true;CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.thumb_bytes<1024*1024);
    vr::VREvent_t failed{};failed.eventType=vr::VREvent_ImageFailed;images.thumbnail.push_back(failed);CHECK(overlay.step(image_model,"Backend running",no_send));
    std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(images.thumb_uploads==2);
  }
  FakeRuntime recovery;recovery.shown=true;
  {Overlay overlay(recovery);overlay.init();auto no_send=[](const Json&,bool){return false;};
    CHECK(overlay.step(image_model,"Backend running",no_send));
    for(int i=0;i<2;++i){recovery.emit(vr::VREvent_ImageFailed,EventOrigin::OwnedOverlay);CHECK(overlay.step(image_model,"Backend running",no_send));std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(overlay.step(image_model,"Backend running",no_send));}
    recovery.emit(vr::VREvent_ImageLoaded,EventOrigin::OwnedOverlay);CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(overlay.ui.image_status.empty());
    for(int i=0;i<2;++i){auto uploads=recovery.main_uploads;recovery.emit(vr::VREvent_ImageFailed,EventOrigin::OwnedOverlay);CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(overlay.ui.image_status.find("retry pending")!=std::string::npos);std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(overlay.step(image_model,"Backend running",no_send));CHECK(recovery.main_uploads==uploads+1);}
  }
  FakeRuntime discard;discard.shown=true;Model discard_model=image_model;
  {Overlay overlay(discard);overlay.init();overlay.ui.edit("eyelid_output_cap","0.6",discard_model);discard_model.state["settings"]["current"]["eyelid_output_cap"]=0.6;
    CHECK(overlay.step(discard_model,"Backend running",[](const Json&,bool){return false;}));auto before_discard=discard.main_uploads;
    discard.click(1050,Ui::height-20);CHECK(overlay.step(discard_model,"Backend running",[](const Json&,bool){return false;}));
    CHECK(discard.main_uploads==before_discard+1);CHECK(discard_model.state["settings"]["current"]["eyelid_output_cap"]==0.6);
    overlay.ui.save=true;overlay.ui.edit("prefix","/Selected",discard_model);Json sent;
    discard.click(650,Ui::height-20);CHECK(overlay.step(discard_model,"Backend running",[&](const Json& patch,bool save){CHECK(save);sent=patch;return true;}));CHECK((sent==Json{{"prefix","/Selected"}}));
  }
  // Case preservation must be visible in the actual bitmap, not only model storage.
  Ui ui;Model lower=model;lower.state["settings"]["current"]["prefix"]="/ft";
  CHECK(ui.render(model,"Backend running")!=ui.render(lower,"Backend running"));
  std::cout<<"overlay runtime and keyboard contracts passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
