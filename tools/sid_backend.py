#!/usr/bin/env python3
"""Prepare pinned external SID tools under ignored .local/sid/tools (Windows/Linux)."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
import concurrent.futures
import shlex
import sys

def build_command(command, msys, cwd=None, env=None):
    if msys is None:
        subprocess.run(command, cwd=cwd, env=env, check=True)
    else:
        script='export PATH='+shlex.quote(msys_path(msys/'ucrt64/bin'))+':/usr/bin; '+shlex.join(command)
        subprocess.run([str(msys/'usr/bin/bash.exe'),'-lc',script],cwd=cwd,check=True)

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / '.local/sid'
GOAT_URL = 'https://deb.debian.org/debian/pool/main/g/goattracker/goattracker_2.77+ds.orig.tar.xz'
GOAT_HASH = '4cf6860bc0e459ab75b1cd2cd7e39ca3338a1b6efa4c32171cfdac594dc6c8d9'
PLAYER_URL = 'https://github.com/libsidplayfp/sidplayfp/releases/download/v3.2.1/sidplayfp-3.2.1-ucrt64.zip'
PLAYER_HASH = 'd9d746a207f7367185fd4ec3e7e3baf0ddddaa34eceaa528c35a0d3e6b854642'
LIBRARIES = [
    ('libresidfp','1.2.3','e9b53a984ca708c3e8d9139eb6aad6141f73ffc22d70a020f7966803f4e2f76d'),
    ('libsidplayfp','3.1.1','12b79190593bf480b2d11481b5c2de62bac07f344437a66cd8d887329875c626')]

def msys_path(path):
    value=path.resolve().as_posix()
    return '/'+value[0].lower()+value[2:] if len(value)>1 and value[1]==':' else value

def prepare_renderer(msys, tools):
    prefix=msys_path(BASE/'native')
    for name,version,digest in LIBRARIES:
        archive=BASE/f'{name}-{version}.tar.gz'
        download(f'https://github.com/libsidplayfp/{name}/releases/download/v{version}/{archive.name}',digest,archive)
        source=BASE/f'{name}-{version}'
        if not source.exists():
            with tarfile.open(archive) as tar:tar.extractall(BASE,filter='data')
        if name=='libresidfp':
            for chip,functions in [('6581',['filterSummer','filterMixer','filterGain','filterResonance','filterVcrVg','filterVcrIds']),('8580',['filterSummer','filterMixer','filterGain','filterResonance'])]:
                file=source/f'src/FilterModelConfig{chip}.cpp';content=file.read_text()
                marker=content.find('#ifdef HAVE_JTHREADS',content.index('// We spawn'))
                if marker!=-1:
                    end=content.index('\n}',marker)
                    file.write_text(content[:marker]+' '.join(f+'();' for f in functions)+'\n'+content[end:])
        configure=['./configure','--disable-shared','--enable-static','--prefix='+prefix]
        if msys is not None: configure.append('--host=x86_64-w64-mingw32')
        if name=='libsidplayfp':configure+=['--without-exsid','--without-usbsid']
        if msys is None:
            env=os.environ.copy()
            env['RESIDFP_CFLAGS']='-I'+prefix+'/include'
            env['RESIDFP_LIBS']='-L'+prefix+'/lib -lresidfp'
            if not (source/'Makefile').exists(): build_command(configure,None,cwd=source,env=env)
            build_command(['make','-j8'],None,cwd=source,env=env)
            build_command(['make','install'],None,cwd=source,env=env)
            shutil.copy2(source/'COPYING',tools/f'{name}-COPYING.txt')
            continue
        script='export PATH='+shlex.quote(msys_path(msys/'ucrt64/bin'))+':/usr/bin; '
        if name=='libsidplayfp':script+='export RESIDFP_CFLAGS='+shlex.quote('-I'+prefix+'/include')+'; export RESIDFP_LIBS='+shlex.quote('-L'+prefix+'/lib -lresidfp')+'; '
        script+='cd '+shlex.quote(msys_path(source))+' && '
        if not (source/'Makefile').exists():script+=shlex.join(configure)+' && '
        script+='make -j8 && make install'
        subprocess.run([str(msys/'usr/bin/bash.exe'),'-lc',script],check=True)
        shutil.copy2(source/'COPYING',tools/f'{name}-COPYING.txt')
    command=helper_command(msys,tools,'sid_render.cpp','gtracker_sidrender')
    build_command(command,msys)
    shutil.copy2(ROOT/'tools/sid_render.cpp',tools/'sid_render.cpp')

def download(url, expected, target):
    if not target.exists():
        urllib.request.urlretrieve(url, target)
    if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'Checksum mismatch: {target}')

def helper_command(msys,tools,source,name):
    prefix=msys_path(BASE/'native')
    suffix='.exe' if msys is not None else ''
    flags=['-static','-lwinpthread'] if msys is not None else ['-pthread']
    return ['g++','-O2','-std=c++17','-I'+prefix+'/include',msys_path(ROOT/'tools'/source),
            '-L'+prefix+'/lib','-lsidplayfp','-lresidfp',*flags,'-o',msys_path(tools/(name+suffix))]

def prepare_video(msys):
    """Add isolated-voice rendering without invalidating the existing audition bank."""
    tools=BASE/'tools'
    prefix=msys_path(BASE/'native')
    if not (BASE/'native/lib/libsidplayfp.a').is_file():
        raise RuntimeError('Run tools/sid_backend.py prepare before prepare-video.')
    for manifest,source,name in [('video.json','sid_render.cpp','gtracker_sidvisual'),('stream.json','sid_stream.cpp','gtracker_sidstream')]:
        command=helper_command(msys,tools,source,name)
        build_command(command,msys)
        binary=Path(command[-1]).name
        (tools/manifest).write_text(json.dumps({'version':1,'files':{binary:hashlib.sha256((tools/binary).read_bytes()).hexdigest()}},indent=2)+'\n')
    print('Isolated SID voice renderer prepared')

def prepare(msys):
    if os.name != 'nt':
        return prepare_linux()
    BASE.mkdir(parents=True, exist_ok=True)
    goat = BASE / 'goattracker-2.77.tar.xz'
    player = BASE / 'sidplayfp.zip'
    download(GOAT_URL, GOAT_HASH, goat)
    download(PLAYER_URL, PLAYER_HASH, player)
    source = BASE / 'build-goat'
    source.mkdir(exist_ok=True)
    with tarfile.open(goat) as archive:
        archive.extractall(source, filter='data')
    src = source / 'goattracker-2.77+ds/src'
    # Retain normal redirected diagnostics and avoid console-window dependencies.
    file = src / 'gt2reloc.c'
    content = file.read_text()
    content = content.replace('STDOUT = fopen("CON", "w");', 'STDOUT = stdout;').replace('STDERR = fopen("CON", "w");', 'STDERR = stderr;')
    content = content.replace('#include "bme.h"', '#include "bme.h"\n#ifdef main\n#undef main\n#endif')
    file.write_text(content)
    (src.parent / 'win32').mkdir(exist_ok=True)
    # Debian source includes prebuilt Linux objects; never reuse those for Windows.
    for stale in src.rglob('*.o'):
        stale.unlink()
    env = os.environ.copy()
    env['PATH'] = str(msys / 'mingw32/bin') + os.pathsep + str(msys / 'usr/bin') + os.pathsep + env['PATH']
    subprocess.run([str(msys / 'usr/bin/make.exe'), '-f', 'makefile.common', 'PREFIX=../win32/', 'SUFFIX=.exe', 'CC=gcc', 'CXX=g++',
                    'CFLAGS=-Ibme -Iasm -O2 -fcommon', 'LIBS=-lSDL -static-libstdc++ -static-libgcc', '../win32/gt2reloc.exe', '-j8'], cwd=src, env=env, check=True)
    tools = BASE / 'tools'
    tools.mkdir(exist_ok=True)
    shutil.copy2(src.parent / 'win32/gt2reloc.exe', tools)
    shutil.copy2(msys / 'mingw32/bin/SDL.dll', tools)
    shutil.copy2(msys / 'mingw32/bin/libwinpthread-1.dll', tools)
    shutil.copy2(msys / 'mingw32/share/licenses/libwinpthread/COPYING',tools/'winpthreads-COPYING.txt')
    sdl_source=BASE/'SDL-source.tar.gz'
    download('https://codeload.github.com/libsdl-org/SDL-1.2/tar.gz/ef3a6c05','214abe7e4f911e99f7adc9e47bfbe353594635ca92012fefc03cc725bc91f28d',sdl_source)
    with tarfile.open(sdl_source) as archive:
        member=archive.extractfile('SDL-1.2-ef3a6c05/COPYING')
        (tools/'SDL-COPYING.txt').write_bytes(member.read())
    with zipfile.ZipFile(player) as archive:
        archive.extractall(tools)
    # sidplayfp Windows explicitly prefers this file beside its executable.
    (tools / 'sidplayfp.ini').write_text('[SIDPlayfp]\nVersion=2\nSonglength Database=\nKernal Rom=\nBasic Rom=\nChargen Rom=\n[Audio]\nFrequency=48000\nChannels=1\nBitsPerSample=16\n[Emulation]\nEngine=RESIDFP\n[Console]\nANSI=false\n', encoding='ascii')
    shutil.copy2(src.parent / 'copying', tools / 'GoatTracker-COPYING.txt')
    shutil.copy2(src.parent / 'readme.txt', tools / 'GoatTracker-readme.txt')
    prepare_renderer(msys,tools)
    (tools / 'backend.json').write_text(json.dumps({'goattracker':{'version':'2.77','source':GOAT_URL,'sha256':GOAT_HASH,'patch':'redirect CON to stdout/stderr'},
        'sidplayfp':{'version':'3.2.1','archive':PLAYER_URL,'sha256':PLAYER_HASH},
        'renderer':{'adapter':1,'libsidplayfp':'3.1.1','libresidfp':'1.2.3','patch':'serial analogue table construction','sources':LIBRARIES},
        'files':{name:hashlib.sha256((tools/name).read_bytes()).hexdigest() for name in ['gt2reloc.exe','SDL.dll','libwinpthread-1.dll','gtracker_sidrender.exe','GoatTracker-COPYING.txt','GoatTracker-readme.txt','libsidplayfp-COPYING.txt','libresidfp-COPYING.txt','SDL-COPYING.txt','winpthreads-COPYING.txt']}}, indent=2)+'\n')
    print(tools)
    prepare_video(msys)

def prepare_linux():
    if not sys.platform.startswith('linux'):
        raise RuntimeError('macOS backend preparation is deferred pending native qualification.')
    for tool in ('make','gcc','g++','sdl-config','pkg-config'):
        if shutil.which(tool) is None: raise RuntimeError('Missing Linux build dependency: '+tool)
    BASE.mkdir(parents=True,exist_ok=True)
    tools=BASE/'tools'
    tools.mkdir(exist_ok=True)
    goat=BASE/'goattracker-2.77.tar.xz'
    download(GOAT_URL,GOAT_HASH,goat)
    source=BASE/'build-goat'
    source.mkdir(exist_ok=True)
    with tarfile.open(goat) as archive: archive.extractall(source,filter='data')
    src=source/'goattracker-2.77+ds/src'
    for stale in src.rglob('*.o'): stale.unlink()
    output=src.parent/'linux'
    output.mkdir(exist_ok=True)
    cflags=subprocess.check_output(['sdl-config','--cflags'],text=True).strip()
    libs=subprocess.check_output(['sdl-config','--libs'],text=True).strip()
    subprocess.run(['make','-f','makefile.common','PREFIX=../linux/','SUFFIX=',
        'CC=gcc','CXX=g++','CFLAGS=-Ibme -Iasm -O2 -fcommon '+cflags,
        'LIBS='+libs,'../linux/gt2reloc','-j8'],cwd=src,check=True)
    shutil.copy2(output/'gt2reloc',tools/'gt2reloc')
    shutil.copy2(src.parent/'copying',tools/'GoatTracker-COPYING.txt')
    shutil.copy2(src.parent/'readme.txt',tools/'GoatTracker-readme.txt')
    prepare_renderer(None,tools)
    names=['gt2reloc','gtracker_sidrender','GoatTracker-COPYING.txt','GoatTracker-readme.txt',
           'libsidplayfp-COPYING.txt','libresidfp-COPYING.txt']
    (tools/'backend.json').write_text(json.dumps({'goattracker':{'version':'2.77','source':GOAT_URL,'sha256':GOAT_HASH},
        'renderer':{'adapter':1,'libsidplayfp':'3.1.1','libresidfp':'1.2.3','patch':'serial analogue table construction','sources':LIBRARIES},
        'system_dependencies':['SDL 1.2 ABI (distribution package)'],
        'files':{name:hashlib.sha256((tools/name).read_bytes()).hexdigest() for name in names}},indent=2)+'\n')
    prepare_video(None)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare','prepare-video','check','auditions'])
    parser.add_argument('--profile',default='desktop',choices=['headless','desktop'])
    parser.add_argument('--msys',type=Path,default=Path('C:/msys64'))
    args=parser.parse_args()
    if args.command=='prepare': prepare(args.msys)
    elif args.command=='prepare-video': prepare_video(args.msys if os.name=='nt' else None)
    elif args.command=='check':
        tools=BASE/'tools'
        manifest=json.loads((tools/'backend.json').read_text())
        for name,digest in manifest['files'].items():
            if hashlib.sha256((tools/name).read_bytes()).hexdigest()!=digest: raise RuntimeError('Backend mismatch: '+name)
        print('Pinned SID backend checksums verified')
    else:
        cli=ROOT/'.local/build'/args.profile/'bin/gtracker_cli.exe'
        tools=BASE/'tools'
        def make(pair):
            instrument,note=pair
            temporary=BASE/f'audition-{instrument}-{note}.wav'
            result=subprocess.run([str(cli),'sid-audition',str(note),str(instrument),'--output',str(temporary),'--sid-tools',str(tools),'--json'],cwd=ROOT,encoding='utf-8',capture_output=True,check=True)
            digest=json.loads(result.stdout)['cache_identity']
            destination=BASE/'auditions'/digest/temporary.name.removeprefix('audition-')
            destination.parent.mkdir(parents=True,exist_ok=True)
            os.replace(temporary,destination)
            return destination
        pairs=[(instrument,note) for instrument in range(1,12) for note in (range(24,96) if instrument<9 else [60])]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            completed=[]
            for i,_ in enumerate(pool.map(make,pairs),1):
                completed.append(_)
                if i%48==0 or i==len(pairs): print(f'Auditions {i}/{len(pairs)}',flush=True)
        identity=completed[0].parent
        (identity/'atlas.json').write_text(json.dumps({'cache_identity':identity.name,'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in completed}},indent=2)+'\n')
