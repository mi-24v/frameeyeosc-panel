#include "../src/overlay.hpp"
#include "../src/ipc_client.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using panel::Json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int main() { try {
  using namespace panel;
  CHECK(!ends_session(vr::VREvent_DashboardDeactivated, EventOrigin::System, 1, 1));
  CHECK(!ends_session(vr::VREvent_ProcessQuit, EventOrigin::System, 1, 1));
  CHECK(ends_session(vr::VREvent_Quit, EventOrigin::System, 1, 1));
  CHECK(ends_session(vr::VREvent_OverlayClosed, EventOrigin::OwnedOverlay, 1, 1));
  CHECK(!ends_session(vr::VREvent_OverlayClosed, EventOrigin::System, 2, 1));
  std::ifstream f(FIXTURE_PATH); std::vector<Json> lines; std::string s;
  while (std::getline(f,s)) lines.push_back(Json::parse(s));
  Model m; m.expect("s1", "state.get", Json::object()); m.receive(lines[1]);
  CHECK(m.fresh); CHECK(m.state == lines[1]["result"]);
  m.expect("p1", "settings.patch", lines[2]["params"]);
  CHECK(m.pending()); CHECK(m.state["settings"]["current"]["eyelid_output_cap"]==1.0);
  m.receive(lines[3]); CHECK(!m.pending()); CHECK(m.state["settings"]["current"]["eyelid_output_cap"]==0.7);
  m.receive(lines[4]); CHECK(m.state==lines[4]["data"]);
  m.expect("w1", "settings.patch", lines[7]["params"]); m.receive(lines[8]);
  CHECK(m.status.find("unknown setting")!=std::string::npos); CHECK(m.state["settings"]["current"]["eyelid_output_cap"]==0.7);
  auto saved=m.state; m.receive(Json{{"v",2},{"type","event"},{"event","state.changed"},{"data",Json::object()}}); CHECK(m.state==saved);
  m.disconnected("lost"); CHECK(!m.fresh); CHECK(m.state==saved); CHECK(!m.pending());
  m.expect("s1", "state.get", Json::object()); m.receive(lines[1]);
  Ui ui; ui.edit("eyelid_output_cap","0.6",m); CHECK(ui.patch(m)["eyelid_output_cap"]==0.6);
  ui.edit("target_port","9001",m); CHECK(ui.patch(m)["target_port"].is_number_integer());
  CHECK(!ui.edit("target_port","1.5",m)); CHECK(!ui.edit("eyelid_output_cap","nan",m));
  CHECK(!ui.edit("one_euro_min_cutoff","0",m));
  ui.edit("prefix","/New",m); CHECK(ui.patch(m)["prefix"]=="/New");
  m.receive(lines[4]); CHECK(ui.patch(m)["prefix"]=="/New");
  CHECK(ui.click(20,Ui::rows_top+5,m)=="eyelid_output_cap");
  CHECK(ui.click(20,Ui::height-20,m)=="close");
  m.expect("limit", "settings.patch", Json{{"patch",Json{{"prefix","/Later"}}},{"save",false}});
  m.receive(Json{{"v",1},{"type","response"},{"id","limit"},{"ok",false},{"result",Json::object()},{"error",Json{{"code","request_limit"},{"message","reconnect"}}}});
  CHECK(!m.pending()); CHECK(m.status.find("request_limit")!=std::string::npos);
  CHECK(m.state["settings"]["current"]["prefix"]=="/FT");
  auto pixels=ui.render(m,"running"); CHECK(pixels.size()==Ui::width*Ui::height*4);
  auto long_model=m;long_model.state["settings"]["current"]["prefix"]="/"+std::string(100,'x');
  auto clipped=ui.render(long_model,"running");
  CHECK(clipped[((Ui::rows_top+Ui::row_height+10)*Ui::width+425+30*12)*4]==220);
  std::cout<<"core contracts passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; } }
