from pathlib import Path
import subprocess,argparse
p=Path(__file__).resolve().parent;repo=p.parents[1]
a=argparse.ArgumentParser();a.add_argument('--source',type=Path,default=repo/'src/components/ReadingStatsView.cpp');a.add_argument('--output',type=Path,required=True);args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(p/'stubs'),'-I'+str(repo/'test/ui_layout/stats_stubs'),'-I'+str(repo/'src'),'-I'+str(repo/'src/components'),str(p/'check.cpp'),str(args.source),str(repo/'src/components/ReadingStatsFormat.cpp'),str(repo/'src/util/NgayGio.cpp'),'-o',str(args.output/'check')],check=True)
r=subprocess.run([str(args.output/'check')],capture_output=True,text=True);print(r.stdout);(args.output/'result.log').write_text(r.stdout+r.stderr);raise SystemExit(r.returncode)
