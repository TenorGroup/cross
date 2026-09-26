int failures=0,tests=0;
void require(bool yes,const char*message){if(!yes)throw std::runtime_error(message);}
void test(const std::string&name,std::function<void()>fn){tests++; RenderLock::busy=false;nowMs=1000;READING_STATS={};activityManager.generation=1;activityManager.pendingAction=ActivityManager::PendingAction::None;activityManager.sleepTransition=false;activityManager.exclusive=false;activityManager.preventSleep=false;gpio={};mappedInputManager={};powerManager={};halTiltSensor={};filetransfer::active=false;freeink::BleKeyboardHost::getInstance()={};freeink::ble::stopped=false;freeink::ble::rearm=false;freeink::ble::held=false;freeink::ble::deferred=false;freeink::ble::init=false;freeink::ble::starts=0;freeink::ble::startSuccess=true;SETTINGS.blePageTurnerEnabled=true;SETTINGS.bleRemoteCount=0;
 try{fn();std::cout<<"PASS "<<name<<"\n";}catch(const std::exception&e){failures++;std::cout<<"FAIL "<<name<<": "<<e.what()<<"\n";}RenderLock::busy=false;activityManager.currentActivity.reset();}
template<class T>std::shared_ptr<T>reader(){auto r=std::make_shared<T>();activityManager.currentActivity=r;return r;}
void press(int key){freeink::BleKeyboardHost::getInstance().queue.push_back({key,0,true});}
void release(int key){freeink::BleKeyboardHost::getInstance().queue.push_back({key,0,false});}
void tap(int key){press(key);release(key);}
void noiRemote(){freeink::BleKeyboardHost::getInstance().connected=true;}

