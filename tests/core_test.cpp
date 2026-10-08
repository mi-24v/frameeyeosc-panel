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
  const auto draft_before=ui.patch(m);const bool save_before=ui.save;
  for(const auto& point:std::vector<std::pair<float,float>>{{0,Ui::height-20},{15,Ui::height-20},{244,Ui::height-20},{249,Ui::height-20},{250,Ui::height-20},{259,Ui::height-20},{580,Ui::height-20},{589,Ui::height-20},{590,Ui::height-20},{594,Ui::height-20},{935,Ui::height-20},{949,Ui::height-20},{100,Ui::height-10},{100,Ui::height-1},{300,Ui::height-10},{650,Ui::height-1}}) {
    CHECK(ui.click(point.first,point.second,m).empty());CHECK(ui.save==save_before);CHECK(ui.patch(m)==draft_before);
  }
  CHECK(ui.click(260,Ui::height-55,m)=="save");CHECK(ui.click(595,Ui::height-11,m)=="apply");
  m.expect("limit", "settings.patch", Json{{"patch",Json{{"prefix","/Later"}}},{"save",false}});
  m.receive(Json{{"v",1},{"type","response"},{"id","limit"},{"ok",false},{"result",Json::object()},{"error",Json{{"code","request_limit"},{"message","reconnect"}}}});
  CHECK(!m.pending()); CHECK(m.status.find("request_limit")!=std::string::npos);
  CHECK(m.state["settings"]["current"]["prefix"]=="/FT");
  Ui save_ui;Model save_model=m;
  CHECK(save_ui.edit("eyelid_output_cap","0.6",save_model));
  save_model.state["settings"]["current"]["eyelid_output_cap"]=0.6; // confirmed save:false result
  CHECK(save_ui.patch(save_model).empty());
  save_ui.save=true;CHECK((save_ui.patch(save_model)==Json{{"eyelid_output_cap",0.6}}));
  for(const auto& point:std::vector<std::pair<float,float>>{{950,Ui::height-20},{959,Ui::height-20},{1424,Ui::height-20},{1439,Ui::height-20},{1050,Ui::height-10},{1050,Ui::height-1}}) {
    CHECK(save_ui.click(point.first,point.second,save_model).empty());
    CHECK((save_ui.patch(save_model)==Json{{"eyelid_output_cap",0.6}}));
  }
  save_ui.save=false;
  CHECK(save_ui.click(1050,Ui::height-20,save_model)=="discard");
  CHECK(save_model.state["settings"]["current"]["eyelid_output_cap"]==0.6);
  save_ui.save=true;
  CHECK(save_ui.edit("prefix","/OnlyPrefix",save_model));
  CHECK((save_ui.patch(save_model)==Json{{"prefix","/OnlyPrefix"}}));
  Ui cli_ui;CHECK(cli_ui.edit("prefix","/FT",save_model));cli_ui.save=true;
  CHECK((cli_ui.patch(save_model)==Json{{"prefix","/FT"}}));
  auto pixels=ui.render(m,"running"); CHECK(pixels.size()==Ui::width*Ui::height*4);
  auto long_model=m;long_model.state["settings"]["current"]["prefix"]="/"+std::string(100,'x');
  auto clipped=ui.render(long_model,"running");
  CHECK(clipped[((Ui::rows_top+Ui::row_height+10)*Ui::width+425+30*12)*4]==220);
  std::cout<<"core contracts passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; } }
