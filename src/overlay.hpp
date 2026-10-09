#pragma once
#include "ipc_client.hpp"
#include <openvr.h>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <chrono>
namespace panel {
enum class EventOrigin {System, OwnedOverlay};
bool ends_session(uint32_t event,EventOrigin origin,vr::VROverlayHandle_t event_handle,vr::VROverlayHandle_t own_handle);
class Ui {
  std::map<std::string,Json> draft_;
  uint64_t revision_=0;
public:
  static constexpr int width=1440,height=1080,rows_top=130,row_height=65;
  static const std::vector<std::string> fields;
  bool save=false;
  std::string error,image_status;
  bool edit(const std::string& key,const std::string& text,const Model& model);
  Json patch(const Model& model) const;
  std::string value(const std::string& key,const Model& model) const;
  std::string click(float x,float y,const Model& model);
  void confirmed(const Model& model);
  uint64_t revision() const{return revision_;}
  std::vector<unsigned char> render(const Model& model,const std::string& backend) const;
};
class OverlayRuntime {
public:
  virtual ~OverlayRuntime()=default;
  virtual std::chrono::steady_clock::time_point now(){return std::chrono::steady_clock::now();}
  virtual void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)=0;
  virtual void shutdown(vr::VROverlayHandle_t main,vr::VROverlayHandle_t thumb)=0;
  virtual bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& event)=0;
  virtual bool visible(vr::VROverlayHandle_t handle)=0;
  virtual void raw(vr::VROverlayHandle_t handle,const std::vector<unsigned char>& bytes,int width,int height)=0;
  virtual std::string keyboard(vr::VROverlayHandle_t handle,const std::string& key,const std::string& existing,uint64_t token)=0;
  virtual std::string keyboard_text()=0;
  virtual void hide_keyboard()=0;
  virtual void acknowledge_quit()=0;
};
class Overlay {
  std::unique_ptr<OverlayRuntime> sdk_;
  OverlayRuntime* runtime_;
  vr::VROverlayHandle_t handle_=0,thumbnail_=0;
  std::string keyboard_key_;
  struct ImageState {
    std::string signature,error;
    unsigned failures=0;
    std::chrono::steady_clock::time_point retry_after{};
  };
  ImageState main_image_,thumb_image_;
  uint64_t keyboard_token_=0;
  bool keyboard_opened_=false;
  uint64_t keyboard_closing_token_=0;
  std::chrono::steady_clock::time_point keyboard_close_deadline_{};
  std::vector<vr::VREvent_t> keyboard_deferred_clicks_;
public:
  Overlay();
  explicit Overlay(OverlayRuntime& runtime):runtime_(&runtime){}
  ~Overlay();
  Overlay(const Overlay&)=delete;
  Overlay& operator=(const Overlay&)=delete;
  void init();
  bool step(IpcClient& ipc,OwnedChild& child);
  bool step(Model& model,const std::string& backend,const std::function<bool(const Json&,bool)>& submit);
  Ui ui;
};
}
