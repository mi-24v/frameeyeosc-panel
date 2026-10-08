#include "overlay.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <iostream>
namespace panel {
bool ends_session(uint32_t event,EventOrigin origin,vr::VROverlayHandle_t event_handle,vr::VROverlayHandle_t own_handle) {
  return (event==vr::VREvent_Quit && (origin==EventOrigin::System || event_handle==own_handle)) || (event==vr::VREvent_OverlayClosed && origin==EventOrigin::OwnedOverlay && event_handle==own_handle);
}
const std::vector<std::string> Ui::fields={"eyelid_output_cap","prefix","one_euro","one_euro_min_cutoff","one_euro_beta","one_euro_d_cutoff","target_mode","target_host","target_port","shm_version"};
namespace {
std::string display(const Json& j){return j.is_string()?j.get<std::string>():j.dump();}
Json current(const Model& m) {
  if(m.state.contains("settings") && m.state["settings"].contains("current")) return m.state["settings"]["current"];
  return Json::object();
}
bool ascii(const std::string& s){for(unsigned char c:s)if(c<32 || c>126)return false;return true;}
// Original small bitmap alphabet. Lowercase glyphs remain distinct: OSC is case sensitive.
const std::map<char,std::array<unsigned char,7>> glyphs={
{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},
{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
{'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},
{'J',{7,2,2,2,18,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
{'M',{17,27,21,21,17,17,17}},{'N',{17,25,25,21,19,19,17}},{'O',{14,17,17,17,17,17,14}},
{'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
{'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},
{'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
{'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
{'a',{0,0,14,1,15,17,15}},{'b',{16,16,22,25,17,17,30}},{'c',{0,0,14,17,16,17,14}},
{'d',{1,1,13,19,17,17,15}},{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},
{'g',{0,14,17,17,15,1,14}},{'h',{16,16,22,25,17,17,17}},{'i',{4,0,12,4,4,4,14}},
{'j',{2,0,6,2,2,18,12}},{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},
{'m',{0,0,26,21,21,21,21}},{'n',{0,0,22,25,17,17,17}},{'o',{0,0,14,17,17,17,14}},
{'p',{0,0,30,17,30,16,16}},{'q',{0,0,15,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},
{'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},{'u',{0,0,17,17,17,19,13}},
{'v',{0,0,17,17,17,10,4}},{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},
{'y',{0,0,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}},
{'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
{'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
{'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
{'9',{14,17,17,15,1,1,14}},{' ',{0,0,0,0,0,0,0}},{'.',{0,0,0,0,0,12,12}},
{':',{0,12,12,0,12,12,0}},{'/',{1,1,2,4,8,16,16}},{'\\',{16,16,8,4,2,1,1}},
{'-',{0,0,0,31,0,0,0}},{'_', {0,0,0,0,0,0,31}},{'+',{0,4,4,31,4,4,0}},
{'=',{0,0,31,0,31,0,0}},{'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},
{'(', {2,4,8,8,8,4,2}},{')',{8,4,2,2,2,4,8}},{'?',{14,17,1,2,4,0,4}},
{'!',{4,4,4,4,4,0,4}},{',',{0,0,0,0,0,4,8}},{'"',{10,10,10,0,0,0,0}},
{'\'',{4,4,8,0,0,0,0}},{'<',{1,2,4,8,4,2,1}},{'>',{16,8,4,2,4,8,16}},
{'%',{17,2,4,4,8,16,17}},{'#',{10,31,10,10,31,10,0}},{'@',{14,17,23,21,23,16,14}},
{'*',{0,21,14,31,14,21,0}},{'&',{12,18,20,8,21,18,13}},{'|',{4,4,4,4,4,4,4}},
{'^',{4,10,17,0,0,0,0}},{'~',{0,0,9,22,0,0,0}},{';',{0,4,0,0,4,4,8}},
{'{',{2,4,4,8,4,4,2}},{'}',{8,4,4,2,4,4,8}},{'$',{4,15,20,14,5,30,4}},
{'`',{8,4,2,0,0,0,0}}
};
struct Canvas {
  int width,height;
  std::vector<unsigned char> bytes;
  explicit Canvas(int w=Ui::width,int h=Ui::height):width(w),height(h),bytes(static_cast<size_t>(w)*h*4){}
  void rect(int x,int y,int w,int h,std::array<unsigned char,3> c) {
    for(int yy=std::max(0,y);yy<std::min(height,y+h);++yy) for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx){size_t i=(yy*width+xx)*4;bytes[i]=c[0];bytes[i+1]=c[1];bytes[i+2]=c[2];bytes[i+3]=255;}
  }
  void text(int x,int y,const std::string& text,int scale=2,int max_width=1400,std::array<unsigned char,3> color={220,230,240}) {
    std::string shown=text;
    const size_t capacity=static_cast<size_t>(std::max(0,max_width/(6*scale)));
    if(shown.size()>capacity && capacity>0)shown=shown.substr(0,capacity-1)+">";
    int position=x;
    for(unsigned char c:shown) {
      if(position+6*scale>x+max_width) break;
      auto it=glyphs.find(static_cast<char>(c)); const auto& g=it==glyphs.end()?glyphs.at('?'):it->second;
      for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1<<(4-col)))rect(position+col*scale,y+row*scale,scale,scale,color);
      position+=6*scale;
    }
  }
};
void vrcheck(vr::EVROverlayError error,const char* operation){if(error!=vr::VROverlayError_None)throw std::runtime_error(std::string(operation)+": "+vr::VROverlay()->GetOverlayErrorNameFromEnum(error));}
}
bool Ui::edit(const std::string& key,const std::string& text,const Model& m) {
  error.clear();
  if(m.pending()){error="Wait for backend response before editing";return false;}
  if(std::find(fields.begin(),fields.end(),key)==fields.end())return false;
  Json value;
  if(key=="prefix" || key=="target_host") {
    if(!ascii(text) || text.size()>1024 || (key=="prefix" && (text.empty()||text[0]!='/')) || (key=="target_host" && text.empty())) {error="Enter a printable ASCII value (prefix starts with /)";return false;}
    value=text;
  } else if(key=="target_mode" || key=="shm_version") {
    if(key=="target_mode" && text!="auto" && text!="manual")return false;
    if(key=="shm_version" && text!="auto" && text!="4" && text!="5")return false;
    value=text;
  } else if(key=="one_euro") {
    if(text!="true" && text!="false")return false;
    value=text=="true";
  } else {
    value=Json::parse(text,nullptr,false);
    if(value.is_discarded() || !value.is_number()) {error="Enter a finite number";return false;}
    const double number=value.get<double>();
    if(!std::isfinite(number) || (key=="target_port" && (!value.is_number_integer()||number<1||number>65535)) || (key=="eyelid_output_cap" && (number<0||number>1)) || (key=="one_euro_beta" && number<0) || ((key=="one_euro_min_cutoff"||key=="one_euro_d_cutoff") && (number<=0 || number>std::numeric_limits<float>::max() || static_cast<float>(number)==0))) {error="Out of range: cap 0..1; port integer 1..65535; cutoff >0; beta >=0";return false;}
    if(key!="target_port") value=number;
  }
  draft_[key]=value;return true;
}
Json Ui::patch(const Model& m) const {
  Json result=Json::object();const auto applied=current(m);
  const Json saved=m.state.contains("settings")?m.state["settings"].value("saved",Json::object()):Json::object();
  for(const auto& [key,value]:draft_) {
    if(!applied.contains(key)||applied[key]!=value || (save && (!saved.contains(key)||saved[key]!=value)))result[key]=value;
  }
  return result;
}
void Ui::confirmed(const Model& m) {
  if(m.pending()||!m.fresh)return;
  const auto applied=current(m);
  const Json saved=m.state.contains("settings")?m.state["settings"].value("saved",Json::object()):Json::object();
  for(auto it=draft_.begin();it!=draft_.end();) {
    if(applied.contains(it->first)&&applied[it->first]==it->second&&saved.contains(it->first)&&saved[it->first]==it->second)it=draft_.erase(it);
    else ++it;
  }
}
std::string Ui::value(const std::string& key,const Model& m) const {
  const auto it=draft_.find(key);if(it!=draft_.end())return display(it->second);
  auto applied=current(m);return applied.contains(key)?display(applied[key]):"";
}
std::string Ui::click(float x,float y,const Model& m) {
  if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>=width||y<0||y>=height)return "";
  if(y>=height-55) {if(x<250)return "close";if(m.pending()||!m.fresh)return "";if(x<590){save=!save;return "save";} if(x<950)return "apply";return "";}
  if(m.pending()||!m.fresh)return "";
  if(y>=rows_top && y<rows_top+row_height*static_cast<int>(fields.size())) {
    const auto key=fields[static_cast<size_t>((y-rows_top)/row_height)];
    if(key=="one_euro") {edit(key,value(key,m)=="true"?"false":"true",m);return "changed";}
    if(key=="target_mode") {edit(key,value(key,m)=="auto"?"manual":"auto",m);return "changed";}
    if(key=="shm_version") {auto old=value(key,m);edit(key,old=="auto"?"4":old=="4"?"5":"auto",m);return "changed";}
    return key;
  }
  return "";
}
std::vector<unsigned char> Ui::render(const Model& m,const std::string& backend) const {
  Canvas c;c.rect(0,0,width,height,{16,23,35});
  c.text(24,22,"FrameEyeOSC Panel 0.1.0",3);
  c.text(24,57,m.fresh?"Actual state confirmed":"STALE / UNCONFIRMED - last known values",2,1380,m.fresh?std::array<unsigned char,3>{110,230,160}:std::array<unsigned char,3>{255,190,80});
  c.text(24,85,backend);c.text(24,108,"Setting (click to edit)          Current                      Saved                    Source");
  const auto applied=current(m);
  for(size_t i=0;i<fields.size();++i) {
    int y=rows_top+static_cast<int>(i)*row_height;const auto& key=fields[i];
    c.rect(16,y,width-32,row_height-3,i%2?std::array<unsigned char,3>{28,37,51}:std::array<unsigned char,3>{22,31,45});
    c.text(24,y+10,key,2,390);
    c.text(425,y+10,applied.contains(key)?display(applied[key]):"--",2,375);
    std::string saved="(unset)",source="--";
    if(m.state.contains("settings")) {
      const auto& s=m.state["settings"];
      if(s.contains("saved") && s["saved"].contains(key))saved=display(s["saved"][key]);
      if(s.contains("sources") && s["sources"].contains(key))source=display(s["sources"][key]);
    }
    c.text(825,y+10,saved,2,370);c.text(1230,y+10,source,2,185);
    if(draft_.count(key))c.text(425,y+36,"Draft: "+value(key,m),2,970,{130,190,255});
  }
  int y=rows_top+row_height*static_cast<int>(fields.size())+8;
  if(m.state.contains("tracking"))c.text(24,y,"Tracking: "+display(m.state["tracking"])+"   Stream: "+display(m.state["stream"]["state"])+"   Peer: "+display(m.state["stream"]["peer"]));
  if(m.state.contains("output")) {
    c.text(24,y+24,"Actual OSC: "+display(m.state["osc_target"]));
    c.text(24,y+48,"Output: "+display(m.state["output"]["state"])+"   Reason: "+display(m.state["output"]["reason"]));
  }
  c.text(24,y+75,"Calibration unavailable in this build",2,1380,{255,190,80});
  c.text(24,y+99,m.status,2,1380);
  c.text(24,y+123,error,2,1380,{255,120,120});
  c.text(24,y+149,image_status.empty()?"Drafts apply only after response. Close stops owned backend. Relaunch manually.":image_status,2,1380,{255,190,80});
  c.rect(16,height-55,228,45,{110,45,50});c.text(30,height-41,"Close",3);
  c.rect(260,height-55,320,45,{45,65,90});c.text(274,height-41,save?"Save: true":"Save: false",3);
  c.rect(595,height-55,340,45,{40,90,75});c.text(610,height-41,m.pending()?"Pending...":"Apply changes",3);
  return std::move(c.bytes);
}
namespace {
class SdkRuntime final:public OverlayRuntime {
  bool initialized_=false;
public:
  void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)override {
    vr::EVRInitError error=vr::VRInitError_None;vr::VR_Init(&error,vr::VRApplication_Overlay);
    if(error!=vr::VRInitError_None)throw std::runtime_error(std::string("OpenVR init failed: ")+vr::VR_GetVRInitErrorAsEnglishDescription(error));
    initialized_=true;
    vrcheck(vr::VROverlay()->CreateDashboardOverlay("frameeyeosc.panel","FrameEyeOSC",&main,&thumb),"CreateDashboardOverlay");
    vrcheck(vr::VROverlay()->SetOverlayWidthInMeters(main,2.0f),"SetOverlayWidthInMeters");
    vrcheck(vr::VROverlay()->SetOverlayInputMethod(main,vr::VROverlayInputMethod_Mouse),"SetOverlayInputMethod");
    vr::HmdVector2_t scale{{static_cast<float>(Ui::width),static_cast<float>(Ui::height)}};
    vrcheck(vr::VROverlay()->SetOverlayMouseScale(main,&scale),"SetOverlayMouseScale");
    vrcheck(vr::VROverlay()->SetOverlayFlag(main,vr::VROverlayFlags_EnableControlBarClose,true),"EnableControlBarClose");
  }
  void shutdown(vr::VROverlayHandle_t main,vr::VROverlayHandle_t thumb)override {
    if(!initialized_)return;
    if(thumb)vr::VROverlay()->DestroyOverlay(thumb);
    if(main)vr::VROverlay()->DestroyOverlay(main);
    vr::VR_Shutdown();initialized_=false;
  }
  bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& e)override {
    return origin==EventOrigin::System?vr::VRSystem()->PollNextEvent(&e,sizeof(e)):vr::VROverlay()->PollNextOverlayEvent(handle,&e,sizeof(e));
  }
  bool visible(vr::VROverlayHandle_t handle)override{return vr::VROverlay()->IsOverlayVisible(handle);}
  void raw(vr::VROverlayHandle_t handle,const std::vector<unsigned char>& bytes,int width,int height)override {
    vrcheck(vr::VROverlay()->SetOverlayRaw(handle,const_cast<unsigned char*>(bytes.data()),width,height,4),"SetOverlayRaw");
  }
  std::string keyboard(vr::VROverlayHandle_t handle,const std::string& key,const std::string& existing,uint64_t token)override {
    auto rc=vr::VROverlay()->ShowKeyboardForOverlay(handle,vr::k_EGamepadTextInputModeNormal,vr::k_EGamepadTextInputLineModeSingleLine,vr::KeyboardFlag_Modal,key.c_str(),1024,existing.c_str(),token);
    return rc==vr::VROverlayError_None?"":vr::VROverlay()->GetOverlayErrorNameFromEnum(rc);
  }
  std::string keyboard_text()override {
    char text[1025]{};uint32_t length=vr::VROverlay()->GetKeyboardText(text,sizeof(text));
    if(length>sizeof(text)||!std::memchr(text,0,sizeof(text)))throw std::runtime_error("Keyboard value exceeds input limit");
    return text;
  }
  void hide_keyboard()override{vr::VROverlay()->HideKeyboard();}
  void acknowledge_quit()override{vr::VRSystem()->AcknowledgeQuit_Exiting();}
};
}
Overlay::Overlay():sdk_(std::make_unique<SdkRuntime>()),runtime_(sdk_.get()){}
Overlay::~Overlay(){if(!keyboard_key_.empty())runtime_->hide_keyboard();runtime_->shutdown(handle_,thumbnail_);}
void Overlay::init(){runtime_->init(handle_,thumbnail_);}
bool Overlay::step(IpcClient& ipc,OwnedChild& child) {
  return step(ipc.model,child.reason(),[&](const Json& patch,bool save){return ipc.patch(patch,save);});
}
bool Overlay::step(Model& model,const std::string& backend,const std::function<bool(const Json&,bool)>& submit) {
  auto failed_image=[&](vr::VROverlayHandle_t handle,const std::string& detail) {
    auto& image=handle==handle_?main_image_:thumb_image_;
    image.signature.clear();image.retry_after=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
    if(image.failures<3) {
      ++image.failures;
      image.error=(handle==handle_?"Dashboard":"Thumbnail")+std::string(" image failed: ")+detail+(image.failures>=3?"; retries exhausted; relaunch manually":"; retry pending");
      std::cerr<<image.error<<"\n";
    }
  };
  auto event=[&](const vr::VREvent_t& e,EventOrigin origin,vr::VROverlayHandle_t queue_handle)->bool {
    const bool own_queue=origin==EventOrigin::OwnedOverlay && (queue_handle==handle_ || queue_handle==thumbnail_);
    if(ends_session(e.eventType,origin,own_queue?handle_:queue_handle,handle_)) {if(e.eventType==vr::VREvent_Quit)runtime_->acknowledge_quit();return false;}
    if(own_queue && e.eventType==vr::VREvent_ImageFailed)failed_image(queue_handle,"SDK ImageFailed");
    if(own_queue && e.eventType==vr::VREvent_ImageLoaded) {
      auto& image=queue_handle==handle_?main_image_:thumb_image_;image.failures=0;image.error.clear();
    }
    if(e.eventType==vr::VREvent_KeyboardDone && !keyboard_key_.empty() && e.data.keyboard.overlayHandle==handle_ && e.data.keyboard.uUserValue==keyboard_token_) {
      try{ui.edit(keyboard_key_,runtime_->keyboard_text(),model);}catch(const std::exception& error){ui.error=error.what();}
      keyboard_key_.clear();keyboard_opened_=false;runtime_->hide_keyboard();
    }
    // Global open/close notifications guarantee the overlay handle, not a token.
    // A close preceding this request's Opened notification belongs to an older keyboard.
    if(e.eventType==vr::VREvent_KeyboardOpened_Global && e.data.keyboard.overlayHandle==handle_ && !keyboard_key_.empty())keyboard_opened_=true;
    if(((e.eventType==vr::VREvent_KeyboardClosed_Global && e.data.keyboard.overlayHandle==handle_) || (e.eventType==vr::VREvent_KeyboardClosed && own_queue && queue_handle==handle_)) && keyboard_opened_) {
      keyboard_key_.clear();keyboard_opened_=false;
    }
    if(e.eventType==vr::VREvent_MouseButtonDown && own_queue && queue_handle==handle_ && e.data.mouse.button==vr::VRMouseButton_Left) {
      // SDK mouse origin is bottom-left; the CPU canvas uses top-left.
      const float x=e.data.mouse.x,y=Ui::height-e.data.mouse.y;
      const bool close_hit=std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<250&&y>=Ui::height-55&&y<Ui::height;
      if(!keyboard_key_.empty() && !close_hit)return true;
      const auto action=ui.click(x,y,model);
      if(action=="close") {
        if(!keyboard_key_.empty())runtime_->hide_keyboard();
        keyboard_key_.clear();keyboard_opened_=false;return false;
      }
      if(action=="apply") {
        const auto patch=ui.patch(model);
        if(patch.empty())ui.error="No changed or unsaved selected values";
        else {ui.error.clear();submit(patch,ui.save);} // Failure reason belongs to model.status.
      } else if(action!="" && action!="changed" && action!="save") {
        const auto token=++keyboard_token_;
        auto error=runtime_->keyboard(handle_,action,ui.value(action,model),token);
        if(error.empty()){keyboard_key_=action;keyboard_opened_=false;}
        else ui.error="Keyboard failed: "+error;
      }
    }
    return true;
  };
  vr::VREvent_t e{};
  for(unsigned i=0;i<64 && runtime_->poll(EventOrigin::System,handle_,e);++i)if(!event(e,EventOrigin::System,0))return false;
  for(unsigned i=0;i<64 && runtime_->poll(EventOrigin::OwnedOverlay,handle_,e);++i)if(!event(e,EventOrigin::OwnedOverlay,handle_))return false;
  for(unsigned i=0;i<64 && runtime_->poll(EventOrigin::OwnedOverlay,thumbnail_,e);++i)if(!event(e,EventOrigin::OwnedOverlay,thumbnail_))return false;
  // No optimistic draft clearing: clear only values now confirmed by the backend.
  ui.confirmed(model);
  ui.image_status=main_image_.error;
  if(!thumb_image_.error.empty())ui.image_status+=(ui.image_status.empty()?"":"; ")+thumb_image_.error;
  const auto now=std::chrono::steady_clock::now();
  if(runtime_->visible(handle_) && main_image_.failures<3 && now>=main_image_.retry_after) {
    const std::string signature=model.state.dump()+model.status+backend+ui.patch(model).dump()+ui.error+ui.image_status+(ui.save?"save":"nosave")+(model.fresh?"fresh":"stale");
    if(signature!=main_image_.signature) {
      auto bytes=ui.render(model,backend);
      try{runtime_->raw(handle_,bytes,Ui::width,Ui::height);main_image_.signature=signature;}
      catch(const std::exception& error){failed_image(handle_,error.what());}
    }
  }
  if(runtime_->visible(thumbnail_) && thumb_image_.signature.empty() && thumb_image_.failures<3 && now>=thumb_image_.retry_after) {
    constexpr int width=256,height=128;
    Canvas c(width,height);c.rect(0,0,width,height,{25,60,85});c.text(25,50,"FrameEyeOSC",3,230);
    try{runtime_->raw(thumbnail_,c.bytes,width,height);thumb_image_.signature="shown";}
    catch(const std::exception& error){failed_image(thumbnail_,error.what());}
  }
  return true;
}
}
