from pathlib import Path
import os,json,subprocess,shutil
r=Path(__file__).resolve().parents[2];o=Path(os.environ.get('BOOT_TEST_OUTPUT','/tmp/tenor-boot-home-tests'));o.mkdir(parents=True,exist_ok=True)
program=Path(os.environ.get('TEST_PROGRAM',r/'.pio/build/simulator_x3_uc8279/program'))
for name,recent,opened in [('book',True,True),('empty',False,False),('missing',True,True)]:
 sd=o/name;d=sd/'.crosspoint';d.mkdir(parents=True,exist_ok=True)
 if name=='book':shutil.copyfile(r/'test/epubs/test_kerning_ligature.epub',sd/'book.epub')
 (d/'state.json').write_text(json.dumps(dict(openEpubPath='/book.epub' if opened else '',lastSleepFromReader=opened,showBootScreen=True,readerActivityLoadCount=0)))
 (d/'settings.json').write_text(json.dumps(dict(language='VI',uiTheme=4,sdFontFamilyName='',sleepTimeout=10)))
 (d/'recent.json').write_text(json.dumps({'books':[dict(path='/book.epub',title='Boot fixture')] if recent else []}))
 env={k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')};env.update(SDL_VIDEODRIVER='dummy',CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_INPUT_SCRIPT='2400:QUIT',CROSSPOINT_SIM_SCREENSHOTS=f'2000:{sd}/home.bmp')
 run=subprocess.run([str(program)],cwd=r,env=env,capture_output=True,text=True,timeout=10)
 log=run.stdout+run.stderr;(sd/'run.log').write_text(log)
 assert run.returncode==0 and 'Entering activity: Home' in log and 'Entering activity: EpubReader' not in log,(name,log[-1500:])
 print('PASS',name)
