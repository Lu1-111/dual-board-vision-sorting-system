"""PC orchestration with an authenticated Paramiko `transport` supplied by board task runner.

Real IMX415 capture + board NPU + isolated simulated arm. Never permits hardware.
Settings/evidence directory and class are supplied in task. Camera stays open.
"""
import base64
import hashlib
import json
import pathlib
import shlex
import sys
import time
sys.path.insert(0, str(pathlib.Path(task['tools_dir'])))
import fixed_camera_sort as sort

cfg=sort.load(task['config'])
output=pathlib.Path(task['output']); output.mkdir(parents=True,exist_ok=True)
arm_bin='/home/uisrc/qt_play/fixed-camera-20261031/hdmi_ui'
def shell(command):
    c=transport.open_session(); c.settimeout(30); c.exec_command(command)
    out=c.makefile().read().decode(errors='replace'); err=c.makefile_stderr().read().decode(errors='replace')
    code=c.recv_exit_status(); c.close()
    if code: raise RuntimeError('Board command failed: '+err+out[-1500:])
    return out
def arm(command):
    result=json.loads(shell('HDMI_UI_ARM_SOCKET=/tmp/fixed-sort-arm.sock LD_LIBRARY_PATH=/home/uisrc/qt_play/qt_lib/lib '+arm_bin+' --arm-command '+shlex.quote(json.dumps(command))))
    if not result.get('ok'): raise RuntimeError(result.get('error',str(result)))
    if result.get('mode')!='simulation': raise RuntimeError('Only simulation is allowed in this runner')
    return result
state=arm({'action':'status'})
if state['moving'] or state['actionPlaying']: raise RuntimeError('Simulation is busy')
bridge=transport.open_session(); bridge.settimeout(10)
bridge.exec_command('LD_LIBRARY_PATH=/home/uisrc/camera_dataset_app/qt_lib/lib /home/uisrc/camera_dataset_app/camera_dataset_app --serial-bridge')
reader=bridge.makefile('r'); writer=bridge.makefile_stdin('w')
def reply():
    while True:
        line=reader.readline()
        if not line: raise RuntimeError('Camera bridge closed')
        if line.startswith('@CAMERA@'): return json.loads(line[8:])
reply()
def camera(command):
    writer.write(json.dumps(command)+'\n'); writer.flush(); result=reply()
    if not result.get('ok'): raise RuntimeError(result)
    return result
evidence=[]; job=None
try:
    previous=None
    for index in range(3):
        state=camera({'action':'status'})
        if state['pending'] or state['busy']: raise RuntimeError('Deliver the existing camera photo first')
        camera({'action':'capture'})
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            state=camera({'action':'status'})
            if state.get('pending'): break
            time.sleep(.05)
        else: raise RuntimeError('No fresh photo')
        meta=state['photo']; data=bytearray()
        if meta['source']!='imx415': raise RuntimeError('Real IMX415 photo required')
        while len(data)<meta['size']:
            part=camera({'action':'image_chunk','photo_id':meta['photo_id'],'offset':len(data),'size':8192})
            block=base64.b64decode(part['data'],validate=True)
            if not block: raise RuntimeError('Empty image chunk')
            data.extend(block)
        if len(data)!=meta['size'] or hashlib.sha256(data).hexdigest()!=meta['sha256']: raise RuntimeError('Photo integrity check failed')
        photo=output/('capture-%d.jpg'%index); photo.write_bytes(data)
        camera({'action':'ack','photo_id':meta['photo_id'],'sha256':meta['sha256']})
        with transport.open_sftp_client() as sftp: sftp.put(str(photo),'/tmp/fixed-sort-input.jpg')
        raw=shell('/home/uisrc/cube_detection/current/run_image.sh /tmp/fixed-sort-input.jpg /tmp/fixed-sort-detected.jpg')
        frame=json.loads(next(line for line in reversed(raw.splitlines()) if line.startswith('{"frame"')))
        (output/('detection-%d.json'%index)).write_text(json.dumps(frame,indent=2))
        print('DETECTIONS',json.dumps(frame['objects']),flush=True)
        obj,xy=sort.target(frame,cfg,task.get('class_name','blue'))
        if previous and (meta['frame_sequence']==previous[0] or ((xy[0]-previous[1][0])**2+(xy[1]-previous[1][1])**2)**.5>cfg['stability_cm']):
            raise RuntimeError('Require distinct frames and a stationary target')
        previous=(meta['frame_sequence'],xy)
        evidence.append(dict(photo=meta,detections=frame,target=obj,xy_cm=xy))
        (output/'observations.json').write_text(json.dumps(evidence,indent=2))
        print('FRAME',index,obj['class_name'],obj['detection_confidence'],'XY',xy,flush=True)
    state=arm({'action':'debug-enable','confirm':True,'baseline':[1000,1500,833.2,999.8,1832.8,1499.6]})
    cmd=sort.command(cfg,state,obj,xy)
    preview=arm(cmd)
    (output/'preview.json').write_text(json.dumps(preview,indent=2))
    cmd['action']='fixed-sort-run'; cmd['confirm']=True
    started=arm(cmd); job=started['sortJobId']; stages=set(); deadline=time.monotonic()+40
    while time.monotonic()<deadline:
        state=arm({'action':'status'}); stages.add(state['actionFrameIndex'])
        if state['sortJobId']!=job: raise RuntimeError('Simulation job replaced')
        if state['sortJobState']=='completed': break
        if state['sortJobState']!='running': raise RuntimeError('Simulation interrupted')
        time.sleep(.1)
    else: raise RuntimeError('Simulation timed out')
    report=dict(real_camera=True,real_npu=True,physical_arm=False,assumed_calibration=True,
                class_name=obj['class_name'],xy_cm=xy,place_cm=cfg['zones'][obj['class_name']]['place_xyz_cm'],
                job_id=job,job_state=state['sortJobState'],stages=sorted(stages))
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)
finally:
    bridge.close()
    state=arm({'action':'status'})
    if job is None or state['sortJobId']==job:
        arm({'action':'stop'}); arm({'action':'off'})
