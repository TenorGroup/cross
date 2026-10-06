"""Extract production reader lifecycle methods and check timing, locking and save gates."""
from pathlib import Path
import re, subprocess, tempfile, unittest
REPO = Path(__file__).resolve().parents[2]
def fn(text, name):
    m = re.search(r'^.*EpubReaderActivity::' + name + r'\s*\(', text, re.M)
    brace = text.index('{', m.end()); i = brace + 1; depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}'); i += 1
    return text[m.start():i]

STUB = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>
int lockDepth=0,starts=0,abandoned=0,destroyed=0,writes=0,updates=0,version=0;
uint32_t now=0;
uint32_t millis(){return now;}
struct RenderLock {
 struct TryTake{};
 RenderLock(){assert(lockDepth==0 && "recursive RenderLock");++lockDepth;}
 RenderLock(TryTake):RenderLock(){}
 bool acquired()const{return true;}
 ~RenderLock(){--lockDepth;}
};
struct Guarded {
 uint8_t value=0;
 operator uint8_t()const{return value;}
 Guarded& operator=(uint8_t v){assert(lockDepth==1 && "SETTINGS mutated outside RenderLock");value=v;++version;return *this;}
};
struct ReaderRenderSpec {int version;};
struct Renderer{};
struct Settings {
 Guarded lineSpacing,paragraphAlignment,dropCapMode;
 ReaderRenderSpec readerRenderSpec(int,int){assert(lockDepth==1);return {version};}
 void saveToFile(){++writes;}
} SETTINGS;
const int kSpacingByPlace[]={1,2,0,3,4};
int textChoiceCount(int row){return row==4?3:5;}
int textChoiceInUse(int row){
 if(row==2){for(int i=0;i<5;++i)if(kSpacingByPlace[i]==SETTINGS.lineSpacing)return i;}
 return row==3?int(SETTINGS.paragraphAlignment):int(SETTINGS.dropCapMode);
}
struct Section {
 int currentPage=12,pageCount=40,builtVersion=-1;
 bool building=true,reached=false;
 Section()=default;
 Section(std::shared_ptr<int>,int,Renderer&,bool){}
 void abandonBuild(){++abandoned;}
 ~Section(){++destroyed;}
 bool loadSectionFile(ReaderRenderSpec){return false;}
 bool isPartial(){return true;}
 bool coversVisibleTextOffset(uint32_t){return false;}
 bool startBuild(ReaderRenderSpec spec){++starts;builtVersion=spec.version;return true;}
 bool isBuilding(){return building;}
 bool buildSomeMore(int){return true;}
 bool buildStarved(){return false;}
 bool buildReachedVisibleTextOffset(uint32_t){return reached;}
 std::optional<int> getPageForVisibleTextOffset(uint32_t){return 12;}
 void parkBuild(){}
};
struct Power {struct Lock{Lock(){} ~Lock(){}};};
using HalPowerManager=Power;
struct Esp {int free=100000,largest=100000;int getFreeHeap(){return free;}int getMaxAllocHeap(){return largest;}} ESP;
namespace bleturner {struct Status{bool starting=false;} state;Status status(){return state;}}
constexpr int BACKGROUND_BUILD_MIN_FREE_HEAP=2000,BACKGROUND_BUILD_MIN_MAX_ALLOC=2000;
#define LOG_INF(...) do {} while(0)
struct SdFonts {void ensureLoaded(Renderer&){assert(lockDepth==1);}void releaseCatalog(){}}
;
struct ActivityManager {
 bool sleep=false;void(*deferred)()=nullptr;
 bool isSleepTransition(){return sleep;}
 void deferWrite(void(*write)()){assert(!deferred);deferred=write;}
} activityManager;
struct EpubReaderActivity {
 std::unique_ptr<Section> section,catchUp;
 std::shared_ptr<int> epub=std::make_shared<int>(0);
 std::optional<uint32_t> cachedVisibleTextOffset;
 int cachedSpineIndex=0,currentSpineIndex=0,cachedChapterTotalPageCount=0,nextPageNumber=0;
 int buildViewportWidth=400,buildViewportHeight=600,xemTruocLat=0,catchUpFails=0;
 bool xemTruocTrenMan=false,xemTruoc=false,preview=false,bleDeferred=false;
 uint32_t xemTruocDich=0,xemTruocInputMs=0;
 static constexpr uint32_t CATCH_UP_QUIET_MS=QUIET;
 static constexpr int CATCH_UP_MAX_FAILS=3;
 bool textSettingsDirty=false;
 enum class TextDepth:uint8_t{Rows,Fonts,Spacing,PointSize};
 TextDepth textDepth=TextDepth::Rows;
 std::vector<int> fontFamilies;
 enum class Overlay{None,Text};Overlay overlay=Overlay::None;
 std::atomic<bool> paintDropped{false};
 std::atomic<uint8_t> textCloseFrame{0};
 Renderer renderer;SdFonts sdFontSystem;
 bool deferBackgroundBuildForBle() const {return bleDeferred;}
 void rememberCurrentContentOffset(){cachedVisibleTextOffset=4000;}
 void requestUpdate(){++updates;}
 void discardOverlayPage(){assert(lockDepth==1);}
 void danLaiTrang();void dropCatchUp();void catchUpTick(bool);bool catchUpCanTick() const;
 void chooseTextValue(int,int);void applyTextSettingLive();
 void invalidateTextSettingsLocked();void applyReaderTextSettingsLocked();void applyReaderTextSettings();
 void markClosedTextFrameUpLocked();void flushTextSettingsLocked();void flushTextSettings();
 void panelClosedLocked(bool,bool);void panelClosed(bool=false,bool=false);
};
'''
QUIET_MAIN = r'''
int main(){
 EpubReaderActivity r;r.cachedVisibleTextOffset=4386;r.xemTruocDich=4386;r.xemTruoc=true;
 r.catchUp=std::make_unique<Section>();
 {RenderLock lock;r.danLaiTrang();}
 assert(!r.catchUp&&abandoned==1&&destroyed==1&&r.xemTruocDich==4386);
 now=400;r.catchUpTick(false);assert(starts==0&&"catchUp started at400 before quiet600");
 now=599;r.catchUpTick(false);assert(starts==0);
 now=600;r.catchUpTick(false);assert(starts==1);
 {RenderLock lock;r.danLaiTrang();}
 assert(!r.catchUp&&abandoned==2);
 now=1199;r.catchUpTick(false);assert(starts==1);
 r.bleDeferred=true;now=1200;r.catchUpTick(false);assert(starts==1);
 r.bleDeferred=false;bleturner::state.starting=true;r.catchUpTick(false);assert(starts==1);
 bleturner::state.starting=false;ESP.free=1000;r.catchUpTick(false);assert(starts==1);
 ESP.free=100000;ESP.largest=1000;now=1800;r.catchUpTick(false);assert(starts==1);
 ESP.largest=100000;now=2400;r.catchUpTick(false);assert(starts==2);
 printf("quiet400/599/600 latest cancellation BLE startup free largest gates PASS\n");
}
'''
ATOMIC_MAIN = r'''
int main(){
 EpubReaderActivity r;r.cachedVisibleTextOffset=4386;r.xemTruocDich=4386;r.xemTruoc=true;
 r.catchUp=std::make_unique<Section>();
 r.textCloseFrame=2;r.chooseTextValue(2,0);assert(r.textCloseFrame==0);assert(!r.catchUp&&r.xemTruocDich==4386&&r.textSettingsDirty);
 now=100;r.chooseTextValue(2,1);assert(int(SETTINGS.lineSpacing)==2&&updates==2);
 now=499;r.catchUpTick(false);assert(starts==0);
 now=699;r.catchUpTick(false);assert(starts==0);
 now=700;r.catchUpTick(false);assert(starts==1&&r.catchUp->builtVersion==version);
 printf("atomic SETTINGS+invalidation no recursive lock rapid latest spec PASS\n");
}
'''
SAVE_MAIN = r'''
void maybeFlush(EpubReaderActivity& r){
 if(r.textCloseFrame.load(std::memory_order_acquire)==2){
  RenderLock lock;
  if(r.textCloseFrame.load(std::memory_order_acquire)==2)r.flushTextSettingsLocked();
 }
}
int main(){
 EpubReaderActivity r;r.textSettingsDirty=true;r.panelClosed(false,false);
 assert(writes==0&&r.textCloseFrame==1);
 r.paintDropped=true;
 {RenderLock lock;r.markClosedTextFrameUpLocked();}
 now=4000;maybeFlush(r);assert(writes==0&&"dropped or timeout opened close-write gate");
 r.paintDropped=false;r.overlay=EpubReaderActivity::Overlay::Text;
 {RenderLock lock;r.markClosedTextFrameUpLocked();}
 maybeFlush(r);assert(writes==0);
 r.overlay=EpubReaderActivity::Overlay::None;
 {RenderLock lock;r.markClosedTextFrameUpLocked();}
 maybeFlush(r);maybeFlush(r);assert(writes==1&&!r.textSettingsDirty&&r.textCloseFrame==0);
 r.textSettingsDirty=true;r.panelClosed(false,true);assert(writes==2);
 r.textSettingsDirty=true;activityManager.sleep=true;
 {RenderLock lock;r.panelClosedLocked(true,false);}
 assert(writes==3&&!r.textSettingsDirty&&r.textCloseFrame==0);
 r.textSettingsDirty=true;activityManager.sleep=false;
 {RenderLock lock;r.panelClosedLocked(true,false);}
 assert(writes==3&&activityManager.deferred&&!r.textSettingsDirty);
 activityManager.deferred();activityManager.deferred=nullptr;assert(writes==4);
 printf("droppedframe timeout overlay saveonce frameUp sleep leaving locked PASS\n");
}
'''
class ReaderTableLifecycleTest(unittest.TestCase):
    def run_production(self, main):
        text = (REPO/'src/activities/reader/EpubReaderActivity.cpp').read_text()
        names = ('danLaiTrang','dropCatchUp','catchUpTick','catchUpCanTick','chooseTextValue','applyTextSettingLive',
                 'invalidateTextSettingsLocked','applyReaderTextSettingsLocked','applyReaderTextSettings',
                 'markClosedTextFrameUpLocked','flushTextSettingsLocked','flushTextSettings',
                 'panelClosedLocked','panelClosed')
        header = (REPO/'src/ReaderTextRelayout.h').read_text()
        quiet = re.search(r'kQuietMs\s*=\s*(\d+)', header).group(1)
        reader_header = (REPO/'src/activities/reader/EpubReaderActivity.h').read_text()
        self.assertIn('CATCH_UP_QUIET_MS = TextRelayoutQuiet::kQuietMs',reader_header)
        code = STUB.replace('=QUIET;',f'={quiet};') + '\n'.join(fn(text,n) for n in names) + main
        with tempfile.TemporaryDirectory(prefix='reader-table-lifecycle-') as tmp:
            cpp,program = Path(tmp)/'test.cpp',Path(tmp)/'test'
            cpp.write_text(code)
            build = subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                                    str(cpp),'-o',str(program)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run = subprocess.run([str(program)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout.strip())
    def test_quiet_and_resource_gates(self): self.run_production(QUIET_MAIN)
    def test_atomic_latest_settings(self): self.run_production(ATOMIC_MAIN)
    def test_actual_frame_before_save(self): self.run_production(SAVE_MAIN)
if __name__ == '__main__': unittest.main()
