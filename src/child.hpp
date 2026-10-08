#pragma once
#include <string>
#include <sys/types.h>
namespace panel {
class OwnedChild {
  pid_t child_=-1;
#ifdef PANEL_CHILD_TEST_HOOK
  int ready_fd_=-1,release_fd_=-1;
#endif
  std::string reason_="Backend not started";
public:
  OwnedChild()=default;
#ifdef PANEL_CHILD_TEST_HOOK
  void set_test_gate(int ready,int release){ready_fd_=ready;release_fd_=release;}
#endif
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
