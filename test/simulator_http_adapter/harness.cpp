#include <esp_http_client.h>
#include <sys/socket.h>
#include <chrono>
#include <thread>
#include <memory>
#include <functional>
#include <ctime>
#include <iostream>
#ifdef BASELINE
constexpr int ESP_ERR_HTTP_EAGAIN=0x7007;
int esp_http_client_get_socket(esp_http_client_handle_t){return -1;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int){return ESP_OK;}
#endif
using String=std::string;
#define LOG_ERR(...)
#define LOG_DBG(...)
#define CROSSPOINT_VERSION "sim-regression"
constexpr int WIFI_PS_NONE=0,WIFI_PS_MIN_MODEM=1;
int esp_wifi_set_ps(int){return ESP_OK;}
const char* fakeCA="fixture";
namespace network_trust {const char*forUrl(const std::string&){return fakeCA;}}
namespace base64 {std::string encode(const char*){return "fixture";}}
unsigned long millis(){static auto start=std::chrono::steady_clock::now();return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
void delay(unsigned long ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
template<class T>std::unique_ptr<T>makeUniqueNoThrow(size_t n){return std::make_unique<T>(n);}
struct HttpDownloader {using ProgressCallback=std::function<void(size_t,size_t)>;enum DownloadError{OK,HTTP_ERROR,FILE_ERROR,ABORTED};};
#include "production.inc"
int main(int argc,char**argv){
 const std::string base=argv[1];int failed=0,total=0;
 auto check=[&](bool ok,const char*name){++total;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
 for(const char* path:{"/stall-header","/stall-fixed","/stall-chunked"}){
  Sink sink;bool cancel=false;size_t callbacks=0;const auto start=millis();
  sink.cancelFlag=&cancel;sink.write=[](const uint8_t*,size_t){return true;};
  sink.progress=[&](size_t,size_t){++callbacks;if(millis()-start>=100)cancel=true;};
  auto result=runGet(base+path,"","",sink,nullptr,true);
  const auto elapsed=millis()-start;
  check(result==HttpDownloader::ABORTED&&elapsed<500&&callbacks>=2,path);
  std::cout<<"elapsed="<<elapsed<<" callbacks="<<callbacks<<" result="<<result<<"\n";
 }
 for(const char* path:{"/fixed","/chunked","/close"}){
  Sink sink;std::string bytes;size_t lastTotal=999;
  sink.write=[&](const uint8_t*p,size_t n){bytes.append(reinterpret_cast<const char*>(p),n);return true;};
  sink.progress=[&](size_t,size_t n){lastTotal=n;};
  auto result=runGet(base+path,"","",sink,nullptr,true);
  check(result==HttpDownloader::OK&&bytes.size()==328&&(std::string(path)!="/chunked"||lastTotal==0),path);
 }
 for(const char*path:{"/incomplete-fixed","/incomplete-chunked"}){
  Sink sink;sink.write=[](const uint8_t*,size_t){return true;};
  check(runGet(base+path,"","",sink,nullptr,true)==HttpDownloader::HTTP_ERROR,path);
 }
 for(const char*path:{"file:///tmp/tenor-simulator-http-local.txt","http://mock.test/local.txt"}){
  Sink sink;std::string bytes;sink.write=[&](const uint8_t*p,size_t n){bytes.append(reinterpret_cast<const char*>(p),n);return true;};
  if(std::string(path).rfind("http://mock",0)==0)setenv("CROSSPOINT_SIM_HTTP_MOCK_ROOT","/tmp/tenor-simulator-http-mock",1);
  check(runGet(path,"","",sink,nullptr,true)==HttpDownloader::OK&&bytes=="LOCAL-FIXTURE",path);
  unsetenv("CROSSPOINT_SIM_HTTP_MOCK_ROOT");
 }
 std::cout<<"RESULT "<<total-failed<<"/"<<total<<"\n";return failed?1:0;
}
