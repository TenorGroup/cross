"""Production paper paging epoch regression, using the existing queue fixture."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--mutation-red', action='store_true', help='Remove the paper-range epoch guard in the extracted queue only')
args = parser.parse_args()
repo = args.source_root.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
fixture = repo / 'test/ugly_forms'


def extract(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


settings = repo / 'src/activities/settings/SettingsActivity.cpp'
source = settings.read_text()
queue = extract(source, 'void SettingsActivity::queueForm(') + '\n' + extract(source, 'bool SettingsActivity::handleCustomInput()')
if args.mutation_red:
    marker = '    const int previousPaperFirst = form_.paperFirst();\n'
    guard = '(form_.paperOpen() && previousPaperFirst != form_.paperFirst())'
    assert marker in queue and guard in queue, 'Current paper epoch guard required for mutation'
    queue = queue.replace(marker, '').replace(guard, 'false')
(output / 'QueueMethods.inc').write_text(queue)
ink = repo / 'src/shells/ugly/UglyInk.cpp'
nav = extract(ink.read_text(), 'void navRow(').replace('{', '{\n  ++r.navBars;', 1)
(output / 'NavRow.inc').write_text(nav)

CPP = r'''
#define main existing_queue_main
#include "queue.cpp"
#undef main
void open(SettingsActivity& a){
 a.renderer.w=480;a.renderer.h=800;a.bind();
 a.queueForm({SettingsActivity::FormEvent::Type::Key,Sheet::Key::Confirm});a.handleCustomInput();
 a.renderer.circleCount=0;a.paint();
}
void fresh(SettingsActivity& a,int first){
 a.renderer.circleCount=0;a.paint();auto now=a.renderer.circles[1];
 a.queueForm({SettingsActivity::FormEvent::Type::Tap,Sheet::Key::Confirm,(int16_t)now.x,(int16_t)now.y});
 a.handleCustomInput();check(a.commits==1&&a.form_.candidate()==first+1,"fresh tap chooses visible paper option");
}
int main(){
 SettingsActivity a;open(a);int n=a.renderer.circleCount;auto old=a.renderer.circles[1];
 const auto initialEpoch=a.formSurface_;
 for(int i=0;i<n;++i)a.queueForm({SettingsActivity::FormEvent::Type::Key,Sheet::Key::NextQuestion});
 a.queueForm({SettingsActivity::FormEvent::Type::Tap,Sheet::Key::Confirm,(int16_t)old.x,(int16_t)old.y});
 for(int i=0;a.formCount_>1;++i){a.handleCustomInput();a.renderer.circleCount=0;a.paint();if(i==0)check(a.formSurface_==initialEpoch,"candidate inside same paper keeps epoch");}
 const int first=a.form_.paperFirst();check(first==n&&a.formSurface_==initialEpoch+1,"key re-anchor advances epoch exactly once");
 a.handleCustomInput();check(a.commits==0&&a.form_.paperOpen(),"queued tap from old paper range dropped");fresh(a,first);
 SettingsActivity b;open(b);n=b.renderer.circleCount;old=b.renderer.circles[1];auto last=b.renderer.circles[n-1];
 const auto before=b.formSurface_;
 b.queueForm({SettingsActivity::FormEvent::Type::Tap,Sheet::Key::Confirm,(int16_t)old.x,(int16_t)(last.y+64)});
 b.queueForm({SettingsActivity::FormEvent::Type::Tap,Sheet::Key::Confirm,(int16_t)old.x,(int16_t)old.y});
 b.handleCustomInput();check(b.form_.paperFirst()==n&&b.formSurface_==before+1,"tap paging advances epoch");
 b.renderer.circleCount=0;b.paint();b.handleCustomInput();check(b.commits==0&&b.form_.paperOpen(),"old tap after tap paging dropped");fresh(b,n);
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
'''

(output / 'epoch.cpp').write_text(CPP)
files = [settings, ink, repo / 'src/shells/ugly/UglyQuestionSheet.cpp', repo / 'src/shells/ugly/UglyQuestionSheet.h', repo / 'src/shells/ugly/UglyInk.h']
files += [repo / 'lib/I18n' / name for name in ['I18n.h', 'I18nKeys.h', 'I18n.cpp', 'I18nStrings.h', 'I18nStrings.cpp']]
files += [fixture / name for name in ['queue.cpp', 'component.cpp', 'stubs/GfxRenderer.h']]
manifest = {'source_root': str(repo), 'mutation_red': args.mutation_red,
            'files': [{'path': str(path.relative_to(repo)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in files],
            'extracted_queue_sha256': hashlib.sha256(queue.encode()).hexdigest()}
(output / 'production-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
command = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-DFREEINK_DEVICE_X4PRO=1']
command += ['-I' + str(path) for path in [repo / 'lib/I18n', fixture, fixture / 'stubs', output, repo / 'src']]
command += [str(output / 'epoch.cpp'), str(repo / 'src/shells/ugly/UglyQuestionSheet.cpp'), str(repo / 'lib/I18n/I18n.cpp'), str(repo / 'lib/I18n/I18nStrings.cpp'), '-o', str(output / 'check')]
(output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
build = subprocess.run(command, capture_output=True, text=True)
(output / 'build.log').write_text(build.stdout + build.stderr)
if build.returncode:
    print(build.stdout + build.stderr)
    raise SystemExit(build.returncode)
run = subprocess.run([str(output / 'check')], capture_output=True, text=True)
(output / 'result.log').write_text(run.stdout + run.stderr)
print(run.stdout + run.stderr, end='')
raise SystemExit(run.returncode)
