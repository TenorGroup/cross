from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[2]
s=(R/'lib/GfxRenderer/GfxRenderer.cpp').read_text();a=s.index('std::vector<std::string> GfxRenderer::wrappedText(');b=s.index('// Note: Internal driver',a);body=s[a:b]
header='''#include <cassert>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "Utf8.h"
#if __has_include("CjkTextWrap.h")
#include "CjkTextWrap.h"
#endif
struct EpdFontFamily { enum Style { REGULAR }; };
struct GfxRenderer {
int getTextWidth(int,const char* t,EpdFontFamily::Style) const {int w=0;auto p=(const unsigned char*)t;while(*p){auto cp=utf8NextCodepoint(&p);w+=cp>127?10:5;}return w;}
std::string truncatedText(int f,const char* t,int w,EpdFontFamily::Style s) const {std::string out=t;if(getTextWidth(f,t,s)<=w)return out;while(!out.empty()&&getTextWidth(f,(out+"...").c_str(),s)>w)utf8RemoveLastChar(out);return getTextWidth(f,"...",s)<=w?out+"...":"";}
std::vector<std::string> wrappedText(int,const char*,int,int,EpdFontFamily::Style) const;
};
'''
tests='''int main(){GfxRenderer g;auto s=EpdFontFamily::REGULAR;
auto wrap=[&](const char* t,int width,int lines){return g.wrappedText(0,t,width,lines,s);};
auto join=[](const auto& v){std::string t;for(auto& l:v)t+=l;return t;};
auto a=wrap("阅读设置字体大小章节目录同步进度",40,10);assert(a.size()==4);assert(join(a)=="阅读设置字体大小章节目录同步进度");
a=wrap("阅读《书籍》，然后继续。",40,10);assert(join(a)=="阅读《书籍》，然后继续。");for(auto& l:a){assert(g.getTextWidth(0,l.c_str(),s)<=40);assert(l.find("，")!=0);assert(l.find("。")!=0);assert(l.find("》")!=0);assert(l.substr(l.size()-3)!="《");}
a=wrap("设置\\n阅读统计",40,5);assert(a.size()==2&&a[0]=="设置"&&a[1]=="阅读统计");
a=wrap("设置 KOReader 同步",65,5);assert(a.size()>=2);assert(join(a).find("KOReader")!=std::string::npos);
a=wrap("网址 https://abcdefghijklmnopqrstuvwxyz.test/path 阅读",40,20);for(auto& l:a)assert(g.getTextWidth(0,l.c_str(),s)<=40);assert(join(a).find("https://abcdefghijklmnopqrstuvwxyz.test/path")!=std::string::npos);
a=wrap("阅读设置字体大小章节目录同步进度",40,2);assert(a.size()==2);assert(a.back().find("...")!=std::string::npos);
a=wrap("阅读设置",5,4);for(auto& l:a)assert(g.getTextWidth(0,l.c_str(),s)<=5);
a=wrap("one two three four",40,10);assert((a==std::vector<std::string>{"one two","three","four"}));
a=wrap("阅读  收藏",40,10);for(auto& l:a)assert(l.empty()||(l.front()!=' '&&l.back()!=' '));
std::cout<<"PASS: actual GfxRenderer wrappedText, CJK/mixed/PUA/Latin/width/newline/ellipsis\\n";}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(header+body+tests)
 c=subprocess.run(['c++','-std=c++20','-I'+str(R/'lib/Utf8'),'-I'+str(R/'lib/GfxRenderer'),str(p/'test.cpp'),str(R/'lib/Utf8/Utf8.cpp'),'-o',str(p/'test')],capture_output=True,text=True);assert c.returncode==0,c.stderr
 q=subprocess.run([str(p/'test')],capture_output=True,text=True);print(q.stdout+q.stderr);raise SystemExit(q.returncode)
