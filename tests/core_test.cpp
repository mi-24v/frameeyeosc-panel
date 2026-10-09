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
  m.expect("w1", "settings.patch", lines[8]["params"]); m.receive(lines[9]);
  CHECK(m.status.find("invalid value for eye_widening_margin_left")!=std::string::npos); CHECK(m.state["settings"]["current"]["eyelid_output_cap"]==0.7);
  // Calibration updates must validate the complete contract and never cross instances.
  const Json cause={{"connection_id","p1"},{"request_id","c1"}};
  Json calibration={{"session_id","session1"},{"state","waiting"},{"stage","closed"},{"remaining_ms",0},{"caused_by",cause},{"result",nullptr}};
  m.expect("c1","calibration.start",Json::object());
  m.receive(Json{{"v",1},{"type","response"},{"id","c1"},{"ok",true},{"result",Json{{"calibration",calibration},{"save","not_requested"}}},{"error",nullptr}});
  CHECK(!m.pending());CHECK(m.state["calibration"]==calibration);
  auto event=Json{{"v",1},{"type","event"},{"event","calibration.changed"},{"data",Json{{"instance_id","i-fixture"},{"calibration",calibration}}},{"caused_by",cause}};
  event["data"]["calibration"]["state"]="preparing";event["data"]["calibration"]["remaining_ms"]=2000;
  m.receive(event);CHECK(m.state["calibration"]["state"]=="preparing");
  auto before=m.state;event["data"]["instance_id"]="other";m.receive(event);CHECK(m.state==before);
  event["data"]["instance_id"]="i-fixture";event["data"]["calibration"]["remaining_ms"]=2.5;m.receive(event);CHECK(m.state==before);
  auto stale_snapshot=lines[4];stale_snapshot["data"]["instance_id"]="other";m.receive(stale_snapshot);CHECK(m.state==before);
  auto invalid_snapshot=lines[1];invalid_snapshot["result"]["calibration"]=calibration;invalid_snapshot["result"]["calibration"]["caused_by"]="wrong";
  Model invalid;invalid.expect("s1","state.get",Json::object());invalid.receive(invalid_snapshot);CHECK(!invalid.fresh);CHECK(invalid.pending());
  invalid_snapshot=lines[1];invalid_snapshot["result"]["settings"]["current"]["eye_widening"]=1;invalid.receive(invalid_snapshot);CHECK(!invalid.fresh);
  // Every required field is validated before adopting a replacement snapshot.
  for(const auto& entry:std::vector<std::pair<std::string,Json>>{{"/settings/current/eye_widening",1},{"/settings/current/eye_widening_margin_left",-0.1},{"/settings/current/min_base_width",1.1},{"/calibration",Json{{"state","waiting"}}},{"/active_profile",Json::object()}}) {
    Model rejected;rejected.expect("s1","state.get",Json::object());auto bad=lines[1];bad["result"][Json::json_pointer(entry.first)]=entry.second;
    rejected.receive(bad);CHECK(!rejected.fresh);CHECK(rejected.pending());
  }
  event["data"]["calibration"]=calibration;
  for(const auto& entry:std::vector<std::pair<std::string,Json>>{{"state","unknown"},{"session_id",""},{"stage","natural"},{"remaining_ms",-1},{"remaining_ms",true},{"caused_by",Json::object()},{"result",Json::object()}}) {
    auto malformed=event;malformed["data"]["calibration"][entry.first]=entry.second;m.receive(malformed);CHECK(m.state==before);
  }
  for(const auto& entry:std::vector<std::pair<std::string,int>>{{"preparing",2000},{"measuring",4250}}) {
    event["data"]["calibration"]=calibration;event["data"]["calibration"]["state"]=entry.first;event["data"]["calibration"]["remaining_ms"]=entry.second;
    m.receive(event);CHECK(m.state["calibration"]["remaining_ms"]==entry.second);CHECK(m.state["active_profile"].is_null());
  }
  auto failed_stage=calibration;failed_stage["stage"]="relaxed";failed_stage["result"]=Json{{"kind","stage_failed"},{"stage","relaxed"},{"reason","invalid_interval"},{"wide_available",nullptr},{"caused_by",cause}};
  event["data"]["calibration"]=failed_stage;m.receive(event);CHECK(m.calibration_allowed("calibration.begin_stage"));
  auto profile_unchanged=m.state["active_profile"];
  for(const std::string terminal:{"completed","cancelled","failed"}) {
    auto finished=calibration;finished["state"]=terminal;finished["stage"]=nullptr;
    finished["result"]=Json{{"kind",terminal},{"stage",nullptr},{"reason",terminal=="failed"?Json("save_failed"):Json()},{"wide_available",Json::array({false,true})},{"caused_by",cause}};
    event["data"]["calibration"]=finished;m.receive(event);CHECK(m.state["calibration"]==finished);CHECK(m.state["active_profile"]==profile_unchanged);CHECK(!m.calibration_allowed("calibration.cancel"));
    CHECK(m.calibration_allowed("calibration.start"));
  }
  auto terminal_state=m.state;auto missing_cause=event;missing_cause.erase("caused_by");m.receive(missing_cause);CHECK(m.state==terminal_state);
  m.disconnected("lost");m.receive(event);CHECK(!m.fresh);CHECK(m.state==terminal_state);
  auto reconnected=lines[1];reconnected["result"]["calibration"]=terminal_state["calibration"];m.expect("s1","state.get",Json::object());m.receive(reconnected);CHECK(m.fresh);CHECK(m.state["calibration"]["state"]=="failed");
  m.receive(lines[4]);
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
  for(size_t row=0;row<Ui::fields.size();++row) {
    const float top=Ui::rows_top+row*Ui::row_height;const auto before=ui.patch(m);
    for(const auto& point:std::vector<std::pair<float,float>>{{15,top+5},{Ui::width-16,top+5},{30,top+Ui::row_height-3},{30,top+Ui::row_height-1}}) {
      CHECK(ui.click(point.first,point.second,m).empty());CHECK(ui.patch(m)==before);
    }
  }
  CHECK(ui.click(16,Ui::rows_top,m)=="eyelid_output_cap");
  CHECK(ui.click(Ui::width-17,Ui::rows_top+Ui::row_height-4,m)=="eyelid_output_cap");
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
  Ui calibration_ui;
  CHECK(calibration_ui.click(1100,35,m)=="page");
  for(size_t row=0;row<Ui::calibration_fields.size();++row) {
    const float top=Ui::rows_top+row*Ui::row_height;const auto previous=calibration_ui.patch(m);
    for(const auto& point:std::vector<std::pair<float,float>>{{15,top+5},{Ui::width-16,top+5},{30,top+Ui::row_height-3},{30,top+Ui::row_height-1}}) {
      CHECK(calibration_ui.click(point.first,point.second,m).empty());CHECK(calibration_ui.patch(m)==previous);
    }
  }
  for(const auto& point:std::vector<std::pair<float,float>>{{15,890},{300,890},{319,890},{640,890},{659,890},{950,890},{969,890},{1424,890},{30,874},{30,925}})CHECK(calibration_ui.click(point.first,point.second,m).empty());
  CHECK(calibration_ui.click(30,Ui::rows_top+5,m)=="changed");
  CHECK(calibration_ui.patch(m)["eye_widening"]==false);
  CHECK(calibration_ui.edit("eye_widening_margin_left","0.2",m));
  CHECK(calibration_ui.edit("eye_widening_margin_right","0",m));
  CHECK(!calibration_ui.edit("eye_widening_margin_left","-1",m));
  CHECK(!calibration_ui.edit("min_base_width","0",m));
  CHECK(!calibration_ui.edit("min_wide_width","1.01",m));
  CHECK(calibration_ui.edit("min_wide_width","1",m));
  CHECK(calibration_ui.click(30,890,m)=="calibration.start");
  m.state["calibration"]=calibration;
  CHECK(calibration_ui.click(350,890,m)=="calibration.begin_stage");
  CHECK(calibration_ui.click(700,890,m).empty());
  CHECK(calibration_ui.click(1000,890,m)=="calibration.cancel");
  m.state["calibration"]["stage"]="wide";
  CHECK(calibration_ui.click(700,890,m)=="calibration.skip_wide");
  m.state["calibration"]["state"]="measuring";m.state["calibration"]["remaining_ms"]=5000;
  CHECK(calibration_ui.click(350,890,m).empty());
  m.expect("pending","calibration.begin_stage",Json{{"session_id","session1"},{"stage","wide"}});
  CHECK(calibration_ui.click(1000,890,m)=="calibration.cancel");
  CHECK(calibration_ui.click(350,890,m).empty());
  CHECK((m.calibration_params("calibration.begin_stage")==Json{{"session_id","session1"},{"stage","wide"}}));
  m.expect("cancelling","calibration.cancel",Json{{"session_id","session1"}});
  CHECK(calibration_ui.click(1000,890,m).empty());
  m.state["calibration"]["state"]="completed";m.state["calibration"]["stage"]=nullptr;m.state["calibration"]["remaining_ms"]=0;
  m.expect("newstart","calibration.start",Json::object());
  CHECK(m.calibration_allowed("calibration.cancel"));
  CHECK(m.calibration_params("calibration.cancel").empty());
  auto calibration_pixels=calibration_ui.render(m,"running");
  m.disconnected("lost");CHECK(calibration_ui.click(1000,890,m).empty());
  CHECK(!calibration_ui.edit("eye_widening_margin_left","0.3",m));
  CHECK(calibration_ui.click(1000,35,m)=="page");
  CHECK(calibration_ui.render(m,"running")!=calibration_pixels);
  m.expect("s1","state.get",Json::object());m.receive(lines[1]);
  calibration_ui.calibration_page=true;
  auto no_profile=calibration_ui.render(m,"running");m.receive(lines.back());
  CHECK(!m.state["active_profile"].is_null());CHECK(calibration_ui.render(m,"running")!=no_profile);
  auto applied_profile=m.state["active_profile"];
  auto precise=m;precise.state["active_profile"]["left"]["d"]=0.15000000000000002;
  CHECK(calibration_ui.render(precise,"running")==calibration_ui.render(m,"running"));
  m.expect("savefailure","calibration.skip_wide",Json{{"session_id","session1"}});
  auto failed=calibration;failed["state"]="failed";failed["stage"]=nullptr;
  failed["result"]=Json{{"kind","failed"},{"stage",nullptr},{"reason","save_failed"},{"wide_available",nullptr},{"caused_by",cause}};
  m.receive(Json{{"v",1},{"type","response"},{"id","savefailure"},{"ok",false},{"result",Json{{"calibration",failed},{"save","failed"}}},{"error",Json{{"code","save_failed"},{"message","disk full"}}}});
  CHECK(!m.pending());CHECK(m.state["active_profile"]==applied_profile);CHECK(m.status.find("disk full")!=std::string::npos);
  auto failed_pixels=calibration_ui.render(m,"running");m.state["calibration"]["state"]="waiting";m.state["calibration"]["stage"]="relaxed";m.state["calibration"]["result"]["kind"]="stage_failed";m.state["calibration"]["result"]["stage"]="relaxed";m.state["calibration"]["result"]["reason"]="invalid_interval";
  CHECK(calibration_ui.render(m,"running")!=failed_pixels);CHECK(m.calibration_allowed("calibration.begin_stage"));
  m.expect("s1","state.get",Json::object());m.receive(lines[1]);
  auto pixels=ui.render(m,"running"); CHECK(pixels.size()==Ui::width*Ui::height*4);
  auto long_model=m;long_model.state["settings"]["current"]["prefix"]="/"+std::string(100,'x');
  auto clipped=ui.render(long_model,"running");
  CHECK(clipped[((Ui::rows_top+Ui::row_height+10)*Ui::width+425+30*12)*4]==220);
  std::cout<<"core contracts passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; } }
