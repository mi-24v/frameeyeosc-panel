#pragma once
#include "child.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
namespace panel {
using Json=nlohmann::json;
class Model {
  std::string id_, method_, accepted_start_session_, previous_start_session_;
  Json params_;
public:
  Json state=Json::object();
  bool fresh=false;
  bool reconnect_requested=false;
  bool calibration_allowed(const std::string& method) const;
  Json calibration_params(const std::string& method) const;
  std::string status="Waiting for owned backend";
  void expect(const std::string& id,const std::string& method,const Json& params);
  void receive(const Json& message);
  void disconnected(const std::string& reason);
  bool pending() const {return !id_.empty();}
  const std::string& accepted_start_session() const {return accepted_start_session_;}
};
class IpcClient {
  const std::string path_;
  OwnedChild& child_;
  int fd_=-1;
  bool connecting_=false,foreign_=false,cancel_queued_=false;
  std::string cancel_session_;
  std::string input_,output_;
  size_t offset_=0;
  unsigned connections_=0,sequence_=0;
  std::chrono::steady_clock::time_point retry_{},response_deadline_{};
  void drop(const std::string& reason);
  bool connected();
  void request(const std::string& method,const Json& params);
public:
  Model model;
  IpcClient(std::string path,OwnedChild& child):path_(std::move(path)),child_(child){}
  ~IpcClient();
  void tick();
  bool patch(const Json& patch,bool save);
  bool calibrate(const std::string& method);
  bool ownership_failed() const {return foreign_;}
  unsigned connections() const {return connections_;}
};
}
