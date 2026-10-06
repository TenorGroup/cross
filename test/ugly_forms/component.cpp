#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <I18n.h>
#include "shells/ugly/UglyQuestionSheet.h"
#include "shells/ugly/UglyInk.h"
int underlines=0,rowCircles=0;
namespace ugly {
int width(const GfxRenderer&,Size size,const char* s){int n=0;for(const unsigned char* p=(const unsigned char*)s;*p;++p)if((*p&0xc0)!=0x80)++n;return n*(size==Size::S22?9:13);}
int text(const GfxRenderer& r,Size size,int x,int y,const char* s,bool){++r.texts;const int w=width(r,size,s);if(r.textCount<512){auto& t=r.textRuns[r.textCount++];t.x=x;t.y=y;t.w=w;snprintf(t.value,sizeof(t.value),"%s",s);}return w;}
int paragraph(const GfxRenderer& r,Size size,int,int,int w,int,const char* s,bool draw){int lines=std::max(1,(width(r,size,s)+w-1)/w);if(draw)r.texts+=lines;return lines;}
std::string fit(const GfxRenderer& r,Size size,const std::string& s,int w){std::string out=s;while(!out.empty()&&width(r,size,out.c_str())>w)out.pop_back();return out;}
void line(const GfxRenderer& r,int,int,int,int,uint32_t,int){++r.strokes;}
void circle(const GfxRenderer& r,Circle kind,const Box& b,int,int,int){++r.strokes;if(kind==Circle::Row)++rowCircles;if(kind==Circle::Word&&r.circleCount<256)r.circles[r.circleCount++]={(b.x0+b.x1)/2,(b.y0+b.y1)/2};}
void underline(const GfxRenderer& r,int,int,int,uint32_t,int){++r.strokes;++underlines;}
void statusBar(const GfxRenderer& r,const MappedInputManager&,Hints){++r.navBars;}
void topBar(const GfxRenderer& r,const char*){++r.topBars;}
void formTopBar(const GfxRenderer& r){++r.topBars;}
void arrow(const GfxRenderer& r,int,int,bool,int){++r.strokes;}
#include "NavRow.inc"
}
using Sheet=ugly::QuestionSheet;
struct Catalog {
 std::vector<Sheet::Row> rows;
 int labels=0,reads=0;
 static Sheet::Row row(void* context,int index){auto& c=*static_cast<Catalog*>(context);++c.reads;return c.rows[index];}
 static void label(void* context,int,int option,char* out,size_t size){++static_cast<Catalog*>(context)->labels;snprintf(out,size,"Option %d",option);}
 Sheet::View view(){return {this,static_cast<int>(rows.size()),"Display",20261005,7,row,label};}
};
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}}
#ifndef QUEUE_HARNESS
int main(){

#if FREEINK_DEVICE_X4PRO
 for(int profile:{1,2}) {
#else
 for(int profile:{0}) {
#endif
  const bool touch=profile!=0;
  GfxRenderer r;r.w=profile==2?800:touch?480:528;r.h=profile==2?480:touch?800:792;MappedInputManager input;
  Catalog c;c.rows={{11,"Size",Sheet::Kind::Choice,0,3},{12,"Toggle",Sheet::Kind::Toggle,0,2},{13,"Range",Sheet::Kind::Ruler,0,7},{14,"Fonts",Sheet::Kind::Paper,0,30},{15,"Action",Sheet::Kind::Action,0,0},{16,"Read only",Sheet::Kind::ReadOnly,0,0}};
  Sheet sheet;sheet.bind(r,c.view(),touch);sheet.paint(r,input);
  check(r.clears==1&&r.texts>0&&r.strokes>0,"production paint invoked");
  check((touch || r.navBars==1)&&r.topBars==(touch?1:0),"status and input hints preserved");
  if(touch) {
   bool found=false;
   for(int i=0;i<r.textCount;++i)if(std::string(r.textRuns[i].value)=="back to Settings") {
    const auto& back=r.textRuns[i];found=true;
    check(back.y>=r.h-64&&back.y<r.h&&back.x>=r.w/3&&back.x+back.w<=r.w*2/3,"drawn footer matches logical middle cell");
    check(sheet.tap(back.x+back.w/2,back.y-8).kind==Sheet::IntentKind::Back,"drawn Back label has Back hit");
   }
   check(found,"actual drawn Back label present");
  }
  if(touch) {
   const auto preview=sheet.input(Sheet::Key::NextOption);
   check(preview.kind==Sheet::IntentKind::Preview&&preview.id==11&&preview.candidate==1&&c.rows[0].selected==0,"candidate distinct from committed");
   sheet.setQuip(0,"Candidate quip");sheet.paint(r,input);
   check(c.rows[0].selected==0&&sheet.candidate()==1,"paint no commit");
   auto commit=sheet.input(Sheet::Key::Confirm);check(commit.kind==Sheet::IntentKind::Commit&&commit.row==0&&commit.candidate==1,"confirm emits commit intent");
   c.rows[0].selected=1;sheet.didCommit(0,0);sheet.paint(r,input);
   check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::None,"already committed harmless");
   sheet.input(Sheet::Key::NextQuestion);commit=sheet.input(Sheet::Key::Confirm);check(commit.kind==Sheet::IntentKind::Commit&&commit.id==12&&commit.candidate==1&&c.rows[1].selected==0,"toggle commit intent only");
   sheet.input(Sheet::Key::NextQuestion);sheet.input(Sheet::Key::PreviousOption);check(sheet.candidate()==6&&c.rows[2].selected==0,"numeric candidate wraps count");
   sheet.input(Sheet::Key::NextQuestion);sheet.input(Sheet::Key::Confirm);check(sheet.paperOpen(),"long list opens paper");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.candidate()==1&&c.rows[3].selected==0,"paper navigation consumes question input");
   const auto cancel=sheet.input(Sheet::Key::Back);check(!sheet.paperOpen()&&sheet.candidate()==0&&cancel.kind==Sheet::IntentKind::None,"paper back cancels candidate");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::Activate,"action intent");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::None,"readonly inert");
   check(sheet.input(Sheet::Key::Home).kind==Sheet::IntentKind::Back,"home parent intent");
  } else {
   // Buttons (X3, X4): front up/down walk the questions, Select enters one, then every direction moves the circle.
   auto has=[&](const char* needle){for(int i=0;i<r.textCount;++i)if(strstr(r.textRuns[i].value,needle))return true;return false;};
   underlines=rowCircles=0;r.textCount=0;sheet.paint(r,input);
   check(underlines>=1&&rowCircles==0,"question mode underlines the question, no answer circle");
   check(has("select to answer"),"question mode hint names Select");
   auto k=sheet.input(Sheet::Key::NextOption);
   check(k.kind!=Sheet::IntentKind::Preview&&sheet.question()==0&&sheet.candidate()==0,"edge key in question mode changes no answer");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.question()==1,"front down walks to the next question");
   sheet.input(Sheet::Key::PreviousQuestion);check(sheet.question()==0,"front up walks back");
   k=sheet.input(Sheet::Key::Confirm);
   check(k.kind==Sheet::IntentKind::None&&k.repaint&&sheet.candidate()==0&&c.rows[0].selected==0,"select enters the question without a commit");
   underlines=rowCircles=0;r.textCount=0;sheet.paint(r,input);
   check(rowCircles==1&&underlines==0,"inside the question the circle sits on the chosen answer");
   check(has("again to finish"),"inside hint names finishing");
   k=sheet.input(Sheet::Key::NextQuestion);
   check(k.kind==Sheet::IntentKind::Preview&&sheet.question()==0&&sheet.candidate()==1,"front down moves the circle inside the question");
   sheet.input(Sheet::Key::NextOption);check(sheet.question()==0&&sheet.candidate()==2,"edge down moves the circle too");
   sheet.input(Sheet::Key::PreviousOption);check(sheet.candidate()==1,"edge up moves it back");
   k=sheet.input(Sheet::Key::Back);
   check(k.kind==Sheet::IntentKind::None&&sheet.candidate()==0&&c.rows[0].selected==0,"back cancels the unsaved circle");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.question()==1,"after back the front keys walk questions again");
   sheet.input(Sheet::Key::PreviousQuestion);sheet.input(Sheet::Key::Confirm);sheet.input(Sheet::Key::NextQuestion);
   auto commit=sheet.input(Sheet::Key::Confirm);
   check(commit.kind==Sheet::IntentKind::Commit&&commit.row==0&&commit.candidate==1,"select on another answer commits it");
   c.rows[0].selected=1;sheet.didCommit(0,0);
   sheet.input(Sheet::Key::NextQuestion);check(sheet.question()==0&&sheet.candidate()==2,"after a commit the question stays open");
   sheet.input(Sheet::Key::PreviousQuestion);
   k=sheet.input(Sheet::Key::Confirm);
   check(k.kind==Sheet::IntentKind::None&&sheet.candidate()==1,"select on the chosen answer leaves the question");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.question()==1,"left the question: front down walks on");
   r.circleCount=0;sheet.paint(r,input);
   sheet.input(Sheet::Key::Confirm);k=sheet.input(Sheet::Key::NextQuestion);
   check(sheet.question()==1&&sheet.candidate()==1&&k.kind==Sheet::IntentKind::Preview,"a switch is answered like any question");
   commit=sheet.input(Sheet::Key::Confirm);
   check(commit.kind==Sheet::IntentKind::Commit&&commit.id==12&&commit.candidate==1&&c.rows[1].selected==0,"switch commit intent only");
   sheet.input(Sheet::Key::Back);
   sheet.input(Sheet::Key::NextQuestion);sheet.input(Sheet::Key::Confirm);sheet.input(Sheet::Key::PreviousQuestion);
   check(sheet.question()==2&&sheet.candidate()==6&&c.rows[2].selected==0,"ruler circle wraps count");
   sheet.input(Sheet::Key::Back);
   sheet.input(Sheet::Key::NextQuestion);sheet.input(Sheet::Key::Confirm);check(sheet.paperOpen(),"long list opens its paper on select");
   sheet.input(Sheet::Key::NextOption);check(sheet.candidate()==1&&c.rows[3].selected==0,"edge key moves the circle on the paper");
   commit=sheet.input(Sheet::Key::Confirm);check(commit.kind==Sheet::IntentKind::Commit&&commit.candidate==1&&sheet.paperOpen(),"paper commit keeps the paper");
   const auto cancel=sheet.input(Sheet::Key::Back);check(!sheet.paperOpen()&&sheet.candidate()==0&&cancel.kind==Sheet::IntentKind::None,"paper back cancels candidate");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::Activate,"action intent");
   sheet.input(Sheet::Key::NextQuestion);check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::None,"readonly inert");
   check(sheet.input(Sheet::Key::Back).kind==Sheet::IntentKind::Back,"back in question mode leaves the sheet");
   check(sheet.input(Sheet::Key::Home).kind==Sheet::IntentKind::Back,"home parent intent");
   Catalog sw;sw.rows={{21,"Switch",Sheet::Kind::Toggle,1,2}};
   sheet.bind(r,sw.view(),false);r.circleCount=0;sheet.paint(r,input);
   check(r.circleCount==2,"a switch shows its 2 answers as circles");
  }
  if(touch) {
   Catalog one;one.rows={{100,"Choice",Sheet::Kind::Choice,0,3}};
   sheet.bind(r,one.view(),true);r.circleCount=0;sheet.paint(r,input);
   check(r.circleCount==3,"paint exposes actual answer circles");
   check(sheet.tap(100,20).kind==Sheet::IntentKind::None&&sheet.tap(r.w-1,200).kind==Sheet::IntentKind::None,"header and comment margin inert");
   auto selected=sheet.tap(r.circles[0].x,r.circles[0].y);
   check(selected.kind==Sheet::IntentKind::None,"tap committed answer harmless");
   selected=sheet.tap(r.circles[1].x,r.circles[1].y);
   check(selected.kind==Sheet::IntentKind::Commit&&selected.candidate==1&&one.rows[0].selected==0,"paint geometry tap direct commit intent");
   one.rows[0].kind=Sheet::Kind::Paper;one.rows[0].count=30;sheet.bind(r,one.view(),true);sheet.input(Sheet::Key::Confirm);
   sheet.input(Sheet::Key::NextQuestion);sheet.paint(r,input);check(sheet.tap(r.w-1,200).kind==Sheet::IntentKind::None&&!sheet.paperOpen()&&sheet.candidate()==0,"paper outside tap cancels without apply");
   sheet.input(Sheet::Key::Confirm);r.circleCount=0;sheet.paint(r,input);
   check(r.circleCount>1,"paper actual painted choices");
   const auto header=sheet.tap(50,r.circles[0].y-40);
   check(header.kind==Sheet::IntentKind::None&&sheet.paperOpen()&&sheet.candidate()==0,"paper header inert");
   selected=sheet.tap(r.circles[1].x,r.circles[1].y);
   check(selected.kind==Sheet::IntentKind::Commit&&selected.candidate==1&&!sheet.paperOpen(),"paper painted option direct commit");
  }
  sheet.invalidate();check(sheet.input(Sheet::Key::Confirm).kind==Sheet::IntentKind::None,"invalidated catalog inert");
 }
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}

#endif
