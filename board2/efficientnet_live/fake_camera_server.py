"""Loopback-only adversarial camera fixture for live HTTP/gate QA; no board IO."""
import argparse
import base64
import json
import socketserver
import struct
import threading
import time


JPEG=base64.b64decode('/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAAYACADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDEoooryz4sKKKKACiiigAooooA/9k=')


def dimensions_bomb(jpeg):
    # Alter SOF dimensions only; do not allocate a giant raster in the test server.
    data=bytearray(jpeg)
    for marker in (b'\xff\xc0',b'\xff\xc2'):
        at=data.find(marker)
        if at>=0:
            data[at+5:at+9]=struct.pack('>HH',65535,65535)
            return bytes(data)
    raise RuntimeError('JPEG SOF marker missing')


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(1.0)
        try:
            request=b''
            while b'\r\n\r\n' not in request and len(request)<8192:
                part=self.request.recv(1024)
                if not part:return
                request+=part
            path=request.split(b' ',2)[1].decode('ascii')
            case=path.strip('/').split('/')[-1]
            body=JPEG
            session='qa-a';seq='100';age='0'
            if case=='repeat':seq='100'
            if case=='rollback':seq='99'
            if case=='session-reset':session='qa-b';seq='1'
            if case=='fresh-after-error':seq='101'
            if case=='stale':age='60000'
            if case=='invalid-sequence':seq='-1'
            if case=='invalid-age':age='nan'
            if case=='invalid-jpeg':body=b'not a jpeg'
            if case=='truncated-jpeg-valid-length':body=body[:-20]
            if case=='oversize-jpeg':body=dimensions_bomb(body)
            status='500 Internal Server Error' if case=='http500' else '200 OK'
            headers=[f'HTTP/1.1 {status}','Connection: close','Content-Type: image/jpeg',
                     f'X-Camera-Session: {session}',f'X-Frame-Sequence: {seq}',
                     f'X-Frame-Age-Ms: {age}','X-Captured-Utc: 1970-01-01T00:00:00Z',
                     'X-Camera-Source: imx415']
            if case=='missing-session':headers=[h for h in headers if not h.startswith('X-Camera-Session:')]
            if case=='header-bomb':headers.append('X-Padding: '+'x'*70000)
            if case=='huge-length':headers.append('Content-Length: 999999999')
            elif case=='negative-length':headers.append('Content-Length: -1')
            elif case=='duplicate-length':headers+=['Content-Length: 1',f'Content-Length: {len(body)}']
            elif case=='chunked':headers.append('Transfer-Encoding: chunked')
            elif case!='no-length':headers.append(f'Content-Length: {len(body)}')
            wire=('\r\n'.join(headers)+'\r\n\r\n').encode()
            if case=='disconnect':return
            if case=='slow-header':time.sleep(3.5)
            self.request.sendall(wire)
            if case=='slow-body':time.sleep(3.5)
            if case=='truncated':body=body[:len(body)//2]
            if case=='chunked':body=f'{len(body):x}\r\n'.encode()+body+b'\r\n0\r\n\r\n'
            self.request.sendall(body)
        except (BrokenPipeError,ConnectionResetError,TimeoutError,OSError):
            pass


class CameraServer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True


def start_server():
    server=CameraServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    return server


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=float,default=60)
    args=parser.parse_args()
    srv=start_server()
    print(json.dumps({'host':'127.0.0.1','port':srv.server_address[1],'examples':['normal','repeat','rollback','session-reset','stale','huge-length','truncated','slow-header','slow-body','invalid-jpeg','oversize-jpeg','fresh-after-error']}),flush=True)
    try:time.sleep(args.seconds)
    finally:srv.shutdown();srv.server_close()
