"""Local UI demo with an in-memory ESP32 API. Does not contact real hardware.

Run: python tools/preview.py   (http://127.0.0.1:8765)
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
import json
import re
import time
import argparse

ROOT = Path(__file__).resolve().parents[1]


def ring(r, g, b, w=0, fire=False):
    return f'{r:02X}{g:02X}{b:02X}{w:02X}{int(fire)}' * 72


class Demo:
    mode = 'astro'
    sim_ts = 0
    live = {'ext': ring(5, 12, 8), 'int': ring(22, 10, 3)}
    manual = live.copy()
    scenes = {
        'Calma orbital': {'ext': ring(3, 12, 13), 'int': ring(18, 9, 3)},
        'L’espígol': {'ext': ring(5, 4, 18), 'int': ring(18, 8, 17)},
        'Brasa': {'ext': ring(16, 3, 0), 'int': ring(23, 9, 0, fire=True)},
    }
    requests = []


class Handler(BaseHTTPRequestHandler):
    def respond(self, data, status=200, content_type=None):
        if content_type is None:
            content_type = 'application/json; charset=utf-8'
            data = json.dumps(data, ensure_ascii=False)
        content = data.encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(content)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(content)

    def error(self, message, status=400):
        self.respond(message, status, 'text/plain; charset=utf-8')

    def handle_request(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        name = query.get('name', [''])[0]
        path = url.path
        length = int(self.headers.get('Content-Length', 0))
        try:
            body = json.loads(self.rfile.read(length)) if length else None
        except (ValueError, UnicodeDecodeError):
            return self.error('JSON invàlid')
        Demo.requests.append({'method': self.command, 'path': path, 'name': name})
        if path == '/':
            from build_web import build
            page = build().split('R"=====(\n', 1)[1].rsplit('\n)=====";', 1)[0]
            return self.respond(page, content_type='text/html; charset=utf-8')
        if path == '/favicon.ico':
            return self.respond('', 204, 'text/plain')
        if path == '/__demo/requests':
            return self.respond(Demo.requests)
        if path == '/api/status':
            return self.respond(dict(mode=Demo.mode, sim_ts=Demo.sim_ts, fraction=.417,
                phase_age=7.2, is_waxing=True, is_daytime=True,
                timestamp=int(time.time()), clock_synced=True, **Demo.live))
        if path == '/api/colors':
            return self.respond(Demo.manual)
        if path == '/api/mode':
            mode = query.get('set', [''])[0]
            if mode not in ('astro', 'manual'):
                return self.error('Mode invàlid')
            Demo.mode = mode
            Demo.live = Demo.manual.copy() if mode == 'manual' else {'ext': ring(5, 12, 8), 'int': ring(22, 10, 3)}
        elif path == '/api/simulate':
            try:
                Demo.sim_ts = int(query.get('ts', [''])[0])
            except ValueError:
                return self.error('Data invàlida')
            Demo.mode = 'simulacio'
        elif path == '/api/led' and self.command == 'POST':
            if not self.valid_scene(body):
                return self.error('Dades RGBW invàlides')
            Demo.manual = body.copy()
            Demo.live = body.copy()
            Demo.mode = 'manual'
        elif path == '/api/presets/list':
            return self.respond(list(Demo.scenes))
        elif path == '/api/presets/get':
            if name not in Demo.scenes:
                return self.error('No trobat', 404)
            return self.respond(dict(name=name, **Demo.scenes[name]))
        elif path == '/api/presets/save' and self.command == 'POST':
            if not name or len(name) > 20 or '/' in name or '\\' in name:
                return self.error('Nom invàlid')
            if not self.valid_scene(body):
                return self.error('Dades RGBW invàlides')
            Demo.scenes[name] = body.copy()
        elif path == '/api/presets/delete' and self.command == 'POST':
            if name not in Demo.scenes:
                return self.error('No trobat', 404)
            del Demo.scenes[name]
        else:
            return self.error('No trobat', 404)
        self.respond('OK', content_type='text/plain; charset=utf-8')

    @staticmethod
    def valid_scene(body):
        return isinstance(body, dict) and all(
            isinstance(body.get(key), str) and re.fullmatch(r'(?:[0-9a-fA-F]{8}[01]){72}', body[key])
            for key in ('ext', 'int'))

    do_GET = handle_request
    do_POST = handle_request

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    print(f'UI demo (simulated data, no hardware): http://127.0.0.1:{args.port}', flush=True)
    HTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
