#pragma once
#include <string>
#include <sys/types.h>
namespace panel {
class OwnedChild {
  pid_t child_=-1;
  std::string reason_="Backend not started";
public:
  OwnedChild()=default;
  ~OwnedChild();
  OwnedChild(const OwnedChild&)=delete;
  OwnedChild& operator=(const OwnedChild&)=delete;
  void start(const std::string& backend,const std::string& socket,const std::string& config);
  void poll();
  void stop();
  bool running() const {return child_>0;}
  pid_t pid() const {return child_;}
  const std::string& reason() const {return reason_;}
};
}