int page(const TxtReaderActivity&r){return r.currentPage;}int page(const XtcReaderActivity&r){return r.currentPage;}int page(const EpubReaderActivity&r){return r.section?r.section->currentPage:-1;}
void last(TxtReaderActivity&r){r.currentPage=3;}void last(XtcReaderActivity&r){r.currentPage=3;}void last(EpubReaderActivity&r){r.section->currentPage=3;r.currentSpineIndex=2;}
template<class T>void formatTests(const std::string&name){
 test(name+" manager action updates page, repaint and stats once",[]{auto r=reader<T>();require(activityManager.pageTurn(true),"mapped action was rejected");r->loop();require(page(*r)==2,"page did not advance once");require(r->requests==1,"changed page has no single repaint");r->updateReadingTime(true);require(READING_STATS.pages==1,"accepted turn missing from stats");r->loop();r->updateReadingTime(true);require(READING_STATS.pages==1&&r->requests==1,"idle retry repeats action/stats/repaint");});
 test(name+" render busy defers one action",[]{auto r=reader<T>();RenderLock::busy=true;require(activityManager.pageTurn(true),"busy reader must enqueue");r->loop();require(page(*r)==1&&r->requests==0&&r->trangDaLat==0,"model changed while renderer owns it");RenderLock::busy=false;r->loop();require(page(*r)==2&&r->requests==1&&r->trangDaLat==1,"deferred action was lost or repeated");r->loop();require(page(*r)==2,"deferred action repeated");});
 test(name+" generation change drops deferred action",[]{auto r=reader<T>();RenderLock::busy=true;activityManager.pageTurn(true);activityManager.generation++;RenderLock::busy=false;r->loop();require(page(*r)==1&&r->requests==0&&r->trangDaLat==0,"stale action reached a new activity generation");});
 test(name+" bounded input burst keeps one pending turn",[]{auto r=reader<T>();RenderLock::busy=true;activityManager.pageTurn(true);activityManager.pageTurn(true);activityManager.pageTurn(false);RenderLock::busy=false;r->loop();require(page(*r)==0&&r->trangDaLat==1&&r->requests==1,"burst accumulated multiple turns instead of latest direction");});
 test(name+" end-of-book forward returns home",[]{auto r=reader<T>();last(*r);activityManager.pageTurn(true);r->loop();require(r->isAtEndOfBook(),"last page failed to enter end screen");nowMs+=250;activityManager.pageTurn(true);r->loop();require(r->goHome==1,"end-of-book forward did not use common handler");require(r->trangDaLat==1,"end action counted as another book page");});
 test(name+" end-of-book previous returns to last page",[]{auto r=reader<T>();last(*r);activityManager.pageTurn(true);r->loop();nowMs+=250;activityManager.pageTurn(false);r->loop();require(!r->isAtEndOfBook(),"previous did not leave end screen");require(r->requests==2,"return from end screen must repaint");require(r->trangDaLat==1,"end-screen return counted as ordinary page");});
 test(name+" end menu owns input",[]{auto r=reader<T>();last(*r);activityManager.pageTurn(true);r->loop();nowMs+=250;r->endOfBookOptionsReady=true;r->endOfBookOptions->menu=true;const int requests=r->requests;const auto turns=r->trangDaLat;activityManager.pageTurn(false);r->loop();require(r->isAtEndOfBook()&&r->trangDaLat==turns&&r->requests==requests,"remote bypassed end menu input ownership");});
 test(name+" closing end menu discards its earlier remote report",[]{auto r=reader<T>();last(*r);activityManager.pageTurn(true);r->loop();nowMs+=250;r->endOfBookOptionsReady=true;r->endOfBookOptions->menu=true;activityManager.pageTurn(false);r->loop();r->onReturnFromEndOfBook();r->endOfBookOptions->menu=false;nowMs+=250;r->loop();require(r->trangDaLat==1,"queued remote report replayed after menu closed");});
 test(name+" pending activity rejects enqueue",[]{auto r=reader<T>();activityManager.pendingAction=ActivityManager::PendingAction::Push;require(!activityManager.pageTurn(true),"transitioning reader accepted remote input");r->loop();require(page(*r)==1,"transitioning reader changed model");});
}
int main(){
 test("BLE suspended partial watermark preserves radio and queued Next",[]{
 auto r=reader<EpubReaderActivity>(); r->section->currentPage=3; r->section->partial=true;
 auto& host=freeink::BleKeyboardHost::getInstance();host.running=true;host.connected=true;
 MainPump p; RenderLock::busy=true;tap(1);p.pump();r->loop();
 require(page(*r)==3,"queued action mutated render-owned model");
 RenderLock::busy=false;r->loop();
 require(r->section&&page(*r)==4&&r->currentSpineIndex==0,"queued Next skipped partial chapter");
 require(host.running&&host.connected&&!freeink::ble::stopped,"partial Next stopped radio");
 require(r->requests==1&&r->trangDaLat==1,"Next did not request exactly one repaint/stat");
 r->loop();require(r->requests==1&&r->trangDaLat==1,"queued Next repeated");
 });
 formatTests<TxtReaderActivity>("TXT");formatTests<XtcReaderActivity>("XTC");formatTests<EpubReaderActivity>("EPUB");
 test("EPUB chapter forward holds one nonrecursive lock",[]{auto r=reader<EpubReaderActivity>();r->section->currentPage=3;activityManager.pageTurn(true);r->loop();require(r->currentSpineIndex==1&&!r->section&&r->requests==1&&r->trangDaLat==1,"chapter forward has wrong model/repaint/stats");});
 test("EPUB chapter previous holds one nonrecursive lock",[]{auto r=reader<EpubReaderActivity>();r->section->currentPage=0;r->currentSpineIndex=1;activityManager.pageTurn(false);r->loop();require(r->currentSpineIndex==0&&!r->section&&r->pendingPageJump==65535&&r->requests==1&&r->trangDaLat==1,"chapter previous has wrong model/repaint/stats");});
 test("EPUB 200 ms manual guard defers",[]{auto r=reader<EpubReaderActivity>();r->lastPageTurnTime=900;activityManager.pageTurn(true);r->loop();require(page(*r)==1,"remote bypassed 200 ms guard");nowMs=1100;r->loop();require(page(*r)==2&&r->requests==1&&r->trangDaLat==1,"guard expiry did not drain once");});
 test("EPUB overlay rejects remote action",[]{auto r=reader<EpubReaderActivity>();r->overlay=EpubReaderActivity::Overlay::Toolbar;require(!activityManager.pageTurn(true),"overlay accepted remote action");r->loop();require(page(*r)==1&&r->requests==0,"remote bypassed toolbar");});
 test("EPUB opening overlay drops queued action",[]{auto r=reader<EpubReaderActivity>();RenderLock::busy=true;activityManager.pageTurn(true);r->overlay=EpubReaderActivity::Overlay::WordPicker;RenderLock::busy=false;r->loop();r->overlay=EpubReaderActivity::Overlay::None;nowMs+=250;r->loop();require(page(*r)==1&&r->requests==0,"overlay retained stale deferred action");});

 test("BLE-04 mapped report updates both clocks and displayed page",[]{auto r=reader<TxtReaderActivity>();MainPump p;nowMs=59000;tap(1);p.pump();r->loop();require(p.lastActivityTime==59000&&p.lastSleepResetTime==59000,"accepted BLE report did not reset both clocks");require(page(*r)==2&&r->requests==1,"main-to-reader action did not repaint exactly once");});
 test("BLE-06 selected peer is armed once before the first poll",[]{auto r=reader<TxtReaderActivity>();(void)r;std::strcpy(SETTINGS.blePeerAddr,"7d:de:5c:bd:ae:ca");auto& host=freeink::BleKeyboardHost::getInstance();MainPump p;p.pump();require(host.armCalls==1,"selected peer was not armed");require(host.armedAddr==SETTINGS.blePeerAddr,"selected peer address was not propagated");require(host.events.size()>=2&&host.events[0]=="arm"&&host.events[1]=="poll","selected reconnect was armed after poll");p.pump();require(host.armCalls==1,"selected peer was armed more than once per reader generation");});
 test("BLE-06 selected peer arm waits for async begin",[]{auto r=reader<TxtReaderActivity>();(void)r;std::strcpy(SETTINGS.blePeerAddr,"7d:de:5c:bd:ae:ca");auto& host=freeink::BleKeyboardHost::getInstance();host.running=false;freeink::ble::init=true;MainPump p;p.pump();require(host.armCalls==0,"selected peer armed while BLE initialization was still running");freeink::ble::init=false;host.running=true;p.pump();require(host.armCalls==1,"selected peer was not armed after async begin finished");});
 test("BLE-06 generation and idle rearm each arm once",[]{auto r=reader<TxtReaderActivity>();(void)r;std::strcpy(SETTINGS.blePeerAddr,"7d:de:5c:bd:ae:ca");auto& host=freeink::BleKeyboardHost::getInstance();MainPump p;p.pump();require(host.armCalls==1,"initial selected peer arm missing");activityManager.generation++;p.pump();require(host.armCalls==2,"new reader generation did not rearm selected peer");freeink::ble::stopped=true;host.running=false;mappedInputManager.released=true;nowMs++;p.pump();require(host.armCalls==3,"local idle rearm did not arm selected peer once");mappedInputManager.released=false;nowMs++;p.pump();require(host.armCalls==3,"idle rearm repeated without new input");});
 test("BLE-06 selected peer arm is skipped outside ready reader",[]{auto r=reader<TxtReaderActivity>();(void)r;std::strcpy(SETTINGS.blePeerAddr,"7d:de:5c:bd:ae:ca");auto& host=freeink::BleKeyboardHost::getInstance();MainPump p;activityManager.pendingAction=ActivityManager::PendingAction::Push;p.pump();require(host.armCalls==0,"selected peer armed during an activity transition");activityManager.pendingAction=ActivityManager::PendingAction::None;SETTINGS.blePageTurnerEnabled=false;p.pump();require(host.armCalls==0,"selected peer armed while page turner was disabled");});
 test("BLE-04 tab tilt snapshot resets both clocks once",[]{struct TabActivity final:Activity{bool isReaderActivity()const override{return false;}};activityManager.currentActivity=std::make_shared<TabActivity>();MainPump p;nowMs=59000;halTiltSensor.active=true;p.pump();require(p.lastActivityTime==59000&&p.lastSleepResetTime==59000,"tab tilt snapshot did not reset both clocks");nowMs=59001;p.pump();require(p.lastActivityTime==59000&&p.lastSleepResetTime==59000,"consumed tab tilt snapshot reset clocks again");});
 test("BLE-04 unmapped and modifier reports leave both clocks",[]{auto r=reader<TxtReaderActivity>();MainPump p;nowMs=59000;freeink::BleKeyboardHost::getInstance().queue={{42,0},{1,1}};p.pump();r->loop();require(p.lastActivityTime==1000&&p.lastSleepResetTime==1000&&page(*r)==1,"unmapped remote input kept reader awake");});
 test("BLE-04 deferred drain resets activity once per new event",[]{auto r=reader<TxtReaderActivity>();MainPump p;nowMs=2000;RenderLock::busy=true;tap(1);p.pump();r->loop();require(p.lastSleepResetTime==2000,"queued event failed to reset sleep");for(int i=0;i<10;i++){nowMs+=100;p.pump();r->loop();}require(p.lastSleepResetTime==2000,"deferred turn repeatedly reset inactivity");RenderLock::busy=false;r->loop();require(page(*r)==2&&r->requests==1,"busy report did not render once after release");nowMs=62000;p.pump();require(nowMs-p.lastSleepResetTime>=60000,"idle timeout cannot elapse after final report");});
 test("BLE-04 remote-only reading survives timeout then becomes idle",[]{auto r=reader<TxtReaderActivity>();MainPump p;freeink::BleKeyboardHost::getInstance().connected=true;for(int i=0;i<10;i++){nowMs+=40000;tap(i%2?2:1);p.pump();r->loop();require(nowMs-p.lastSleepResetTime<60000,"active remote-only reader reached sleep deadline");}nowMs+=60000;p.pump();require(nowMs-p.lastSleepResetTime==60000,"remote silence failed to reach sleep deadline");});
 for(int button=0;button<4;button++)test("BLE-05 local page release rearms once button "+std::to_string(button),[button]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();nowMs+=300001;p.pump();require(freeink::ble::idleStopped()&&!freeink::BleKeyboardHost::getInstance().running,"disconnected radio did not idle-off");mappedInputManager.released=true;mappedInputManager.releasedButton=button;nowMs++;p.pump();require(freeink::ble::starts==1&&!freeink::ble::idleStopped()&&freeink::BleKeyboardHost::getInstance().running,"physical page action did not rearm radio");mappedInputManager.released=false;for(int i=0;i<20;i++){nowMs+=100;p.pump();}require(freeink::ble::starts==1,"rearm allocated repeatedly without new intent");});
 test("BLE-05 reader start refused for memory retries after five seconds",[]{auto r=reader<TxtReaderActivity>();MainPump p;freeink::BleKeyboardHost::getInstance().running=false;freeink::ble::startSuccess=false;p.pump();require(freeink::ble::starts==1&&freeink::ble::readerStartDeferred(),"first refused start not recorded as deferred");for(int i=0;i<40;i++){nowMs+=100;p.pump();}require(freeink::ble::starts==1,"refused start retried before five seconds");freeink::ble::startSuccess=true;nowMs+=1100;p.pump();require(freeink::ble::starts==2&&freeink::BleKeyboardHost::getInstance().running,"refused start was not retried once the heap settled");nowMs+=6000;p.pump();require(freeink::ble::starts==2,"running radio was started again");});
 test("BLE-05 refused reader start retries are bounded",[]{auto r=reader<TxtReaderActivity>();MainPump p;freeink::BleKeyboardHost::getInstance().running=false;freeink::ble::startSuccess=false;for(int i=0;i<120;i++){nowMs+=1000;p.pump();}require(freeink::ble::starts>=2&&freeink::ble::starts<=7,"refused start retries not bounded to a handful");});
 test("BLE-05 build rearm request rearms once",[]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();nowMs+=300001;p.pump();require(freeink::ble::idleStopped(),"disconnected radio did not idle-off");freeink::ble::requestRearm();nowMs++;p.pump();require(freeink::ble::starts==1&&!freeink::ble::idleStopped()&&!freeink::ble::rearm,"build rearm request did not restart radio once");for(int i=0;i<20;i++){nowMs+=100;p.pump();}require(freeink::ble::starts==1,"build rearm repeated without a new request");});
 // Device evidence (d2, 51033 ms): a page release restarted the radio that a starved build had
 // just stopped; the start task then held the main loop for 2.85 s.
 test("BLE-05 radio held for a starved build waits for the reader, not a page release",[]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();freeink::ble::stopped=true;freeink::ble::held=true;freeink::BleKeyboardHost::getInstance().running=false;mappedInputManager.released=true;nowMs++;p.pump();mappedInputManager.released=false;require(freeink::ble::starts==0&&freeink::ble::idleStopped(),"page release restarted a radio the build still needs stopped");freeink::ble::requestRearm();nowMs++;p.pump();require(freeink::ble::starts==1&&!freeink::ble::idleStopped(),"reader rearm after the page did not restart the radio");});
 test("BLE-05 build hold does not outlive the reader visit",[]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();freeink::ble::stopped=true;freeink::ble::held=true;freeink::BleKeyboardHost::getInstance().running=false;activityManager.generation++;nowMs++;p.pump();mappedInputManager.released=true;nowMs++;p.pump();require(freeink::ble::starts==1,"a new reader visit kept the previous build's radio hold");});
 test("BLE-05 touch rearms once",[]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();nowMs+=300001;p.pump();gpio.touch=true;nowMs++;p.pump();require(freeink::ble::starts==1&&!freeink::ble::idleStopped(),"reader touch did not rearm idle radio");});
 test("BLE-05 failed rearm is bounded without allocation retries",[]{auto r=reader<TxtReaderActivity>();MainPump p;p.pump();nowMs+=300001;p.pump();freeink::ble::startSuccess=false;mappedInputManager.released=true;nowMs++;p.pump();mappedInputManager.released=false;for(int i=0;i<20;i++){nowMs+=100;p.pump();}require(freeink::ble::starts==1,"failed reader rearm did not make exactly one attempt");});
 test("BLE-05 connected idle remote remains on",[]{auto r=reader<TxtReaderActivity>();MainPump p;freeink::BleKeyboardHost::getInstance().connected=true;p.pump();nowMs+=600000;p.pump();require(!freeink::ble::idleStopped()&&freeink::BleKeyboardHost::getInstance().running,"connected remote was stopped by idle rule");});

 for(bool previewMode:{false,true}) {
  test(std::string("TXT local tap during render drains once preview=")+(previewMode?"yes":"no"),[previewMode]{auto r=reader<TxtReaderActivity>();r->preview=previewMode;r->statsEnabled=!previewMode;RenderLock::busy=true;r->mappedInput.next=true;r->loop();r->mappedInput.next=false;require(page(*r)==1,"busy local tap mutated renderer-owned model");RenderLock::busy=false;r->loop();require(page(*r)==2&&r->trangDaLat==1&&r->requests>=1,"busy local tap was lost");r->updateReadingTime(true);require(READING_STATS.pages==(previewMode?0u:1u),"preview/local reading stats mismatch");int requests=r->requests;r->loop();require(page(*r)==2&&r->requests==requests,"drained local tap repeated");});
  test(std::string("XTC local tap during render drains once preview=")+(previewMode?"yes":"no"),[previewMode]{auto r=reader<XtcReaderActivity>();r->preview=previewMode;r->statsEnabled=!previewMode;RenderLock::busy=true;r->mappedInput.next=true;r->loop();r->mappedInput.next=false;require(page(*r)==1,"busy local tap mutated renderer-owned model");RenderLock::busy=false;r->loop();require(page(*r)==2&&r->trangDaLat==1&&r->requests>=1,"busy local tap was lost");r->updateReadingTime(true);require(READING_STATS.pages==(previewMode?0u:1u),"preview/local reading stats mismatch");int requests=r->requests;r->loop();require(page(*r)==2&&r->requests==requests,"drained local tap repeated");});
 }
 test("EPUB deferred remote preserves local Back priority",[]{auto r=reader<EpubReaderActivity>();RenderLock::busy=true;activityManager.pageTurn(true);r->mappedInput.back=true;r->loop();require(r->backCalls==1,"remote deferral swallowed Back release");require(page(*r)==1,"Back tick changed page");});
 test("EPUB deferred remote preserves local Confirm priority",[]{auto r=reader<EpubReaderActivity>();RenderLock::busy=true;activityManager.pageTurn(true);r->mappedInput.confirm=true;r->loop();require(r->openedMenus==1,"remote deferral swallowed Confirm release");require(page(*r)==1,"Confirm tick changed page");});
 // Lan bam DAU TIEN cua phien lat trang ngay luc bam nhu truoc, vi chua biet remote co bao
 // nha nut hay khong. Thay khung nha roi thi tu do moi doi luc nha va biet giu.
 // Free3 bao mot khung nha co dinh 60 toi 100 ms sau khung bam, bat ke tay giu bao lau, nen
 // do giu la vo nghia voi no. Lat trang ngay luc bam nhu truoc; khung nha khong lam gi.
 test("BLE press turns a page at once even after a release frame was seen",[]{
 auto r=reader<TxtReaderActivity>();MainPump p;noiRemote();
 press(1);p.pump();r->loop();require(page(*r)==2,"first press did not turn at once");
 nowMs+=70;release(1);p.pump();r->loop();require(page(*r)==2&&r->trangDaLat==1,"the release frame acted");
 nowMs+=500;press(1);p.pump();r->loop();require(page(*r)==3,"press after a learned release waited for the release");
 nowMs+=900;release(1);p.pump();r->loop();require(page(*r)==3&&r->trangDaLat==2,"a long press-release pair acted twice");
 require(r->chapterSkips==0,"a page button reached chapter skip");
 });
 test("BLE release frames alone never act",[]{
 auto r=reader<TxtReaderActivity>();MainPump p;noiRemote();
 for(int i=0;i<3;i++){release(1);p.pump();r->loop();nowMs+=100;}
 require(page(*r)==1&&r->trangDaLat==0&&r->chapterSkips==0,"a stray release frame turned a page");
 });
 // Per-button binding (v1.0.14): RAW edges of a remote with a table, through the real main
 // loop into the reader. Three-button remote: button 3 sends "00 02 00" (byte 1) on a tap and
 // "08 00 00" when held. Default table by name: tap = next chapter, hold = previous chapter;
 // the page buttons keep the old usage mapping.
 const auto noiRemoteTen=[](const char*ten){auto&h=freeink::BleKeyboardHost::getInstance();h.connected=true;h.addr="7d:de:5c:bd:ae:ca";h.name=ten;};
 const auto canhCu=[](uint32_t code){freeink::RawButtonEvent e;e.value=code&0xFF;e.byteIndex=(code>>8)&0xFF;e.reportId=(code>>16)&0xFF;e.atMs=nowMs;freeink::BleKeyboardHost::getInstance().raw.push_back(e);};
 const auto canh=[](uint32_t code,bool nhan,uint8_t keycode=0){freeink::RawButtonEvent e;e.value=code&0xFF;e.byteIndex=(code>>8)&0xFF;e.reportId=(code>>16)&0xFF;e.pressed=nhan;e.keycode=keycode;e.atMs=nowMs;freeink::BleKeyboardHost::getInstance().raw.push_back(e);};
 test("BLE table: third button tap skips one chapter forward and turns no page",[&]{
 auto r=reader<EpubReaderActivity>();MainPump p;noiRemoteTen("Free3-R");p.pump();nowMs=5000;
 canh(0x030102,true);p.pump();r->loop();nowMs+=60;canh(0x030102,false);p.pump();r->loop();
 require(r->chapterSkips==1&&r->currentSpineIndex==1,"third button tap did not skip one chapter forward");
 require(r->trangDaLat==0,"a chapter skip was counted as a page");
 require(p.lastSleepResetTime==nowMs-60,"accepted remote action did not reset the sleep clock");
 });
 test("BLE table: held third button frame skips one chapter back",[&]{
 auto r=reader<EpubReaderActivity>();r->currentSpineIndex=2;MainPump p;noiRemoteTen("Free3-R");p.pump();
 canh(0x030008,true,0x30);p.pump();r->loop();nowMs+=53;canh(0x030008,false);p.pump();r->loop();
 require(r->chapterSkips==1&&r->currentSpineIndex==1,"hold frame did not skip one chapter back");
 });
 test("BLE table: page frame turns exactly one page though its key event is queued too",[&]{
 auto r=reader<TxtReaderActivity>();MainPump p;noiRemoteTen("Free3-R");p.pump();
 // The notify can land between the raw and the key drains: the key event then comes one pass later.
 canh(0x030002,true,1);p.pump();r->loop();press(1);nowMs+=20;p.pump();r->loop();nowMs+=40;canh(0x030002,false);release(1);p.pump();r->loop();
 require(page(*r)==2&&r->trangDaLat==1,"table route and key path turned the page twice (or not at all)");
 require(r->chapterSkips==0,"a page button skipped a chapter");
 });
 test("BLE no table: raw edges are drained and the key path is unchanged",[&]{
 auto r=reader<TxtReaderActivity>();MainPump p;noiRemoteTen("Some Remote");p.pump();
 canh(0x030002,true,1);press(1);p.pump();r->loop();
 canh(0x030102,true);p.pump();r->loop();
 require(page(*r)==2&&r->trangDaLat==1&&r->chapterSkips==0,"a remote without a table left today's key path");
 require(freeink::BleKeyboardHost::getInstance().raw.empty(),"raw edges were left in the ring");
 });
 test("BLE hold slot: a tap acts on release, a hold acts at the threshold and its release does nothing",[&]{
 auto r=reader<EpubReaderActivity>();r->section->currentPage=1;MainPump p;noiRemoteTen("Some Remote");
 auto*t=blebinding::editableTable(SETTINGS.bleRemotes,SETTINGS.bleRemoteCount,"7d:de:5c:bd:ae:ca","Some Remote");
 require(t&&blebinding::learn(*t,blebinding::Action::NextPage,0x030102,false)&&blebinding::learn(*t,blebinding::Action::NextChapter,0x030102,true),"table setup");p.pump();
 canh(0x030102,true);p.pump();r->loop();require(page(*r)==1&&r->chapterSkips==0,"a button with a hold slot acted on its press");
 nowMs+=100;canh(0x030102,false);p.pump();r->loop();require(page(*r)==2&&r->chapterSkips==0,"a 100 ms tap did not turn one page");
 nowMs+=250;canh(0x030102,true);p.pump();r->loop();nowMs+=699;p.pump();r->loop();require(r->chapterSkips==0,"hold fired before the threshold");
 nowMs+=1;p.pump();r->loop();require(r->chapterSkips==1&&r->trangDaLat==1,"hold did not skip one chapter at the threshold");
 nowMs+=300;canh(0x030102,false);p.pump();r->loop();require(r->chapterSkips==1&&r->trangDaLat==1,"the release after a hold acted");
 });
 test("BLE table: reader menu and save quotation buttons go through the shared quick actions and turn nothing",[&]{
 auto r=reader<EpubReaderActivity>();MainPump p;noiRemoteTen("Some Remote");quickActions.clear();
 auto*t=blebinding::editableTable(SETTINGS.bleRemotes,SETTINGS.bleRemoteCount,"7d:de:5c:bd:ae:ca","Some Remote");
 require(t&&blebinding::learn(*t,blebinding::Action::ReaderMenu,0x030001,false)&&blebinding::learn(*t,blebinding::Action::SaveQuote,0x030008,false),"table setup");p.pump();
 nowMs=5000;canh(0x030001,true);p.pump();r->loop();nowMs+=60;canh(0x030001,false);p.pump();r->loop();
 nowMs+=400;canh(0x030008,true);p.pump();r->loop();nowMs+=60;canh(0x030008,false);p.pump();r->loop();
 const std::vector<std::pair<uint8_t,quickaction::Trigger>> want{{CrossPointSettings::READER_MENU,quickaction::Trigger::Remote},{CrossPointSettings::SAVE_QUOTE,quickaction::Trigger::Remote}};
 require(quickActions==want,"a shortcut button did not ask the shared catalog once, as a remote");
 require(page(*r)==1&&r->trangDaLat==0&&r->chapterSkips==0,"a shortcut button turned a page or a chapter");
 require(p.lastSleepResetTime==nowMs-60,"a shortcut button did not reset the sleep clock");
 });
 test("BLE router drops a table when the link goes to another remote",[&]{
 auto r=reader<EpubReaderActivity>();MainPump p;noiRemoteTen("Free3-R");p.pump();
 canh(0x030102,true);p.pump();r->loop();canh(0x030102,false);p.pump();r->loop();require(r->chapterSkips==1,"setup skip missing");
 auto&h=freeink::BleKeyboardHost::getInstance();h.connected=false;nowMs+=10;p.pump();
 noiRemoteTen("Some Remote");nowMs+=10;canh(0x030102,true);p.pump();r->loop();
 require(r->chapterSkips==1,"the previous remote's table acted for another remote");
 });
 test("BLE table: edges queued before the reader chose its table are dropped",[&]{
 auto r=reader<EpubReaderActivity>();MainPump p;noiRemoteTen("Free3-R");
 canhCu(0x030102);p.pump();r->loop();nowMs+=900;p.pump();r->loop();
 require(r->chapterSkips==0&&r->trangDaLat==0,"a press queued before the reader pass acted");
 canh(0x030102,true);p.pump();r->loop();require(r->chapterSkips==1,"a fresh press after the table was chosen did not act");
 });
 std::cout<<"RESULT "<<tests-failures<<"/"<<tests<<" passed\n";return failures?1:0;
}
