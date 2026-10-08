#include "child.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace panel {
namespace {
void fail(const std::string& what) {throw std::runtime_error(what+": "+std::strerror(errno));}
void check_existing(const std::string& path) {
  sockaddr_un address{}; address.sun_family=AF_UNIX;
  if(path.size()>=sizeof(address.sun_path)) throw std::runtime_error("control socket path is too long");
  std::memcpy(address.sun_path,path.c_str(),path.size()+1);
  int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
  if(fd<0) fail("socket probe");
  int rc=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  int error=rc==0?0:errno;
  if(rc<0 && error==EINPROGRESS) {
    pollfd item{fd,POLLOUT,0};
    do {rc=::poll(&item,1,150);}while(rc<0 && errno==EINTR);
    socklen_t size=sizeof(error);
    if(rc>0 && getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0) {} else error=ETIMEDOUT;
  }
  close(fd);
  if(error==0) throw std::runtime_error("existing backend uses this control socket; refusing ownership");
  if(error!=ENOENT && error!=ECONNREFUSED) throw std::runtime_error("cannot safely probe control socket: "+std::string(std::strerror(error)));
}
std::string status_reason(int status) {
  if(WIFEXITED(status)) return "Backend stopped: exit "+std::to_string(WEXITSTATUS(status));
  if(WIFSIGNALED(status)) return "Backend stopped: signal "+std::to_string(WTERMSIG(status));
  return "Backend stopped";
}
}
OwnedChild::~OwnedChild(){stop();}
void OwnedChild::start(const std::string& backend,const std::string& path,const std::string& config) {
  if(running()) throw std::runtime_error("owned backend already started");
  check_existing(path);
  // All storage, argv, and signal structures are prepared before fork/VR_Init.
  std::vector<std::string> args{backend,"--control-socket",path,"--panel-mode"};
  if(!config.empty()) {args.push_back("--config"); args.push_back(config);}
  std::vector<char*> argv; for(auto& arg:args) argv.push_back(arg.data()); argv.push_back(nullptr);
  struct sigaction defaults{}; defaults.sa_handler=SIG_DFL; sigemptyset(&defaults.sa_mask);
  sigset_t empty; sigemptyset(&empty);
  const pid_t parent=getpid();
  int pipefd[2]; if(pipe2(pipefd,O_CLOEXEC)<0) fail("exec status pipe");
  const pid_t pid=fork();
  if(pid<0) {int error=errno; close(pipefd[0]);close(pipefd[1]);errno=error;fail("fork backend");}
  if(pid==0) {
    close(pipefd[0]);
    // Only async-signal-safe Linux operations here. Default/unblock first so
    // inherited panel handlers cannot consume the parent-death signal.
    int error=0;
    if(sigaction(SIGTERM,&defaults,nullptr)<0 || sigaction(SIGINT,&defaults,nullptr)<0 || sigprocmask(SIG_SETMASK,&empty,nullptr)<0) error=errno;
#ifdef PANEL_CHILD_TEST_HOOK
    // Test library only: deterministic gate before parent-death setup.
    if(!error && ready_fd_>=0 && release_fd_>=0) {
      const pid_t self=getpid();
      ssize_t n;do{n=write(ready_fd_,&self,sizeof(self));}while(n<0 && errno==EINTR);
      if(n!=sizeof(self))_exit(124);
      char release;do{n=read(release_fd_,&release,1);}while(n<0 && errno==EINTR);
      if(n!=1)_exit(124);
      close(ready_fd_);close(release_fd_);
    }
#endif
    if(!error && prctl(PR_SET_PDEATHSIG,SIGTERM)<0)error=errno;
    if(!error && getppid()!=parent) _exit(125);
    if(!error) execve(argv[0],argv.data(),environ);
    if(!error) error=errno;
    const char* bytes=reinterpret_cast<const char*>(&error); size_t left=sizeof(error);
    while(left) {ssize_t n=write(pipefd[1],bytes,left); if(n>0){bytes+=n;left-=static_cast<size_t>(n);}else if(errno!=EINTR) break;}
    _exit(127);
  }
  close(pipefd[1]); child_=pid; reason_="Backend starting";
  pollfd item{pipefd[0],POLLIN,0}; int rc;
  do{rc=::poll(&item,1,2000);}while(rc<0 && errno==EINTR);
  int error=0; ssize_t count=-1;
  if(rc>0) {do{count=read(pipefd[0],&error,sizeof(error));}while(count<0 && errno==EINTR);}
  close(pipefd[0]);
  if(rc<=0 || count!=0) {
    stop();
    throw std::runtime_error(count>0?"backend exec failed: "+std::string(std::strerror(error)):"backend exec handshake failed");
  }
  reason_="Backend running";
}
void OwnedChild::poll() {
  if(!running()) return;
  int status=0; pid_t rc;
  do{rc=waitpid(child_,&status,WNOHANG);}while(rc<0 && errno==EINTR);
  if(rc==child_) {child_=-1;reason_=status_reason(status);}
  else if(rc<0 && errno==ECHILD) {child_=-1;reason_="Backend ownership lost (already reaped)";}
}
void OwnedChild::stop() {
  poll(); if(!running()) return;
  // An unreaped owned child cannot have its PID reused. No other PID is signaled.
  kill(child_,SIGTERM);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(running() && std::chrono::steady_clock::now()<deadline) {poll(); if(running()) ::poll(nullptr,0,10);}
  if(!running()) return;
  kill(child_,SIGKILL);
  int status=0; pid_t rc;
  do{rc=waitpid(child_,&status,0);}while(rc<0 && errno==EINTR);
  child_=-1; reason_=rc>0?status_reason(status):"Backend stopped (wait unavailable)";
}
}
