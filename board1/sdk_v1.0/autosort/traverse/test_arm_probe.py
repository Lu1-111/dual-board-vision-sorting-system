"""Host VM only:20 synthetic Unix-socket status replies, no UIO or real UI."""
import json,os,socket,subprocess,sys,threading,time
path='/tmp/hdmi-ui-arm.sock'
if os.path.lexists(path):raise SystemExit('Existing VM arm socket present; refusing to replace it')
server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
server.bind(path);inode=os.stat(path).st_ino;server.listen(2);server.settimeout(3)
requests=[]
def serve():
    for i in range(20):
        peer,_=server.accept()
        with peer:
            peer.settimeout(1);data=b''
            while b'\n' not in data:
                data+=peer.recv(1024)
                assert len(data)<=4096
            requests.append(json.loads(data.decode()))
            if i==3:time.sleep(.12)
            peer.sendall(b'{"ok":true,"enabled":false}\n')
t=threading.Thread(target=serve);t.start()
try:
    p=subprocess.run([sys.argv[1],'--arm-probe-readonly'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True,timeout=10)
    t.join(3)
    assert p.returncode==0,(p.returncode,p.stderr)
    value=json.loads(p.stdout)
    assert requests==[{'action':'status'}]*20,requests
    assert value['requests']==20 and value['failures']==0 and value['above100ms']>=1,value
    assert value['motor_device_opened'] is False and value['max_ms']>=100,value
    print('ARM_PROBE_NATIVE_PASS:20 status-only requests; injected120ms delay reported; noUIO')
finally:
    server.close()
    if os.path.lexists(path) and os.stat(path).st_ino==inode:os.unlink(path)
