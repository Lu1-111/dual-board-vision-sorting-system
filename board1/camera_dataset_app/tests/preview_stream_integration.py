"""Hardware-free test of independent preview cache and unchanged full-frame stream."""
import argparse,http.client,json,select,socket,subprocess,time

def main():
    ap=argparse.ArgumentParser();ap.add_argument('fixture');ap.add_argument('--port',type=int,default=47994);a=ap.parse_args()
    results=[]
    for fps,width,quality in [('0','640','75'),('31','640','75'),('15','159','75'),('15','1281','75'),('15','640','0'),('15','640','101')]:
        p=subprocess.run([a.fixture,str(a.port),fps,width,quality],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=5)
        assert p.returncode==3,(fps,width,quality,p.returncode)
    results.append('invalid_preview_configuration_rejected')
    proc=subprocess.Popen([a.fixture,str(a.port)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,universal_newlines=True,bufsize=1)
    def line():
        assert select.select([proc.stdout],[],[],10)[0],'fixture timeout'
        return proc.stdout.readline().strip()
    def command(c):
        proc.stdin.write(c+'\n');proc.stdin.flush();assert line()=='ACK '+c
    def get(path):
        c=http.client.HTTPConnection('127.0.0.1',a.port,timeout=5)
        try:
            c.request('GET',path);r=c.getresponse();b=r.read();h=dict(r.getheaders())
            assert int(h['Content-Length'])==len(b)
            return r.status,h,b
        finally:c.close()
    def status():
        c,h,b=get('/v1/status.json');assert c==200;return json.loads(b)
    def frame(path,size):
        c,h,b=get(path);assert c==200,(path,c,b[:200])
        assert b[:2]==b'\xff\xd8' and b[-2:]==b'\xff\xd9'
        assert (int(h['X-Frame-Width']),int(h['X-Frame-Height']))==size
        assert h['X-Camera-Source']=='simulation' and 0<=int(h['X-Frame-Age-Ms'])<=1500
        assert h['Cache-Control']=='no-store' and len(h['X-Camera-Session'])==36
        return h,b
    try:
        assert line()=='READY';time.sleep(.6)
        st=status();assert st['encode_count']=='0','Preview must not trigger full-res encoding'
        pv=st['preview'];assert int(pv['encode_count'])>0,'Preview should cache before HTTP demand'
        assert pv['quality']==75 and pv['max_edge']==640 and pv['encode_period_ms']==67
        assert st['quality']==95 and st['encode_period_ms']==334
        first,_=frame('/v1/preview.jpg',(640,360))
        before=int(status()['preview']['encode_count']);time.sleep(1.05);after=int(status()['preview']['encode_count'])
        assert 1<=after-before<=17,(before,after)
        results.append('preview_background_encoding_and_rate_cap')
        fresh,_=frame('/v1/preview.jpg',(640,360));assert int(fresh['X-Frame-Sequence'])>int(first['X-Frame-Sequence'])
        full,full_body=frame('/v1/frame.jpg',(1920,1080));assert full['X-Camera-Session']==fresh['X-Camera-Session']
        results.append('independent_preview_and_full_resolution_metadata')
        # Static fixture pixel values mean full-res JPEG stays byte-identical.
        time.sleep(.5);_,second_full=frame('/v1/frame.jpg',(1920,1080));assert second_full==full_body
        time.sleep(1.7);idle=int(status()['encode_count']);time.sleep(.5)
        assert int(status()['encode_count'])==idle
        assert int(status()['preview']['encode_count'])>after
        results.append('full_stream_demand_lease_unchanged_preview_continues')
        st=status();assert st['pending_frames'] in (0,1) and st['preview']['pending_frames'] in (0,1)
        assert int(st['preview']['replaced_count'])>0
        command('pause');time.sleep(1.6)
        c,_,b=get('/v1/preview.jpg');assert c==503 and json.loads(b)['error']=='frame_stale'
        command('stop');c,_,b=get('/v1/preview.jpg');assert c==503 and json.loads(b)['error']=='fixture_stopped'
        command('begin');h,_=frame('/v1/preview.jpg',(640,360));assert h['X-Camera-Session']!=first['X-Camera-Session']
        full,_=frame('/v1/frame.jpg',(1920,1080));assert full['X-Camera-Session']==h['X-Camera-Session']
        results.append('pause_stale_stop_restart_session_invalidation')
        evidence=status();command('quit');assert proc.wait(timeout=10)==0
        print('PREVIEW_STREAM_PASS '+json.dumps({'tests':results,'all_passed':True,'final_status':evidence},sort_keys=True))
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=10)

if __name__=='__main__':main()
