"""Linux-host fake HTTP regression for production live_io.hpp; no NPU required."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from fake_camera_server import start_server

ROOT=Path(__file__).resolve().parent

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler',default='g++');args=ap.parse_args()
    if os.name!='posix':raise SystemExit('Production header uses POSIX APIs; run this harness on a Linux host, not the board.')
    results=[]
    with tempfile.TemporaryDirectory(prefix='efficientnet_http_qa_') as temp:
        exe=Path(temp)/'io_qa'
        subprocess.run([args.compiler,'-std=c++11','-O2',str(ROOT/'live_io_qa_driver.cpp'),'-o',str(exe)],check=True,timeout=30)
        server=start_server();base=f'http://127.0.0.1:{server.server_address[1]}/'
        def test(name,cases,expected,max_time=4):
            begin=time.monotonic()
            proc=subprocess.run([str(exe)]+[base+c for c in cases],universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=max_time+1)
            elapsed=time.monotonic()-begin
            lines=proc.stdout.strip().splitlines()
            got=[line.split(' ',1)[0] for line in lines]
            passed=proc.returncode==0 and got==expected and elapsed<max_time
            results.append({'test':name,'passed':passed,'elapsed_seconds':elapsed,'output':lines,'expected':expected})
        try:
            test('normal',['normal'],['OK'])
            test('duplicates_and_recovery',['normal','repeat','rollback','fresh-after-error'],['OK','ERROR','ERROR','OK'])
            test('session_reset_and_retired_replay',['normal','session-reset','normal'],['OK','OK','ERROR'])
            for name in ['stale','invalid-sequence','invalid-age','invalid-jpeg','oversize-jpeg','missing-session','header-bomb','huge-length','negative-length','duplicate-length','chunked','no-length','disconnect','truncated','truncated-jpeg-valid-length','http500']:
                test(name,[name],['ERROR'])
            test('slow_header_deadline',['slow-header'],['ERROR'],max_time=2.7)
            test('slow_body_deadline',['slow-body'],['ERROR'],max_time=2.7)
            test('network_error_then_recover',['http500','disconnect','normal'],['ERROR','ERROR','OK'])
            proc=subprocess.Popen([str(exe),base+'slow-body'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True)
            time.sleep(0.15);begin=time.monotonic();proc.send_signal(signal.SIGTERM)
            out,err=proc.communicate(timeout=0.8);elapsed=time.monotonic()-begin
            results.append({'test':'sigterm_unblocks_poll','passed':proc.returncode==0 and out.startswith('ERROR Stopping') and elapsed<0.8,'elapsed_seconds':elapsed,'output':out.strip()})
        finally:server.shutdown();server.server_close()
    report={'scope':'Pure POSIX HTTP/frame gate only; does not cover ArmNN worker publication or JPEG decode success','all_passed':all(r['passed'] for r in results),'results':results}
    (ROOT/'live_io_qa_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))
    if not report['all_passed']:raise SystemExit(1)

if __name__=='__main__':main()

