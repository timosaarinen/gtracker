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
import stat
import uuid
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def package_path(name):
    """Require a portable, canonical relative filename on both host platforms."""
    path=PurePosixPath(name)
    if (not name or path.is_absolute() or path.as_posix()!=name or
        any(part in ('.','..') or ':' in part or '\\' in part or
            part.endswith((' ','.')) for part in path.parts)):
        raise ValueError('Unsafe package path')
    return path

def extract_verified(archive, folder):
    # Validate the complete archive before writing files or executing anything.
    with zipfile.ZipFile(archive) as zipped:
        entries=zipped.infolist()
        names=[entry.filename for entry in entries]
        if len({name.casefold() for name in names})!=len(names):
            raise ValueError('Duplicate package paths')
        for entry in entries:
            path=package_path(entry.filename)
            kind=stat.S_IFMT(entry.external_attr>>16)
            if len(path.parts)<2 or path.parts[0]!='gtracker' or kind not in (0,stat.S_IFREG):
                raise ValueError('Unsafe archive path or file type')
        manifest=json.loads(zipped.read('gtracker/package.json'))
        if (manifest.get('schema')!='gtracker.developer-package' or
            manifest.get('version')!=1 or manifest.get('profile')!='desktop'):
            raise ValueError('Expected a version 1 desktop package')
        payloads={}
        for entry in manifest['files']:
            name=package_path(entry['path']).as_posix()
            if name=='package.json' or name.casefold() in payloads:
                raise ValueError('Duplicate or reserved manifest path')
            payloads[name.casefold()]='gtracker/'+name
            data=zipped.read('gtracker/'+name)
            if hashlib.sha256(data).hexdigest()!=entry['sha256'] or len(data)!=entry['size_bytes']:
                raise ValueError('Payload mismatch: '+name)
        if set(names)!=set(payloads.values())|{'gtracker/package.json'}:
            raise ValueError('Archive and manifest payloads differ')
        for entry in entries:
            target=folder.joinpath(*PurePosixPath(entry.filename).parts)
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(zipped.read(entry))
            if os.name!='nt': target.chmod((entry.external_attr>>16)&0o777)
    return manifest

def verify(archive):
    folder=ROOT/'.local/package-checks'/('candidate Ω '+uuid.uuid4().hex[:12])
    folder.mkdir(parents=True)
    manifest=extract_verified(archive,folder)
    app=folder/'gtracker'
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
