import json, pathlib, subprocess, tempfile, time, socket, struct
base=pathlib.Path('/tmp/can-overview-20261103/host')
with tempfile.TemporaryDirectory() as d:
 root=pathlib.Path(d); out=root/'line.txt'
 def put(name,o):
  p=root/(name+'.tmp'); p.write_text(json.dumps(o));p.replace(root/name)
 put('state.json',{'state':'FAULT'})
 put('cycle-1.json',{'decision':'NORMAL','before_job':0})
 put('completed-1.json',{'actionJobState':'running','moving':True,'actionPlaying':True})
 def start():return subprocess.Popen([str(base/'telemetry_publisher'),str(root),str(out)])
 def values():return list(map(int,out.read_text().split()))
 proc=start()
 try:
  time.sleep(.7); assert values()[0]==3 and values()[8:10]==[0,0]
  put('completed-1.json',{'actionJobId':1,'actionJobState':'completed','moving':False,'actionPlaying':False})
  time.sleep(.7); assert values()[8:10]==[1,0]
  time.sleep(.7); assert values()[8:10]==[1,0]
  proc.terminate();proc.wait();proc=start();time.sleep(.7);assert values()[8:10]==[1,0]
  put('cycle-2.json',{'decision':'DEFECT','before_job':1});put('completed-2.json',{'actionJobId':2,'actionJobState':'completed','moving':False,'actionPlaying':False})
  time.sleep(.7);assert values()[8:10]==[1,1]
  put('cycle-1.json',{'decision':'DEFECT','before_job':2});time.sleep(.7);assert values()[8:10]==[1,1] # old completion cannot match new decision
  sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',39129));sock.settimeout(8)
  sender=subprocess.Popen([str(base/'can_sender'),'--udp','39129','--state',str(out),'--ticks','3'])
  beats=[]
  while len(beats)<3:
   b=sock.recv(128)
   if struct.unpack('<I',b[:4])[0]==0x101:beats.append((time.monotonic(),b))
  assert all(1.8<beats[i][0]-beats[i-1][0]<2.3 for i in (1,2)),beats
  assert all(struct.unpack('<I',b[9:13])[0]>0 and b[7]==3 for _,b in beats)
  assert sender.wait(timeout=4)==0
  sock.close()
 finally:proc.terminate();proc.wait()
print('Publisher PASS: completed-only counts, dedup, restart, fault, real uptime; sender default 2-second cadence PASS')
