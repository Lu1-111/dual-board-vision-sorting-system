"""Native production supervisor with test-only transport paths; no board access."""
import copy, hashlib, json, os, socket, subprocess, sys, tempfile, threading, time
from pathlib import Path

BIN, UI = sys.argv[1:3]
BASE=[500,1495.7,539.2,500,1252.6,1656]
MAP='faf77b03777460fd3f10132c520221664204a884c7cc7ff3dee3f647e8340cd0'
TRACK_RECOVERY_CASES=['target_missing','target_replaced','outside_region','target_stale']
RECOVERY_CASES=TRACK_RECOVERY_CASES+['worker_vision','prepare_target_lost']
PAUSE_CASES=['boot_paused','pause_waiting','pause_feeding','pause_classifying','pause_during_action']
STATUS_TIMING_CASES=['status_late_grasp','status_late_return','status_silent_completion','status_bad_reply']
def mono(): return int(time.monotonic()*1000)
def dump(p,o):
    t=Path(str(p)+'.new');t.write_text(json.dumps(o));t.replace(p)
def wait(pred, seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if pred(): return
        time.sleep(.025)
    raise AssertionError('timeout')
def action_files():
    root=Path('/home/uisrc/qt_play/actions/fixed_pick_20261021/actions');root.mkdir(parents=True,exist_ok=True)
    out={}
    for label,name in [('NORMAL','perfect_selection.json'),('DEFECT','defect_selection.json')]:
        p=root/name
        # Isolated VM fixtures, not physical motion actions.
        dump(p,dict(version=1,angleMappingId=MAP,frames=[dict(anglesDeg=[0,89.65,3.53,0,67.76,104.08],durationMs=20,holdMs=0)]*7))
        out[label]=dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    return out

class Fixture:
    def __init__(self,case):
        self.temp=tempfile.TemporaryDirectory(prefix='as-');self.r=Path(self.temp.name);self.case=case;self.stop=False;self.requests=[];self.request_times=[];self.object=False;self.seq=1;self.trackid='1';self.label='NORMAL';self.stale=False;self.partial=False
        self.pending_v=None;self.prev_phase=None;self.classify_start=None;self.feed_start=None;self.disturbance_seen=False;self.disturbance_released=False;self.action_finish_at=None;self.status_delay_sent=False;self.bad_status_sent=False
        self.arm=dict(ok=True,automationProtocol=1,consoleSessionId='test-ui-1',commandGeneration='0',lastAutomationRequest='',actionJobId=0,actionJobState='idle',actionFile='',loadedActionSha256='',enabled=False,debug=False,mode='simulation',connected=True,hardwareOutputActive=False,hardwareStatusAvailable=True,holdingAfterTimeout=False,moving=False,actionPlaying=False,returningBeforeOff=False,initialPoseAcknowledged=False,error='',uiError='',sortJobId=0,sortJobState='idle',visionReference=dict(axes=[[i,1,0,180,500,2499.2,0] for i in range(6)],pulses=BASE,linksCm=[2.89,10.43,8.9,17.7]),targetPulses=BASE,commandedPulses=BASE)
        if case=='latched':self.arm.update(holdingAfterTimeout=True,hardwareOutputActive=True)
        if case in ['preconnected_empty','preconnected_start']:self.arm.update(mode='hardware',holdingAfterTimeout=True)
        self.actions=action_files();self.socket=socket.socket(socket.AF_UNIX);self.socket.bind(str(self.r/'arm.sock'));self.socket.listen();self.socket.settimeout(.1)
        worker=self.r/'worker.py';worker.write_text('''#!/usr/bin/env python3
import json,time,pathlib,sys,signal
r=pathlib.Path(__file__).parent
case=json.loads((r/'worker-scenario.json').read_text())['case']
def terminated(*args):
    (r/'worker-terminated').write_text('terminated')
    sys.exit(0)
signal.signal(signal.SIGTERM,terminated)
counter=r/'worker-runs'
runs=int(counter.read_text())+1 if counter.exists() else 1
counter.write_text(str(runs))
time.sleep(1.2 if case=='pause_feeding' and runs==1 else (.8 if case.startswith('transit_') else .65))
if runs==1 and case in ['worker_vision','worker_control']:
    visual=case=='worker_vision'
    (r/'worker-failed-once').write_text(case)
    print(json.dumps(dict(status='stopped_failure',failure_class='vision' if visual else 'control',recoverable=visual,reason='fixture target unavailable' if visual else 'fixture hardware fault',stop_verified=visual,stop_readback=dict(status=0 if visual else 256,active_duty=0,encoder_errors=0))))
    sys.exit(1)
t=json.loads((r/'track.json').read_text())
print(json.dumps(dict(status='success',stop_verified=True,source_session=t['source_session'],track_id=t['objects'][0]['track_id'],frame_sequence=t['frame_sequence'],center_xy=t['objects'][0]['center_xy'],capture_fence_monotonic_ms=int(time.monotonic()*1000))))
''');worker.chmod(0o700)
        dump(self.r/'worker-scenario.json',dict(case=case))
        dump(self.r/'belt.json',dict(stopped=True))
        if case!='boot_paused':self.control(True)
        dump(self.r/'config.json',dict(schema=1,startup_pose_confirmed=True,test_root=str(self.r),test_socket=str(self.r/'arm.sock'),test_worker=str(worker),worker_sha256=hashlib.sha256(worker.read_bytes()).hexdigest(),mapping_id=MAP,actions=self.actions))
        self.y=620 if case=='wide_y' else (510 if case=='resume_ready' else 543)
        args=[BIN,'--boot',str(self.r/'config.json')]
        if case.startswith('resume_'):
            self.arm.update(mode='hardware',enabled=True,debug=True,hardwareOutputActive=True,initialPoseAcknowledged=True,actionFile=self.actions['NORMAL']['path'],loadedActionSha256=self.actions['NORMAL']['sha256'],actionFrameCount=7)
            self.arm['visionReference']['debug']=True
            if case=='resume_latched':self.arm['holdingAfterTimeout']=True
            keys=['consoleSessionId','commandGeneration','actionJobId','actionJobState','actionFile','loadedActionSha256','mode','enabled','debug','holdingAfterTimeout','visionReference']
            expected={k:self.arm[k] for k in keys}
            if case=='resume_changed':expected['commandGeneration']='9'
            if case=='resume_empty':expected={}
            dump(self.r/'expected.json',expected)
            args=[BIN,'--resume-ready',str(self.r/'config.json'),str(self.r/'expected.json')]
        self.threads=[threading.Thread(target=self.server),threading.Thread(target=self.publish)]
        for t in self.threads:t.start()
        time.sleep(.08)
        self.proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    def phase(self):
        try:return json.loads((self.r/'state.json').read_text())['state']
        except:return ''
    def control(self,enabled):dump(self.r/'control.json',dict(enabled=enabled))
    def rpc_timings(self):
        return [json.loads(line) for line in (self.r/'rpc-timing.jsonl').read_text().splitlines()]
    def server(self):
        while not self.stop:
            try:s,_=self.socket.accept()
            except socket.timeout:continue
            with s:
                try:
                    data=b''
                    while b'\n' not in data:data+=s.recv(4096)
                    c=json.loads(data);kind=c['action'];self.requests.append(kind);self.request_times.append((kind,mono()))
                    if kind=='status' and self.arm['actionJobId']>0:
                        if self.case=='status_silent_completion':time.sleep(.65);continue
                        delayed_phase=(self.case=='status_late_grasp' and self.phase()=='SORTING') or (self.case=='status_late_return' and self.phase()=='RETURNING' and 'debug-pulses' in self.requests)
                        if delayed_phase and not self.status_delay_sent:self.status_delay_sent=True;time.sleep(.65)
                        if self.case=='status_bad_reply' and not self.bad_status_sent:
                            self.bad_status_sent=True;s.sendall(b'{invalid json}\n');continue
                    if kind!='status':
                        self.arm['commandGeneration']=str(int(self.arm['commandGeneration'])+1);self.arm['lastAutomationRequest']=c.get('requestId','')
                        if kind=='connect':
                            self.arm['mode']='hardware';self.arm['holdingAfterTimeout']=True
                            if self.case=='existing_output':self.arm['hardwareOutputActive']=True
                            if self.case=='prepare_target_lost':self.object=False;self.disturbance_seen=True
                        if kind=='off':self.arm.update(holdingAfterTimeout=False,hardwareOutputActive=False,enabled=False)
                        if kind=='debug-enable':self.arm.update(enabled=True,debug=True,hardwareOutputActive=True,initialPoseAcknowledged=True);self.arm['visionReference']['debug']=True
                        if kind=='debug-pulses':self.arm.update(targetPulses=c['pulses'],commandedPulses=c['pulses'])
                        if kind=='load-actions':self.arm.update(actionFile=c['path'],loadedActionSha256=hashlib.sha256(Path(c['path']).read_bytes()).hexdigest(),actionFrameCount=7)
                        if kind=='play-actions':
                            self.arm['actionJobId']+=1;self.arm.update(actionJobState='running',moving=True,actionPlaying=True)
                            if self.case=='ack_lost':time.sleep(.65)
                            elif self.case in ['pause_during_action','status_late_grasp','status_silent_completion']:self.action_finish_at=mono()+900
                            else:self.arm.update(actionJobState='completed',moving=False,actionPlaying=False);self.object=False
                        if kind=='stop':self.arm.update(moving=False,actionPlaying=False,actionJobState='interrupted')
                    s.sendall(json.dumps(self.arm).encode()+b'\n')
                except (BrokenPipeError,ConnectionResetError):pass
    def publish(self):
        while not self.stop:
            stamp=mono();self.seq+=2 if self.case=='transit_interpolated' else 1
            if self.action_finish_at is not None and stamp>=self.action_finish_at:
                end_pose=[500,1495.7,876.3,500,923.4,1656]
                self.arm.update(actionJobState='completed',moving=False,actionPlaying=False,targetPulses=end_pose,commandedPulses=end_pose);self.action_finish_at=None
                if self.case!='pause_during_action':self.object=False
            x,y,trackid=1227,self.y,self.trackid
            phase=self.phase();
            if phase=='FEEDING' and self.prev_phase!='FEEDING':self.feed_start=stamp;self.pending_v=None;self.classify_start=None
            if phase=='CLASSIFYING' and self.classify_start is None:self.classify_start=stamp
            self.prev_phase=phase
            if self.case.startswith('transit_') and phase=='FEEDING' and stamp-self.feed_start<500:x=900
            after_arrival=phase in ['CLASSIFYING','SORTING']
            if self.case in TRACK_RECOVERY_CASES and after_arrival and not self.disturbance_released:
                self.disturbance_seen=True
            if self.case=='worker_vision' and (self.r/'worker-failed-once').exists():
                self.disturbance_seen=True
            disturbed=self.disturbance_seen and not self.disturbance_released
            present=self.object
            if after_arrival:
                if self.case=='center_shift':y+=18
            if disturbed:
                if self.case=='target_replaced':trackid='2'
                if self.case=='outside_region':x=1254
                if self.case in ['target_missing','worker_vision','prepare_target_lost'] or phase=='WAIT_OBJECT':present=False
            obj=dict(center_xy=[x,y],bbox_xyxy=[x-30,y-30,x+30,y+30],track_id=trackid,partial=self.partial,ambiguous=False)
            t=dict(schema=1,status='ok',source='imx415',source_session='camera1',frame_sequence=str(self.seq),published_monotonic_ms=stamp,source_frame_age_ms=1000 if self.stale or (disturbed and self.case=='target_stale') else 0,width=1920,height=1080,purpose='localization_only',coordinate_frame='camera_pixel',localization_width=320,sampling_step_px=[6,6],objects=[obj] if present else [])
            v=copy.deepcopy(t);v.update(model='V28',threshold=.88,frame_path=str(self.r/('frame_camera1_'+str(self.seq)+'.jpg')))
            if self.case=='transit_interpolated':
                v['frame_sequence']=str(self.seq-1);v['source_frame_age_ms']=25;v['frame_path']=str(self.r/('frame_camera1_'+str(self.seq-1)+'.jpg'))
            if v['objects']:v['objects'][0].update(label=self.label,score=.98 if self.label=='DEFECT' else .2)
            # Minimal JPEG SOF carries required dimensions, used only for binding tests.
            Path(v['frame_path']).write_bytes(bytes.fromhex('ffd8 ffc0 000b 08 0438 0780 01 011100 ffd9'))
            dump(self.r/'track.json',t)
            if phase=='FEEDING' and stamp-self.feed_start>=300:
                if self.pending_v is None:self.pending_v=copy.deepcopy(v)
                if self.case in ['transit_normal','transit_defect','transit_wrong_session','transit_wrong_position'] and stamp-self.feed_start<400:
                    if self.case=='transit_wrong_session':v['source_session']='other-camera'
                    if self.case=='transit_wrong_position' and v['objects']:v['objects'][0]['center_xy'][0]-=100
                    dump(self.r/'vision.json',v)
            if phase=='CLASSIFYING' and stamp-self.classify_start>=250:
                if self.case.startswith('transit_post_stop') or self.case=='transit_timeout':
                    if self.case in ['transit_timeout','transit_post_stop_wrong_session']:v['source_session']='other-camera'
                    if self.case=='transit_post_stop_wrong_position' and v['objects']:v['objects'][0]['center_xy'][0]-=100
                    dump(self.r/'vision.json',v)
                elif self.pending_v is not None and self.case not in ['transit_wrong_session','transit_wrong_position']:
                    result=copy.deepcopy(self.pending_v)
                    result['source_frame_age_ms']+=stamp-result['published_monotonic_ms'];result['published_monotonic_ms']=stamp
                    if self.case=='transit_stale':result['source_frame_age_ms']+=31000
                    dump(self.r/'vision.json',result)
            time.sleep(.05)
    def end(self):
        if self.proc.poll() is None:self.proc.terminate()
        self.proc.wait(timeout=5);err=self.proc.stderr.read().decode();self.stop=True
        for t in self.threads:t.join(2)
        self.socket.close()
        return err

passed=[]
for case in ['transit_timeout','transit_interpolated','transit_normal','transit_defect','transit_late_normal','transit_late_defect','transit_post_stop','transit_post_stop_defect','transit_post_stop_wrong_session','transit_post_stop_wrong_position','transit_stale','transit_wrong_session','transit_wrong_position','empty','stale','partial','latched','existing_output','preconnected_empty','preconnected_start','two_cycles','ack_lost','same_boot','wide_y','resume_ready','resume_changed','resume_latched','resume_empty','center_shift']+RECOVERY_CASES+['worker_control']+PAUSE_CASES+STATUS_TIMING_CASES:
    if os.environ.get('AUTOSORT_TEST_CASES') and case not in os.environ['AUTOSORT_TEST_CASES'].split(','):continue
    f=Fixture(case)
    try:
        if case.startswith('transit_'):
            f.label='DEFECT' if case in ['transit_defect','transit_late_defect','transit_post_stop_defect'] else 'NORMAL';f.object=True
            if case in ['transit_interpolated','transit_normal','transit_defect','transit_late_normal','transit_late_defect']:
                wait(lambda:f.phase()=='WAIT_CLEAR')
                evidence=json.loads(next(f.r.glob('cycle-*.json')).read_text())
                assert evidence['decision_policy']=='matched_in_transit_frame',evidence
                assert evidence['decision']==f.label
                if case=='transit_interpolated':assert evidence['association_tracking']['association']=='bounded_sequence_interpolation'
                else:assert evidence['association_tracking']['objects'][0]['center_xy'][0]==900
                if 'late' in case or case=='transit_interpolated':
                    assert evidence['classification'][0]['published_monotonic_ms']>evidence['arrival']['capture_fence_monotonic_ms']
                    assert evidence['ready_monotonic_ms']-evidence['classification'][0]['published_monotonic_ms']<600
                else:assert evidence['classification'][0]['published_monotonic_ms']<=evidence['arrival']['capture_fence_monotonic_ms']
            elif case in ['transit_post_stop','transit_post_stop_defect']:
                wait(lambda:f.phase()=='WAIT_CLEAR')
                evidence=json.loads(next(f.r.glob('cycle-*.json')).read_text())
                assert evidence['decision_policy']=='valid_current_object_after_arrival' and evidence['decision']==f.label,evidence
                assert int(evidence['classification'][0]['frame_sequence'])>int(evidence['arrival']['frame_sequence'])
                assert evidence['ready_monotonic_ms']-evidence['classification'][0]['published_monotonic_ms']<600
                assert f.requests.count('play-actions')==1 and int((f.r/'worker-runs').read_text())==1
            elif case=='transit_timeout':
                wait(lambda:f.phase().startswith('FAULT'),seconds=36)
                assert f.requests.count('play-actions')==0 and int((f.r/'worker-runs').read_text())==1
                assert 'no valid current-object' in json.loads((f.r/'state.json').read_text())['reason']
            else:
                wait(lambda:f.phase()=='CLASSIFYING');time.sleep(.7)
                assert 'play-actions' not in f.requests
        elif case in ['empty','stale','partial']:
            f.object=case!='empty';f.stale=case=='stale';f.partial=case=='partial';time.sleep(1.1)
            assert all(x=='status' for x in f.requests),f.requests
        elif case=='latched':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'));assert all(x=='status' for x in f.requests)
        elif case=='existing_output':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'));assert 'off' not in f.requests and 'debug-enable' not in f.requests
        elif case=='preconnected_empty':
            wait(lambda:f.phase()=='WAIT_OBJECT');time.sleep(1.1)
            assert f.proc.poll() is None and f.phase()=='WAIT_OBJECT'
            assert all(kind=='status' for kind in f.requests),f.requests
            assert f.arm['mode']=='hardware' and f.arm['holdingAfterTimeout'] and not f.arm['enabled'] and not f.arm['hardwareOutputActive'],f.arm
        elif case=='preconnected_start':
            f.object=True;wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.requests.count('connect')==0 and f.requests.count('off')==1 and f.requests.count('debug-enable')==1,f.requests
            assert f.requests.count('play-actions')==1 and 'stop' not in f.requests,f.requests
            assert len(list(f.r.glob('completed-*.json')))==1 and not f.arm['holdingAfterTimeout']
        elif case=='two_cycles':
            f.object=True;wait(lambda:f.phase()=='WAIT_CLEAR');wait(lambda:f.phase()=='WAIT_OBJECT');f.trackid='2';f.label='DEFECT';f.object=True;wait(lambda:f.requests.count('play-actions')==2);wait(lambda:f.phase()=='WAIT_CLEAR');assert f.requests.count('debug-enable')==1 and f.requests.count('debug-pulses')==2
            assert len(list(f.r.glob('completed-*.json')))==2
            assert all(len(json.loads(p.read_text())['classification'])==1 for p in f.r.glob('cycle-*.json'))
            assert f.requests.count('off')==1 and f.requests.index('connect')<f.requests.index('off')<f.requests.index('debug-enable')
        elif case in ['wide_y','resume_ready','center_shift']:
            wait(lambda:f.phase()=='WAIT_OBJECT');time.sleep(.6)
            assert all(x=='status' for x in f.requests),f.requests
            f.object=True;wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.requests.count('play-actions')==1 and len(list(f.r.glob('completed-*.json')))==1
            if case=='resume_ready':assert not any(x in f.requests for x in ['connect','off','debug-enable']),f.requests
        elif case in RECOVERY_CASES:
            f.object=True;wait(lambda:f.disturbance_seen);wait(lambda:f.phase()=='WAIT_OBJECT')
            time.sleep(.65)
            assert f.proc.poll() is None and f.phase()=='WAIT_OBJECT',f.phase()
            assert f.arm['enabled'] and f.arm['hardwareOutputActive'] and not f.arm['moving'] and not f.arm['actionPlaying'],f.arm
            assert f.arm['targetPulses']==BASE and f.arm['commandedPulses']==BASE
            assert 'play-actions' not in f.requests and 'stop' not in f.requests,f.requests
            assert all(f.requests.count(kind)==1 for kind in ['connect','off','debug-enable']),f.requests
            released=mono();f.trackid='2';f.object=True;f.disturbance_released=True
            wait(lambda:f.phase()=='WAIT_CLEAR')
            plays=[stamp for kind,stamp in f.request_times if kind=='play-actions']
            assert len(plays)==1 and plays[0]-released>=500,plays
            assert len(list(f.r.glob('completed-*.json')))==1
            assert all(f.requests.count(kind)==1 for kind in ['connect','off','debug-enable']),f.requests
            if case=='worker_vision':assert int((f.r/'worker-runs').read_text())==2
        elif case=='worker_control':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'))
            assert 'play-actions' not in f.requests and int((f.r/'worker-runs').read_text())==1,f.requests
        elif case=='boot_paused':
            f.object=True;wait(lambda:f.phase()=='PAUSED');time.sleep(.65)
            assert all(kind=='status' for kind in f.requests) and not f.arm['enabled'],f.requests
            f.control(False);time.sleep(.25)
            assert f.phase()=='PAUSED' and all(kind=='status' for kind in f.requests),f.requests
            f.control(True);wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.requests.count('play-actions')==1
        elif case=='pause_waiting':
            f.object=True;wait(lambda:f.phase()=='WAIT_CLEAR');wait(lambda:f.phase()=='WAIT_OBJECT')
            f.control(False);wait(lambda:f.phase()=='PAUSED')
            f.trackid='2';f.object=True;time.sleep(.65)
            assert f.phase()=='PAUSED' and f.requests.count('play-actions')==1,f.requests
            assert f.arm['enabled'] and f.arm['hardwareOutputActive'] and f.arm['targetPulses']==BASE and f.arm['commandedPulses']==BASE
            assert all(f.requests.count(kind)==1 for kind in ['connect','off','debug-enable']) and 'stop' not in f.requests,f.requests
            f.control(True);wait(lambda:f.requests.count('play-actions')==2);wait(lambda:f.phase()=='WAIT_CLEAR')
        elif case in ['pause_feeding','pause_classifying']:
            f.object=True
            if case=='pause_feeding':wait(lambda:(f.r/'worker-runs').exists())
            else:wait(lambda:f.phase()=='CLASSIFYING')
            f.control(False);wait(lambda:f.phase()=='PAUSED');time.sleep(.65)
            assert f.proc.poll() is None and f.phase()=='PAUSED' and 'play-actions' not in f.requests,f.requests
            assert f.arm['enabled'] and f.arm['hardwareOutputActive'] and not f.arm['moving'] and f.arm['targetPulses']==BASE
            assert all(f.requests.count(kind)==1 for kind in ['connect','off','debug-enable']) and 'stop' not in f.requests,f.requests
            if case=='pause_feeding':assert (f.r/'worker-terminated').exists()
            f.control(True);wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.requests.count('play-actions')==1 and int((f.r/'worker-runs').read_text())==2,f.requests
        elif case=='pause_during_action':
            f.object=True;wait(lambda:f.arm['actionPlaying']);f.control(False)
            wait(lambda:f.phase()=='PAUSED')
            assert f.arm['actionJobState']=='completed' and not f.arm['actionPlaying'] and not f.arm['moving'],f.arm
            assert f.arm['enabled'] and f.arm['hardwareOutputActive'] and f.arm['targetPulses']==BASE and f.arm['commandedPulses']==BASE
            assert f.requests.count('play-actions')==1 and f.requests.count('debug-pulses')==1 and 'stop' not in f.requests,f.requests
            time.sleep(.65);assert f.phase()=='PAUSED' and f.requests.count('play-actions')==1
            f.control(True);wait(lambda:f.phase()=='WAIT_CLEAR');time.sleep(.65)
            assert f.phase()=='WAIT_CLEAR' and f.requests.count('play-actions')==1,f.requests
            f.object=False;wait(lambda:f.phase()=='WAIT_OBJECT')
            f.trackid='2';f.object=True;wait(lambda:f.arm['actionJobId']==2 and f.arm['actionJobState']=='completed');wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.requests.count('play-actions')==2 and f.requests.count('debug-pulses')==2
            assert all(f.requests.count(kind)==1 for kind in ['connect','off','debug-enable']),f.requests
        elif case in ['status_late_grasp','status_late_return']:
            f.object=True;wait(lambda:f.phase()=='WAIT_CLEAR')
            assert f.status_delay_sent and f.requests.count('play-actions')==1 and f.requests.count('debug-pulses')==1 and 'stop' not in f.requests,f.requests
            assert f.arm['actionJobState']=='completed' and f.arm['targetPulses']==BASE and f.arm['commandedPulses']==BASE
            records=f.rpc_timings();timeouts=[row for row in records if row['action']=='status' and row['result']=='timeout']
            assert timeouts and all(row['timeout_stage']=='reply' and row['elapsed_ms']>=500 and row['connect_ms']>=0 for row in timeouts),records
            phase='SORTING' if case=='status_late_grasp' else 'RETURNING'
            assert any(row['phase']==phase for row in timeouts),timeouts
            assert len([row for row in records if row['action']=='play-actions'])==1
            assert all('requestId' in row and 'reply_complete' in row for row in records)
        elif case=='status_silent_completion':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'),seconds=27)
            elapsed=mono()-next(stamp for kind,stamp in f.request_times if kind=='play-actions')
            state=json.loads((f.r/'state.json').read_text())
            assert state['reason']=='arm completion timeout' and 20000<=elapsed<25000,(state,elapsed)
            assert f.requests.count('play-actions')==1 and 'debug-pulses' not in f.requests,f.requests
            timeouts=[row for row in f.rpc_timings() if row['action']=='status' and row['result']=='timeout']
            assert len(timeouts)>=2 and all(row['timeout_stage']=='reply' for row in timeouts),timeouts
        elif case=='status_bad_reply':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'))
            state=json.loads((f.r/'state.json').read_text())
            assert state['reason']=='bad arm JSON' and f.requests.count('play-actions')==1,state
            failures=[row for row in f.rpc_timings() if row['action']=='status' and row['result']=='error']
            assert any(row['stage']=='decode' and row['reply_complete'] and not row['timeout_stage'] for row in failures),failures
        elif case=='resume_empty':
            wait(lambda:f.proc.poll() is not None);assert f.proc.returncode!=0 and not f.requests,f.requests
        elif case in ['resume_changed','resume_latched']:
            wait(lambda:f.phase().startswith('FAULT'));assert all(x=='status' for x in f.requests),f.requests
        elif case=='ack_lost':
            f.object=True;wait(lambda:f.phase().startswith('FAULT'));assert f.requests.count('play-actions')==1 and f.requests.count('stop')==1,f.requests
            plays=[row for row in f.rpc_timings() if row['action']=='play-actions']
            assert len(plays)==1 and plays[0]['result']=='timeout' and plays[0]['timeout_stage']=='reply' and plays[0]['requestId'],plays
        else:
            wait(lambda:f.phase()=='WAIT_OBJECT');f.end();p=subprocess.run([BIN,'--boot',str(f.r/'config.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert p.returncode!=0 and b'same-boot restart blocked' in p.stderr;passed.append(case);continue
        passed.append(case)
    except:
        print('FAILED',case,'phase',f.phase(),'requests',f.requests,'root',f.r,flush=True);raise
    finally:
        if not f.stop:print('STOP',case,f.end(),flush=True)
print('RUNTIME_PASS',json.dumps(passed),flush=True)
