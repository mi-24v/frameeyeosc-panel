#pragma once
#include "ipc_client.hpp"
#include <openvr.h>
#include <map>
#include <memory>
#include <functional>
#include <vector>
namespace panel {
enum class EventOrigin {System, OwnedOverlay};
bool ends_session(uint32_t event,EventOrigin origin,vr::VROverlayHandle_t event_handle,vr::VROverlayHandle_t own_handle);
class Ui {
  std::map<std::string,Json> draft_;
public:
  static constexpr int width=1440,height=1080,rows_top=130,row_height=65;
  static const std::vector<std::string> fields;
  bool save=false;
  std::string error;
  bool edit(const std::string& key,const std::string& text,const Model& model);
  Json patch(const Model& model) const;
  std::string value(const std::string& key,const Model& model) const;
  std::string click(float x,float y,const Model& model);
  void accepted(){draft_.clear();}
  std::vector<unsigned char> render(const Model& model,const std::string& backend) const;
};
class OverlayRuntime {
public:
  virtual ~OverlayRuntime()=default;
  virtual void init(vr::VROverlayHandle_t& main,vr::VROverlayHandle_t& thumb)=0;
  virtual void shutdown(vr::VROverlayHandle_t main,vr::VROverlayHandle_t thumb)=0;
  virtual bool poll(EventOrigin origin,vr::VROverlayHandle_t handle,vr::VREvent_t& event)=0;
  virtual bool visible(vr::VROverlayHandle_t handle)=0;
  virtual void raw(vr::VROverlayHandle_t handle,const std::vector<unsigned char>& bytes)=0;
  virtual std::string keyboard(vr::VROverlayHandle_t handle,const std::string& key,const std::string& existing,uint64_t token)=0;
  virtual std::string keyboard_text()=0;
  virtual void hide_keyboard()=0;
  virtual void acknowledge_quit()=0;
};
class Overlay {
  std::unique_ptr<OverlayRuntime> sdk_;
  OverlayRuntime* runtime_;
  vr::VROverlayHandle_t handle_=0,thumbnail_=0;
  std::string keyboard_key_,last_main_,last_thumb_;
  uint64_t keyboard_token_=0;
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
