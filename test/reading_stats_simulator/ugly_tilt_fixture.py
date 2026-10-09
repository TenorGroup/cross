"""Opt-in menu HAL event injection in a local simulator dependency, for regression tests."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--hal', type=Path, required=True)
args = parser.parse_args()
source = args.hal.read_text()
if 'class UglyTiltFixtureBase' in source:
    raise SystemExit(0)
source = source.replace('class HalTiltSensor;\nextern HalTiltSensor halTiltSensor;', '')
source = source.replace('class HalTiltSensor {', 'class UglyTiltFixtureBase {')
source += r'''
#include <Arduino.h>
#include <sstream>
#include <utility>
#include <vector>
class HalTiltSensor : public UglyTiltFixtureBase {
  std::vector<std::pair<unsigned long, std::string>> events;
  size_t cursor=0;
  uint8_t tabMode=0,rowMode=0;
  bool tabActive=false,rowActive=false,activity=false;
  bool pending[4]={};
  bool take(int index,uint8_t mode,bool active) {
    const bool seen=pending[index]; pending[index]=false; return seen && mode!=0 && active;
  }
 public:
  HalTiltSensor() {
    const char* spec=std::getenv("CROSSPOINT_SIM_MENU_TILT");
    if (!spec) return;
    std::istringstream input(spec); std::string item;
    while (std::getline(input,item,';')) {
      const auto colon=item.find(':');
      if (colon!=std::string::npos) events.emplace_back(std::stoul(item.substr(0,colon)),item.substr(colon+1));
    }
  }
  void update(uint8_t mode,uint8_t orientation,bool active) {
    UglyTiltFixtureBase::update(mode,orientation,active); tabMode=mode; tabActive=active;
    while (cursor<events.size() && events[cursor].first<=millis()) {
      const auto& name=events[cursor++].second;
      if (name=="FORWARD") pending[0]=true;
      if (name=="BACK") pending[1]=true;
      if (name=="UP") pending[2]=true;
      if (name=="DOWN") pending[3]=true;
      activity=true;
    }
  }
  void configureVerticalGesture(uint8_t mode,bool active) { rowMode=mode; rowActive=active; }
  bool wasTiltedForward() { return take(tabMode==2?1:0,tabMode,tabActive); }
  bool wasTiltedBack() { return take(tabMode==2?0:1,tabMode,tabActive); }
  bool wasTiltedUp() { return take(rowMode==2?3:2,rowMode,rowActive); }
  bool wasTiltedDown() { return take(rowMode==2?2:3,rowMode,rowActive); }
  bool hadActivity() { const bool seen=activity; activity=false; return seen; }
  void clearPendingEvents() { for (bool& event:pending) event=false; }
};
extern HalTiltSensor halTiltSensor;
'''
args.hal.write_text(source)
