"""Verify the actual persistent SID helper against the offline renderer."""
import os
import json
import pathlib
import struct
import subprocess
import time
import wave

ROOT=pathlib.Path(__file__).resolve().parents[1]
folder=ROOT/'.local/sid/stream-verification'
folder.mkdir(parents=True,exist_ok=True)
executable_suffix='.exe' if os.name=='nt' else ''
cli=ROOT/('.local/build/desktop/bin/gtracker_cli'+executable_suffix)
tools=ROOT/'.local/sid/tools'
score=json.loads((ROOT/'examples/sid/heavy-mellow.sidtrk.json').read_text())
score['sections']=score['sections'][:1]
score['sections'][0]['order']=score['sections'][0]['order'][:1]
source=folder/'score.sidtrk.json'
source.write_text(json.dumps(score),encoding='utf-8')
for operation,suffix in [('sid-export','sid'),('sid-render','wav')]:
    subprocess.run([str(cli),operation,str(source),'--sid-tools',str(tools),'--output',str(folder/('song.'+suffix)),'--json'],check=True,capture_output=True)
with wave.open(str(folder/'song.wav')) as wav:
    expected=wav.readframes(wav.getnframes())
started=time.perf_counter()
process=subprocess.Popen([str(tools/('gtracker_sidstream'+executable_suffix))],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
def word():
    value=process.stdout.read(4)
    assert len(value)==4,'helper terminated'
    return struct.unpack('<I',value)[0]
def send(*values):
    process.stdin.write(struct.pack('<'+'I'*len(values),*values))
    process.stdin.flush()
try:
    assert word()==0x47545331
    startup_ms=(time.perf_counter()-started)*1000
    sid=(folder/'song.sid').read_bytes()
    timings=[]
    differences=[]
    for repeat in range(2):
        started=time.perf_counter()
        send(1,len(sid));process.stdin.write(sid);process.stdin.flush();assert word()==1
        pcm=bytearray()
        while len(pcm)<len(expected):
            count=min(512,(len(expected)-len(pcm))//2)
            send(2,count);assert word()==count
            pcm.extend(process.stdout.read(count*2))
            if len(pcm)==1024:timings.append((time.perf_counter()-started)*1000)
        a=struct.unpack('<'+'h'*(len(pcm)//2),pcm);b=struct.unpack('<'+'h'*(len(expected)//2),expected)
        delta=max(abs(x-y) for x,y in zip(a,b));differences.append(delta)
        assert delta <= 4,'streamed PCM exceeds reload tolerance'
    result={'cold_helper_ready_ms':startup_ms,'warm_load_first_512_frames_ms':timings,'frames_compared':len(expected)//2,'max_pcm_difference':differences,'repeated_loads':2}
    (folder/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
finally:
    process.kill();process.wait()
