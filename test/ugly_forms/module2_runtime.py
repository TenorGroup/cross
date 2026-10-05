"""Focused production M2 methods with observable storage/font/lock boundaries."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess
p=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,default=p.parents[1])
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--mutations',action='store_true')
a=parser.parse_args();repo=a.source_root.resolve();a.output.mkdir(parents=True,exist_ok=True)
def extract(s,sig):
 start=s.index(sig);opening=s.index('{',start);i=opening+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
def strip_includes(s):return re.sub(r'^#include[^\n]*\n','',s,flags=re.M)
files=['src/activities/settings/'+n for n in ['TextSettingsActivity.cpp','TextSettingsActivity.h','StatusBarSettingsActivity.cpp','StatusBarSettingsActivity.h','SettingsActivity.h','SettingsActivity.cpp']]
files+=['src/SettingsList.h','src/CrossPointSettings.h','src/CrossPointSettings.cpp','src/ReaderFontChon.cpp','src/ReaderFontChon.h','src/ReaderFontSizes.cpp','src/ReaderFontSizes.h','src/ReaderInkWeight.h','lib/Epub/Epub/ReaderSpacing.h','src/MenuFavorites.cpp','src/MenuFavorites.h','src/shells/ugly/UglyQuestionSheet.cpp','src/shells/ugly/UglyQuestionSheet.h','src/shells/ugly/UglyInk.h','src/shells/ugly/UglyInk.cpp']
files+=['lib/I18n/'+n for n in ['I18n.h','I18nKeys.h','I18n.cpp','I18nStrings.h','I18nStrings.cpp']]
snapshot=a.output/'snapshot'
for f in files:
 dest=snapshot/f;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/f,dest)
manifest={'source_root':str(repo),'files':[{'path':f,'sha256':hashlib.sha256((snapshot/f).read_bytes()).hexdigest()} for f in files]}
(a.output/'production-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
def read(f):return (snapshot/f).read_text()
cps=read('src/CrossPointSettings.h')
enums=['STATUS_BAR_PROGRESS_BAR','STATUS_BAR_PROGRESS_BAR_THICKNESS','STATUS_BAR_TITLE','XTC_STATUS_BAR_MODE','STATUS_BAR_CLOCK_MODE','READER_STATUS_BAR_MODE']
base='\n'.join(extract(cps,'enum '+e)+';' for e in enums)
base+='\n'+extract(cps,'struct StatusBarSpec')+';\n'
base+='\n'.join(re.findall(r'static constexpr uint8_t SCREEN_MARGIN_[A-Z]+ = [^;]+;',cps))
base+='\nenum {NOTOSERIF=0,NOTOSANS=1,BUILTIN_FONT_COUNT=2};\n'
base+='\n'.join('uint8_t '+n+'=0;' for n in ['fontFamily','fontPointSize','lineSpacing','letterSpacing','wordSpacing','extraParagraphSpacing','paragraphAlignment','screenMargin','paragraphIndent','dropCapMode','hyphenationEnabled','embeddedStyle','textAntiAliasing','readerInkWeight','statusBarChapterPageCount','statusBarBookProgressPercentage','statusBarProgressBar','statusBarProgressBarThickness','statusBarTitle','xtcStatusBarMode','statusBarClock','readerStatusBarMode','statusBarItemsMode','clockFormat'])
base+='\nchar sdFontFamilyName[64]{};\nStatusBarSpec statusBarSpec() const;void adoptReaderStatusItems();bool readerStatusBarHidden() const{return readerStatusBarMode==READER_STATUS_BAR_OFF;}bool migrate(JsonDoc doc);\n'
(a.output/'SettingsFields.inc').write_text(base)
info='enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };\nenum class SettingAction {None,CustomiseStatusBar};\n'+extract(read('src/activities/settings/SettingsActivity.h'),'struct SettingInfo')+';\n'
(a.output/'SettingInfo.inc').write_text(info)
text=read('src/activities/settings/TextSettingsActivity.cpp');status=read('src/activities/settings/StatusBarSettingsActivity.cpp')
constants=text[text.index('constexpr StrId TAB_NAME_IDS'):text.index('}  // namespace')]
# Full production catalog statements for the 14 adapter rows, without unrelated settings.
ids=['FONT_FAMILY','FONT_SIZE','LINE_SPACING','LETTER_SPACING','WORD_SPACING','EXTRA_SPACING','PARA_ALIGNMENT','SCREEN_MARGIN','PARAGRAPH_INDENT','FOCUS_READING','HYPHENATION','EMBEDDED_STYLE','TEXT_AA','READER_INK_WEIGHT']
list_source=read('src/SettingsList.h');statements=re.findall(r'v\.push_back\([\s\S]*?;',list_source)
catalog=[next(s for s in statements if re.search(r'SettingInfo::\w+\(\s*StrId::STR_'+i+r'\b',s)) for i in ids]
(a.output/'Catalog.inc').write_text('const std::vector<SettingInfo>& getBaseSettingsList(){static const auto rows=[](){std::vector<SettingInfo> v;\n'+'\n'.join(catalog)+'\nreturn v;}();return rows;}\n')
# Headers retain actual fields/declarations; access and virtual markers are test-only.
for n in ['TextSettingsActivity','StatusBarSettingsActivity']:
 h=strip_includes(read('src/activities/settings/'+n+'.h')).replace(' override','').replace(' final','').replace(' private:',' public:')
 (a.output/(n+'.inc')).write_text(h)
methods=[]
for sig in ['TextSettingsActivity::TextSettingsActivity(', 'void TextSettingsActivity::onEnter(', 'void TextSettingsActivity::rebuildSizeList(', 'int TextSettingsActivity::listCount(', 'void TextSettingsActivity::onTabAction(', 'void TextSettingsActivity::activateIndex(', 'int TextSettingsActivity::formIndex(', 'TextSettingsActivity::Tab TextSettingsActivity::formTab(', 'int TextSettingsActivity::formLocalRow(', 'const SettingInfo* TextSettingsActivity::formSetting(', 'ugly::QuestionSheet::Row TextSettingsActivity::formRow(', 'void TextSettingsActivity::formLabel(', 'bool TextSettingsActivity::saveSettings(', 'bool TextSettingsActivity::applyChosenValue(', 'void TextSettingsActivity::focusForm(', 'void TextSettingsActivity::bindForm(', 'void TextSettingsActivity::applyFormIntent(', 'bool TextSettingsActivity::handleButtons(', 'bool TextSettingsActivity::applyFamily(', 'bool TextSettingsActivity::applySize(', 'void TextSettingsActivity::activateRow(', 'void TextSettingsActivity::confirmLayoutRow(', 'void TextSettingsActivity::confirmStyleRow(', 'std::string TextSettingsActivity::favoriteKey(', 'int TextSettingsActivity::focusFavorite(']:methods.append(extract(text,sig))
for sig in ['StatusBarSettingsActivity::StatusBarSettingsActivity(', 'void StatusBarSettingsActivity::onEnter(', 'void StatusBarSettingsActivity::activateIndex(', 'void StatusBarSettingsActivity::handleSelection(', 'bool StatusBarSettingsActivity::saveSettings(', 'bool StatusBarSettingsActivity::applyChosenValue(', 'ugly::QuestionSheet::Row StatusBarSettingsActivity::formRow(', 'void StatusBarSettingsActivity::formLabel(', 'void StatusBarSettingsActivity::focusForm(', 'void StatusBarSettingsActivity::bindForm(', 'void StatusBarSettingsActivity::applyFormIntent(', 'bool StatusBarSettingsActivity::handleButtons(']:methods.append(extract(status,sig))
status_const=status[status.index('enum MenuItem'):status.index('}  // namespace')]
font=strip_includes(read('src/ReaderFontChon.cpp'))
fav=read('src/MenuFavorites.cpp');font+='\nnamespace menufavorites {\n'+extract(fav,'constexpr Descriptor ITEMS[] =')+';\n'+extract(fav,'const Descriptor* find(')+'\n'+extract(fav,'const char* keyFor(')+'\n}\n'
cps_cpp=read('src/CrossPointSettings.cpp');start=cps_cpp.index('enum : uint8_t { SB_TITLE')
mode_const=cps_cpp[start:cps_cpp.index('}  // namespace',start)]
support=mode_const+'\n'+extract(cps_cpp,'CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec(')+'\n'+extract(cps_cpp,'void CrossPointSettings::adoptReaderStatusItems(')
# Narrow unchanged fromJson migration block, JSON field access injected by fixture.
start=cps_cpp.index('if (doc["readerStatusBarMode"].isNull()');end=cps_cpp.index('\n\n  // Retire',start)
support+='\nbool CrossPointSettings::migrate(JsonDoc doc){bool needsResave=false;\n'+cps_cpp[start:end]+'\nreturn needsResave;}\n'
parent=read('src/activities/settings/SettingsActivity.cpp');start=parent.index('case SettingAction::CustomiseStatusBar:');case=parent[start:parent.index('case SettingAction::ClockSettings:',start)]
parent_methods=extract(parent,'bool SettingsActivity::saveSettings(')+'\nvoid SettingsActivity::openStatus(){auto resultHandler = [this](const ActivityResult&) { saveSettings(); };(void)resultHandler;switch(SettingAction::CustomiseStatusBar){'+case+'default:break;}}\n'
block=constants+'\n'+status_const+'\n'+font+'\n'+support+'\n'+'\n'.join(methods)+'\n'+parent_methods
(a.output/'Methods.inc').write_text(block)
manifest['catalog_rows']=len(catalog);manifest['methods']=len(methods);manifest['extracted_sha256']=hashlib.sha256(block.encode()).hexdigest()
(a.output/'production-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
fixtures=a.output/'fixtures';fixtures.mkdir(exist_ok=True)
input_fixture='''class MappedInputManager {public: enum class Button{Back,Confirm,Left,Right,Up,Down};bool back=false,confirm=false,touch=false;bool hasTouch()const{return touch;}bool wasReleased(Button b){return b==Button::Back?std::exchange(back,false):b==Button::Confirm?std::exchange(confirm,false):false;}};'''
(fixtures/'GfxRenderer.h').write_text((p/'stubs/GfxRenderer.h').read_text().replace('class MappedInputManager {};',input_fixture))
(fixtures/'SdCardFontRegistry.h').write_text('''#pragma once
#include <string>
#include <vector>
struct SdCardFontFamilyInfo {std::string name;bool vector=false;std::vector<uint8_t> sizes;std::vector<uint8_t> availableSizes()const{return sizes;}};
class SdCardFontRegistry {public:std::vector<SdCardFontFamilyInfo> families;const auto& getFamilies()const{return families;}int getFamilyCount()const{return families.size();}const SdCardFontFamilyInfo* findFamily(const char* n)const{for(const auto& f:families)if(f.name==n)return &f;return nullptr;}};
''')
ink=read('src/shells/ugly/UglyInk.cpp');(a.output/'NavRow.inc').write_text(extract(ink,'void navRow('))
variants={'green':block}
if a.mutations:
 replacements={
 'back-unconditional':('if (saveFailed_.load() && !saveSettings(false))','if (!saveSettings(false))'),
 'back-ignores-failure':('if (saveFailed_.load() && !saveSettings(false)) { requestUpdate(); return; }','if (saveFailed_.load()) saveSettings(false);'),
 'favorite-family-autocycle':('if (index < 2) {','if (index < 0) {'),
 'size-index-as-point':('fontdoc::apCo(renderer, sizes_[listIndex].pointSize);','fontdoc::apCo(renderer, listIndex);'),
 'font-without-lock':('RenderLock lock;',''),
 'margin-index-as-value':('setting->valueRange.min + option * setting->valueRange.step : option','option : option'),
 'style-wrap-hardcoded':('(current.selected + 1) % current.count','(current.selected + 1) % 2'),
 'status-no-adopt':('SETTINGS.adoptReaderStatusItems();',''),
 'status-no-clock-clamp':('SETTINGS.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_MODE::STATUS_BAR_CLOCK_HIDE;',''),
 'text-save-selected':('if (current.selected == option) return true;',''),
 'parent-unconditional-save':('startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), [this](const ActivityResult&) { requestUpdate(); });','startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);'),
 'status-double-refresh':('applyChosenValue(nav.selected, (row.selected + 1) % row.count, false);','applyChosenValue(nav.selected, (row.selected + 1) % row.count, true);')}
 for name,(old,new) in replacements.items():
  if old not in block:raise ValueError('missing mutant '+name)
  variants[name]=block.replace(old,new)
if a.mutations:
 variants={**{n:s for n,s in variants.items() if n!='green'},'green':block}
results=[]
for profile in ['x3','x4pro']:
 for name,content in variants.items():
  out=a.output/profile/name;out.mkdir(parents=True,exist_ok=True);(out/'Methods.inc').write_text(content)
  cmd=['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DFREEINK_DEVICE_X4PRO='+str(int(profile=='x4pro')),'-DMODULE2_EXPECTED_BOARD='+('1' if profile=='x4pro' else '0'),'-I'+str(out),'-I'+str(fixtures),'-I'+str(a.output),'-I'+str(snapshot/'src'),'-I'+str(snapshot/'lib/I18n'),'-I'+str(snapshot/'lib/Epub/Epub'),str(p/'module2_runtime.cpp'),str(snapshot/'src/shells/ugly/UglyQuestionSheet.cpp'),str(snapshot/'src/ReaderFontSizes.cpp'),str(snapshot/'lib/I18n/I18n.cpp'),str(snapshot/'lib/I18n/I18nStrings.cpp'),'-o',str(out/'check')]
  (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');r=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(r.stdout+r.stderr)
  if r.returncode:print(profile,name,'BUILD FAIL',r.stderr);results.append({'profile':profile,'variant':name,'build':r.returncode});continue
  r=subprocess.run([str(out/'check')],capture_output=True,text=True);(out/'result.log').write_text(r.stdout+r.stderr)
  results.append({'profile':profile,'variant':name,'returncode':r.returncode,'output':r.stdout.strip()});print(profile,name,r.stdout.strip())
(a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
raise SystemExit(any('build' in r or (r['returncode']!=0 if r['variant']=='green' else r['returncode']==0) for r in results))
