#pragma once
#include <string>
class GfxRenderer {
 public:
 int w=528,h=792;
 struct Center {int x,y;};
 mutable Center circles[256]{};mutable int circleCount=0;
 struct Text {int x,y,w;char value[256];};
 mutable Text textRuns[512]{};mutable int textCount=0;
 mutable int clears=0,texts=0,strokes=0,topBars=0,navBars=0;
 int getScreenWidth()const{return w;}
 int getScreenHeight()const{return h;}
 void clearScreen()const{++clears;}
 void fillRect(int,int,int,int,bool=true)const{++strokes;}
};
class MappedInputManager {};
