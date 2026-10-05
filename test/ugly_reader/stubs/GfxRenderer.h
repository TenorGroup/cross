#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>
enum class Color {White,Black,LightGray,DarkGray,Clear};
struct EpdFontFamily {
 enum Style {REGULAR=0,BOLD=1,SUP=2,SUB=4};
 struct Glyph {int width=8,height=20,left=0,top=20;};
 const Glyph* getGlyph(uint32_t,Style)const{return nullptr;}
};
class GfxRenderer {
 public:
 mutable int draws=0,measures=0;mutable std::array<int,4> clip{0,0,480,800};
 int width=480,height=800;
 int getScreenWidth()const{return width;}int getScreenHeight()const{return height;}
 auto getClipRect()const{return clip;}
 void clearScreen()const{++draws;}
 int getOrientation()const{return 0;}
 void getOrientedViewableTRBL(int*t,int*r,int*b,int*l)const{*t=*r=*b=*l=0;}
 void setClipRect(int x,int y,int w,int h)const{clip={x,y,w,h};}
 int getLineHeight(int)const{return 24;}
 int getFontAscenderSize(int)const{return 20;}
 int getTextWidth(int,const char*s,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{++measures;return strlen(s)*8;}
 std::string truncatedText(int,const char*s,int w,EpdFontFamily::Style)const{return std::string(s).substr(0,std::max(0,w)/8);}
 std::vector<std::string> wrappedText(int,const char*s,int,int,EpdFontFamily::Style)const{return {s};}
 const std::map<int,EpdFontFamily>& getFontMap()const{static std::map<int,EpdFontFamily> m;return m;}
 template<class... A>void fillRoundedRect(A...)const{++draws;}
 template<class... A>void fillRect(A...)const{++draws;}
 template<class... A>void fillRectDither(A...)const{++draws;}
 template<class... A>void drawRoundedRect(A...)const{++draws;}
 template<class... A>void drawRect(A...)const{++draws;}
 template<class... A>void drawLine(A...)const{++draws;}
 template<class... A>void drawPixel(A...)const{++draws;}
 template<class... A>void fillPolygon(A...)const{++draws;}
 template<class... A>void drawText(A...)const{++draws;}
 template<class... A>void drawTextRotated90CW(A...)const{++draws;}
};
