#!/usr/bin/env python3
"""Execute a trusted desktop candidate in an isolated Unicode/space directory.

This runs packaged binaries. Only use archives you built or otherwise trust.
Dummy devices do not establish real display, microphone or listening quality.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import subprocess
import uuid
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(archive):
    folder=ROOT/'.local/package-checks'/('candidate Ω '+uuid.uuid4().hex[:12])
    folder.mkdir(parents=True)
    with zipfile.ZipFile(archive) as zipped:
        names=zipped.namelist()
        if len(set(names))!=len(names): raise ValueError('Duplicate package paths')
        for entry in zipped.infolist():
            path=PurePosixPath(entry.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or path.parts[0]!='gtracker':
                raise ValueError('Unsafe archive path')
            target=folder.joinpath(*path.parts)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(zipped.read(entry))
            if os.name!='nt': target.chmod((entry.external_attr>>16)&0o777)
    app=folder/'gtracker'
    manifest=json.loads((app/'package.json').read_text())
    for entry in manifest['files']:
        path=app/entry['path']
        if digest(path)!=entry['sha256'] or path.stat().st_size!=entry['size_bytes']:
            raise ValueError('Payload mismatch: '+entry['path'])
    before={str(p.relative_to(app)):digest(p) for p in app.rglob('*') if p.is_file()}
    env=os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software',SDL_AUDIODRIVER='dummy')
    suffix='.exe' if os.name=='nt' else ''
    cli=app/('gtracker_cli'+suffix)
    score=json.loads((app/'examples/sid/heavy-mellow.sidtrk.json').read_text())
    score['sections']=score['sections'][:1]
    score['sections'][0]['order']=score['sections'][0]['order'][:1]
    source=folder/'short.sidtrk.json'
    source.write_text(json.dumps(score),encoding='utf-8')
    reports={}
    for operation,extension in [('sid-export','sid'),('sid-export-prg','prg'),('sid-render','wav'),('sid-export-mp4','mp4')]:
        result=subprocess.run([str(cli),operation,str(source),'--output',str(folder/('song.'+extension)),'--json'],
                              cwd=folder,env=env,capture_output=True,timeout=240)
        (folder/(operation+'.log')).write_bytes(result.stdout+result.stderr)
        result.check_returncode()
        reports[operation]=json.loads(result.stdout)
    for mode in ('sid','guitar'):
        command=[str(app/('gtracker_editor'+suffix))]
        if mode=='sid': command.append('--sid')
        command+=['--frames','8','--data-dir',str(folder/('data-'+mode))]
        result=subprocess.run(command,cwd=folder,env=env,capture_output=True,timeout=30)
        (folder/(mode+'-editor.log')).write_bytes(result.stdout+result.stderr)
        result.check_returncode()
    after={str(p.relative_to(app)):digest(p) for p in app.rglob('*') if p.is_file()}
    if before!=after: raise RuntimeError('Package installation changed during use')
    report={'source':manifest['source'],'platform':platform.platform(),'archive_sha256':digest(archive),
            'payload_files':len(manifest['files']),'exports':reports,'dummy_editors':['sid','guitar'],
            'installation_unchanged':True,'evidence_directory':str(folder)}
    (folder/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    args=parser.parse_args()
    verify(args.archive.resolve())
