#!/usr/bin/env python3
"""Real packer/renderer and 6502 register checks; py65 is an authoring test dependency."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import statistics
import struct
import subprocess
import sys
import wave

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.local/sid/python'))
from py65.devices.mpu6502 import MPU

def trace(path,ticks):
    b=path.read_bytes();be=lambda p:int.from_bytes(b[p:p+2],'big')
    at=be(6);load=be(8)
    if not load:load=int.from_bytes(b[at:at+2],'little');at+=2
    m=MPU();m.memory[load:load+len(b[at:])]=b[at:]
    def call(address):
        m.pc=address;m.sp=0xfd;m.memory[0x1fe]=0xff;m.memory[0x1ff]=0x02
        for _ in range(100000):
            if m.pc==0x300:return
            m.step()
        raise RuntimeError('Player call exceeded bounded instruction budget')
    m.a=0;call(be(10));out=[]
    for _ in range(ticks):call(be(12));out.append([list(m.memory[0xd400+v*7:0xd407+v*7]) for v in range(3)])
    return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--profile',default='desktop');args=p.parse_args()
    out=ROOT/'.local/sid/verification';out.mkdir(exist_ok=True)
    cli=ROOT/'.local/build'/args.profile/'bin/gtracker_cli.exe'
    template=json.loads((ROOT/'examples/sid/heavy-mellow.sidtrk.json').read_text())
    report={'platform':sys.platform,'checks':[]}
    def run(command,source,target):
        proc=subprocess.run([str(cli),command,str(source),'--output',str(target),'--sid-tools',str(ROOT/'.local/sid/tools'),'--json'],capture_output=True,text=True,encoding='utf-8')
        if proc.returncode:raise RuntimeError(proc.stdout+proc.stderr)
        return json.loads(proc.stdout)
    for speed in (4,6,12):
        song=dict(template);song['timing']={'ticks_per_row':speed,'rows_per_beat':4}
        voices=[]
        for v in range(3):
            events=[dict(row=i*4,type='note',note=60,instrument=i+1) for i in range(11)]
            events += [dict(row=44,type='note',note=69,instrument=5),dict(row=48,type='off'),dict(row=60,type='note',note=65,instrument=5)]
            voices.append(events)
        song['patterns']=[dict(id='all',name='Bank and retriggers',rows=64,voices=voices),dict(id='held',name='Boundary hold',rows=64,voices=[[dict(row=4,type='off')] for _ in range(3)])]
        song['sections']=[dict(id='s',name='TEST',order=[dict(id='o1',pattern_id='all'),dict(id='o2',pattern_id='held'),dict(id='o3',pattern_id='all')])]
        source=out/f'fixture-{speed}.sidtrk.json';source.write_text(json.dumps(song))
        sid=out/f'fixture-{speed}.sid';info=run('sid-export',source,sid)
        again=out/f'again-{speed}.sid';run('sid-export',source,again);assert sid.read_bytes()==again.read_bytes(),'SID nondeterminism'
        ticks=trace(sid,64*speed*3+speed+160)
        first_gate=next(i+1 for i,t in enumerate(ticks) if t[0][4]&1)
        assert first_gate==speed+2,(speed,first_gate)
        for v in range(3):
            for instrument in range(1,12):
                start=speed+3+(instrument-1)*4*speed
                register=ticks[start-1][v]
                assert register[4]!=0,(v,instrument,'missing waveform')
            a4=ticks[speed+3+44*speed-1][v]
            frequency=(a4[0]+a4[1]*256)*985248/(2**24)
            assert abs(frequency-440)<0.1,(v,speed,frequency)
            # A warm lead after all three drums returns to the authored pitch.
            assert a4[4]==0x11 and a4[5]==0x38 and a4[6]==0x96
            # Boundary HOLD must not trigger the hard restart ADSR.
            boundary=64*speed+speed+3
            assert all(ticks[t][v][4]&1 for t in range(boundary-3,boundary+speed))
            assert all(not (t[v][4]&1) for t in ticks[-120:]),'ending gate stuck'
        report['checks'].append({'speed':speed,'all_voices_and_presets':True,'first_gate_call':first_gate,'pitched_tick_call':speed+3,'a4_hz':frequency,'ending_gates_released':True,'deterministic_sid':True,'export':info})
    source=out/'fixture-6.sidtrk.json'
    wav=out/'fixture.wav';info=run('sid-render',source,wav)
    with wave.open(str(wav)) as w:
        assert w.getframerate()==48000 and w.getnchannels()==1 and w.getsampwidth()==2
        pcm=struct.unpack('<%dh'%w.getnframes(),w.readframes(w.getnframes()))
        assert w.getnframes()==round(info['rendered_seconds']*48000)
        tail=pcm[-24000:];dc=statistics.mean(tail)/32768;rms=math.sqrt(statistics.mean([(x/32768-dc)**2 for x in tail]))
        assert max(map(abs,pcm))>1000 and max(map(abs,pcm))<32767
        assert rms<0.005,('tail above -46dBFS',rms)
        report['pcm']={'frames':len(pcm),'peak':max(map(abs,pcm))/32768,'tail_dc':dc,'tail_dc_removed_rms':rms,'quietness_limit':0.005,'sha256':hashlib.sha256(wav.read_bytes()).hexdigest()}
    second=out/'fixture-repeat.wav';run('sid-render',source,second);assert second.read_bytes()==wav.read_bytes(),'PCM nondeterminism'
    report['repeatable_pcm']=True
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
