#!/usr/bin/env python3
"""Deterministic HTTP fixtures for the native W10M browser loader."""
import argparse
import gzip
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http import HTTPStatus
from pathlib import Path
import ssl
import struct
import threading
import time
from urllib.parse import parse_qs, urlsplit
import zlib

# Deterministic response dimensions, body limits and default fixture endpoint.
PNG_WIDTH = 2
PNG_HEIGHT = 2
PNG_BIT_DEPTH = 8
PNG_COLOR_TYPE_RGBA = 6
PNG_NO_COMPRESSION_FILTER_OR_INTERLACE = (0, 0, 0)
FIXTURE_RGBA = (0, 160, 80, 255)
DEFAULT_SERVER_HOST = '127.0.0.1'
DEFAULT_SERVER_PORT = 8000
DEFAULT_DELAY_MS = 200
MAX_DELAY_MS = 5000
MAX_LARGE_RESPONSE_BYTES = 4 * 1024 * 1024
DEFAULT_LARGE_RESPONSE_BYTES = 1024 * 1024
DELAYED_IMAGE_MS = 3000
MIN_FIXTURE_DELAY_MS = 0
MIN_LARGE_RESPONSE_BYTES = 0
REPORT_KINDS = ('scroll', 'history', 'fetch', 'xhr', 'cookie', 'security', 'font', 'visual', 'policy', 'budget')
MAX_REPORT_BYTES = 64 * 1024
REDACTED = '<redacted>'
SENSITIVE_KEYS = ('cookie', 'authorization', 'credential', 'password', 'passwd', 'token', 'secret', 'set-cookie', 'value')


def png():
    """Return a deterministic 2x2 RGBA PNG used by image fixtures."""
    def chunk(name, body):
        """Encode one PNG chunk with its network-order length and CRC."""
        return struct.pack('>I', len(body)) + name + body + struct.pack('>I', zlib.crc32(name + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', PNG_WIDTH, PNG_HEIGHT, PNG_BIT_DEPTH, PNG_COLOR_TYPE_RGBA, *PNG_NO_COMPRESSION_FILTER_OR_INTERLACE))
            + chunk(b'IDAT', zlib.compress((b'\0' + bytes(FIXTURE_RGBA) * PNG_WIDTH) * PNG_HEIGHT)) + chunk(b'IEND', b''))


INDEX = b'''<!doctype html><html><head><meta charset="utf-8"><title>M9 resource fixture</title>
<link rel="stylesheet" href="assets/style.css"><script src="assets/first.js"></script>
<script defer src="assets/second.js"></script></head><body>
<h1 id="heading">External CSS makes this green</h1><p id="script">External script pending</p>
<p id="fetch">Fetch pending</p><p id="xhr">XHR pending</p><p id="cookie">Cookie pending</p>
<img id="image" src="assets/image.png" width="96" height="96" alt="Green fixture image">
<p class="fixture-font">Bundled external web font</p><a id="next" href="second.html">Second page</a>
<a href="#bottom">Same-document anchor</a><button onclick="history.pushState({}, '', '?state=1');document.title='History state 1'">Push state</button>
<button onclick="window.fixtureCount=(window.fixtureCount||0)+1;this.textContent='Clicks '+window.fixtureCount">Click test</button>
<div style="height:1600px">Scroll fixture</div><p id="bottom">Bottom</p></body></html>'''
STYLE = b'''@font-face{font-family:Fixture;src:url(font.ttf)}
body{font-family:sans-serif;margin:16px;background:#eef7ee}#heading{color:rgb(0,128,0)}
.fixture-font{font-family:Fixture}img{image-rendering:pixelated}'''
FIRST = b"window.fixtureOrder=['first'];"
SECOND = b'''window.fixtureOrder.push('second');
document.getElementById('script').textContent='PASS external script order: '+window.fixtureOrder.join(',');
fetch('api/data').then(r=>r.json()).then(v=>document.getElementById('fetch').textContent='PASS fetch: '+v.value);
var x=new XMLHttpRequest();x.open('GET','api/data');x.onload=function(){document.getElementById('xhr').textContent='PASS XHR: '+JSON.parse(x.responseText).value;};x.send();
fetch('cookies/set').then(()=>fetch('cookies/echo')).then(r=>r.json()).then(v=>document.getElementById('cookie').textContent=v.cookie.indexOf('m9=fixture')>=0?'PASS cookie':'FAIL cookie');'''
POLICY_HTML = (b'<h1>CSP fixture</h1><p id="policy">pending: inline script must stay blocked</p>'
               b'<script>window.fixtureInlineExecuted=true;'
               b'document.getElementById("policy").textContent="FAIL script executed";</script>'
               b'<img src="assets/image.png">')
VISUAL_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Visual oracle fixture</title>
<link rel="stylesheet" href="/assets/style.css"></head><body>
<h1 id="heading">Visual oracle</h1>
<img id="vimage" src="/assets/image.png" width="96" height="96" alt="Oracle image">
<p id="vresult">pending: needs computed style and decoded image</p>
<script>
var vcolor = getComputedStyle(document.getElementById("heading")).color.replace(/\\s+/g, "");
var vimg = document.getElementById("vimage");
function vdone(ok) {
  document.getElementById("vresult").textContent = ok ? "PASS visual" : "FAIL visual";
  fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({kind: "visual", ok: ok})});
}
function vcheck() { vdone(vcolor === "rgb(0,128,0)" && vimg.naturalWidth === 2); }
if (vimg.complete) { vcheck(); } else { vimg.onload = vcheck; vimg.onerror = function() { vdone(false); }; }
</script></body></html>'''
SCROLL_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Scroll report fixture</title></head><body>
<p id="scroll">pending: needs programmatic scroll outcome</p>
<div style="height:1600px">Scroll fixture</div>
<script>
addEventListener("load", function() { setTimeout(function() {
  window.scrollTo(0, 800);
  var y = window.scrollY || document.documentElement.scrollTop;
  var ok = y > 0;
  document.getElementById("scroll").textContent = ok ? "PASS scroll: " + y : "FAIL scroll";
  fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({kind: "scroll", y: y, ok: ok})});
}, 100); });
</script></body></html>'''
HISTORY_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>History report fixture</title></head><body>
<p id="history">pending: needs history outcome</p>
<script>
history.pushState({}, "", "?v=1");
history.replaceState({}, "", "?v=2");
var hok = location.search === "?v=2";
document.getElementById("history").textContent = hok ? "PASS history: ?v=2" : "FAIL history";
fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
  body: JSON.stringify({kind: "history", search: location.search, ok: hok})});
</script></body></html>'''
FETCH_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Fetch report fixture</title></head><body>
<p id="fetchreport">pending: needs fetch outcome</p>
<script>
fetch("api/data").then(function(r) { return r.json(); }).then(function(v) {
  var ok = v.value === "fixture-data";
  document.getElementById("fetchreport").textContent = ok ? "PASS fetch report" : "FAIL fetch report";
  fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({kind: "fetch", ok: ok})});
});
</script></body></html>'''
XHR_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>XHR report fixture</title></head><body>
<p id="xhrreport">pending: needs XHR outcome</p>
<script>
var x = new XMLHttpRequest();
x.open("GET", "api/data");
x.onload = function() {
  var ok = JSON.parse(x.responseText).value === "fixture-data";
  document.getElementById("xhrreport").textContent = ok ? "PASS XHR report" : "FAIL XHR report";
  fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({kind: "xhr", ok: ok})});
};
x.send();
</script></body></html>'''
COOKIE_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Cookie report fixture</title></head><body>
<p id="creport">pending: needs stored cookie round-trip</p>
<script>
fetch("cookies/set", {credentials: "same-origin"})
  .then(function() { return fetch("cookies/echo", {credentials: "same-origin"}); })
  .then(function(r) { return r.json(); })
  .then(function(v) {
    var present = v.cookie.indexOf("m9=fixture") >= 0;
    document.getElementById("creport").textContent = present ? "PASS cookie report" : "FAIL cookie report";
    fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
      body: JSON.stringify({kind: "cookie", present: present})});
  });
</script></body></html>'''
SECURITY_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Security report fixture</title></head><body>
<p id="secreport">pending: needs protocol and cookie-visibility outcome</p>
<script>
var visible = document.cookie.indexOf("m9=") >= 0;
document.getElementById("secreport").textContent = "done: " + location.protocol;
fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
  body: JSON.stringify({kind: "security", protocol: location.protocol, cookie_visible: visible})});
</script></body></html>'''
FONT_REPORT_HTML = b'''<!doctype html><html><head><meta charset="utf-8"><title>Font report fixture</title></head><body>
<link rel="stylesheet" href="/assets/style.css">
<p id="fontprobe" class="fixture-font">pending: needs renderer font match</p>
<script>
function fcheck() {
  var fam = getComputedStyle(document.getElementById("fontprobe")).fontFamily;
  var matched = fam.indexOf("Fixture") >= 0 && !!document.fonts && document.fonts.check("16px Fixture");
  document.getElementById("fontprobe").textContent = matched ? "PASS font" : "FAIL font";
  fetch("/report", {method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({kind: "font", matched: !!matched})});
}
if (document.fonts && document.fonts.ready) { document.fonts.ready.then(fcheck); }
else { addEventListener("load", fcheck); }
</script></body></html>'''


def sanitize_report(node):
    """Return a copy of a report with credential/cookie values redacted."""
    if isinstance(node, dict):
        redacted = {}
        for key, val in node.items():
            if any(token in str(key).lower() for token in SENSITIVE_KEYS):
                redacted[key] = REDACTED
            else:
                redacted[key] = sanitize_report(val)
        return redacted
    if isinstance(node, list):
        return [sanitize_report(item) for item in node]
    return node


def _report_body(entry):
    """Unwrap a stored report entry to the reporter-supplied payload."""
    if isinstance(entry, dict) and isinstance(entry.get('report'), dict):
        return entry['report']
    return entry if isinstance(entry, dict) else {}


def check_policy_oracle(page_body, policy_text, control_executed):
    """Pass only when the inline script demonstrably did not run.

    Static PASS text baked into the fixture is rejected. The caller must
    also show a live-script control page executed, otherwise silence proves
    nothing about CSP enforcement.
    """
    text = page_body.decode('utf-8', 'replace') if isinstance(page_body, bytes) else str(page_body)
    if 'PASS script blocked' in text:
        return False
    if 'fixtureInlineExecuted' not in text or 'FAIL script executed' not in text:
        return False
    if not control_executed:
        return False
    if 'FAIL' in policy_text:
        return False
    lowered = str(policy_text).lower()
    return 'pending' in lowered or 'blocked' in lowered


def check_visual_evidence(result_text, computed_color, natural_width):
    """Require computed-style color and decoded-image width behind PASS text."""
    if 'PASS' not in str(result_text) or 'FAIL' in str(result_text):
        return False
    if str(computed_color).replace(' ', '') != 'rgb(0,128,0)':
        return False
    return natural_width == PNG_WIDTH


def check_budget_response(content_type, body_length, requested_bytes):
    """The main-document budget route must serve capped text/html of exact size."""
    if not str(content_type).startswith('text/html'):
        return False
    expected = min(max(int(requested_bytes), 0), MAX_LARGE_RESPONSE_BYTES)
    return body_length == expected


def check_scroll_report(entry):
    """A scroll report passes only with a positive scrolled offset."""
    body = _report_body(entry)
    return body.get('kind') == 'scroll' and body.get('ok') is True and isinstance(body.get('y'), int) and body['y'] > 0


def check_history_report(entry):
    """A history report passes only with the replaced query string."""
    body = _report_body(entry)
    return body.get('kind') == 'history' and body.get('ok') is True and body.get('search') == '?v=2'


def check_fetch_report(entry):
    """A fetch report passes only with success and no leaked cookie value."""
    body = _report_body(entry)
    if body.get('kind') != 'fetch' or body.get('ok') is not True:
        return False
    return 'm9=fixture' not in json.dumps(body)


def check_xhr_report(entry):
    """An XHR report passes only with success and no leaked cookie value."""
    body = _report_body(entry)
    if body.get('kind') != 'xhr' or body.get('ok') is not True:
        return False
    return 'm9=fixture' not in json.dumps(body)


def check_cookie_report(entry):
    """A cookie report passes only with presence proven and values withheld."""
    body = _report_body(entry)
    if body.get('kind') != 'cookie' or body.get('present') is not True:
        return False
    return 'm9=fixture' not in json.dumps(body)


def check_security_report(entry):
    """A security report passes only with an http(s) protocol and no values."""
    body = _report_body(entry)
    if body.get('kind') != 'security' or body.get('protocol') not in ('http:', 'https:'):
        return False
    return 'm9=fixture' not in json.dumps(body)


def check_font_report(entry):
    """A font report passes only when the renderer matched the Fixture face."""
    body = _report_body(entry)
    return body.get('kind') == 'font' and body.get('matched') is True


def check_report_isolation(entries):
    """Stored reports must carry an origin and no credential/cookie values."""
    if not entries:
        return False
    if 'm9=fixture' in json.dumps(entries):
        return False
    return all(isinstance(item, dict) and isinstance(item.get('origin'), str) and isinstance(item.get('kind'), str)
               for item in entries)


class FixtureServer(ThreadingHTTPServer):
    """Threaded fixture server tracking requests and the optional web-font file."""

    daemon_threads = True

    def __init__(self, address, font_path=None):
        """Create the server with a bind address and optional font asset."""
        super().__init__(address, FixtureHandler)
        self.font_path = font_path
        self.requests = []
        self.reports = []
        self.request_lock = threading.Lock()


class FixtureHandler(BaseHTTPRequestHandler):
    """Serves deterministic redirect, encoding, policy, CORS, cookie and delay fixtures."""

    protocol_version = 'HTTP/1.1'

    def handle(self):
        """Treat navigation cancellation as a normal keep-alive socket close."""
        try:
            super().handle()
        except ConnectionError:
            # Stop/navigation replacement can close a keep-alive socket.
            pass

    def log_message(self, fmt, *args):
        """Suppress access logs so fixture cookies and credentials never leak."""
        # Log paths/status only; cookies and credentials are not recorded.
        pass

    def respond(self, body=b'', content_type='text/plain; charset=utf-8', status=HTTPStatus.OK, headers=()):
        """Send a bounded response with explicit type, length and no-store policy."""
        try:
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            for name, value in headers:
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(body)
        except ConnectionError:
            # Navigation cancellation intentionally closes pending responses.
            pass

    def do_OPTIONS(self):
        """Answer the CORS preflight fixture with an allow or deny response."""
        parts = urlsplit(self.path)
        if parts.path == '/cors/allow':
            self.respond(headers=(('Access-Control-Allow-Origin', '*'),
                                  ('Access-Control-Allow-Methods', 'GET, OPTIONS'),
                                  ('Access-Control-Allow-Headers', 'X-Fixture')))
        else:
            self.respond(status=HTTPStatus.FORBIDDEN)

    def do_GET(self):
        """Dispatch a fixture route and record its path for request-order tests."""
        parts = urlsplit(self.path)
        path = parts.path
        args = parse_qs(parts.query)
        with self.server.request_lock:
            self.server.requests.append(path)
        if path in ('/', '/index.html'):
            self.respond(INDEX, 'text/html; charset=utf-8')
        elif path == '/scheduler.html':
            # Final DOM height is an oracle requiring CSS, delayed JavaScript and fetch.
            identity = 2 if args.get('page', ['A'])[0] == 'B' else 1
            html = (f'<!doctype html><html><head><title>Owner service {identity}</title>'
                    '<link rel="stylesheet" href="/scheduler.css"></head><body>'
                    f'<div id="result" data-identity="{identity}"></div>'
                    '<script src="/scheduler.js"></script></body></html>')
            self.respond(html.encode(), 'text/html; charset=utf-8')
        elif path == '/scheduler.css':
            # The script checks the computed style rather than static HTML text.
            time.sleep(0.15)
            self.respond(b'html,body{margin:0}#result{height:101px}', 'text/css; charset=utf-8')
        elif path == '/scheduler.js':
            # Only a timer plus a successful JSON fetch can create the expected height.
            time.sleep(0.15)
            script = (b'setTimeout(function(){var e=document.getElementById("result");'
                      b'var css=parseInt(getComputedStyle(e).height,10)===101;'
                      b'fetch("/api/data").then(function(r){return r.json();}).then(function(v){'
                      b'e.style.height=(css&&v.value==="fixture-data"?3000+Number(e.dataset.identity):17)+"px";'
                      b'});},350);')
            self.respond(script, 'application/javascript')
        elif path == '/assets/style.css':
            self.respond(STYLE, 'text/css; charset=utf-8')
        elif path == '/assets/first.js':
            self.respond(FIRST, 'application/javascript')
        elif path == '/assets/second.js':
            self.respond(SECOND, 'application/javascript')
        elif path == '/assets/image.png':
            self.respond(png(), 'image/png')
        elif path == '/assets/font.ttf':
            if self.server.font_path and self.server.font_path.is_file():
                self.respond(self.server.font_path.read_bytes(), 'font/ttf')
            else:
                self.respond(b'Fixture font not configured', status=HTTPStatus.NOT_FOUND)
        elif path == '/second.html':
            self.respond(b'<title>M9 second page</title><h1>Second page</h1><a href="index.html">First page</a>', 'text/html')
        elif path == '/pushstate.html':
            self.respond(b'<title>PushState fixture</title><p id="pushstate">pushState pending</p>'
                         b'<script>history.pushState({}, "", "?pushed=1");'
                         b'document.getElementById("pushstate").textContent="PASS pushed: "+location.search;</script>', 'text/html')
        elif path == '/replacestate.html':
            self.respond(b'<title>ReplaceState fixture</title><p id="replacestate">replaceState pending</p>'
                         b'<script>history.pushState({}, "", "?v=1");history.replaceState({}, "", "?v=2");'
                         b'document.getElementById("replacestate").textContent="PASS replaced: "+location.search;</script>', 'text/html')
        elif path == '/redirect':
            self.respond(status=HTTPStatus.FOUND, headers=(('Location', '/index.html'),))
        elif path == '/redirect-loop':
            self.respond(status=HTTPStatus.FOUND, headers=(('Location', '/redirect-loop'),))
        elif path == '/missing.html':
            self.respond(b'<title>M9 404</title><h1>404 fixture document</h1>', 'text/html', HTTPStatus.NOT_FOUND)
        elif path == '/latin1.html':
            self.respond('<title>Encoding fixture</title><p>caf\u00e9</p>'.encode('latin-1'), 'text/html; charset=iso-8859-1')
        elif path == '/gzip.html':
            self.respond(gzip.compress(INDEX, mtime=0), 'text/html; charset=utf-8', headers=(('Content-Encoding', 'gzip'),))
        elif path == '/download':
            self.respond(b'M9 download fixture', 'application/octet-stream', headers=(('Content-Disposition', 'attachment; filename="fixture.bin"'),))
        elif path in ('/api/data', '/cors/allow', '/cors/deny'):
            headers = (('Access-Control-Allow-Origin', '*'),) if path == '/cors/allow' else ()
            self.respond(b'{"value":"fixture-data"}', 'application/json', headers=headers)
        elif path == '/cookies/set':
            self.respond(b'cookie set', headers=(('Set-Cookie', 'm9=fixture; Path=/; SameSite=Lax'),))
        elif path == '/cookies/echo':
            self.respond(json.dumps({'cookie': self.headers.get('Cookie', '')}).encode(), 'application/json')
        elif path == '/policy.html':
            self.respond(POLICY_HTML,
                         'text/html', headers=(('Content-Security-Policy', "default-src 'self'; script-src 'none'; img-src 'none'"),))
        elif path == '/cross-origin.html':
            origin = args.get('origin', [''])[0]
            if not origin.startswith(('http://', 'https://')):
                self.respond(b'Provide an absolute second-origin URL', status=HTTPStatus.BAD_REQUEST)
                return
            script = ('<title>CORS fixture</title><p id="allow">pending</p><p id="deny">pending</p><script>'
                      'const origin=' + json.dumps(origin) + ';'
                      'fetch(origin+"/cors/allow").then(r=>r.json()).then(()=>document.getElementById("allow").textContent="PASS allowed");'
                      'fetch(origin+"/cors/deny").then(()=>document.getElementById("deny").textContent="FAIL exposed")'
                      '.catch(()=>document.getElementById("deny").textContent="PASS blocked");</script>')
            self.respond(script.encode(), 'text/html')
        elif path == '/slow.html':
            self.respond(f'<title>Slow resource fixture</title><h1>Input should stay responsive</h1><img src="delay?ms={DELAYED_IMAGE_MS}">'.encode(), 'text/html')
        elif path == '/delay':
            try:
                delay = max(MIN_FIXTURE_DELAY_MS, min(MAX_DELAY_MS, int(args.get('ms', [str(DEFAULT_DELAY_MS)])[0])))
            except ValueError:
                self.respond(b'Invalid delay', status=HTTPStatus.BAD_REQUEST)
                return
            time.sleep(delay / 1000)
            self.respond(png(), 'image/png')
        elif path == '/large':
            try:
                size = max(MIN_LARGE_RESPONSE_BYTES, min(MAX_LARGE_RESPONSE_BYTES, int(args.get('bytes', [str(DEFAULT_LARGE_RESPONSE_BYTES)])[0])))
            except ValueError:
                self.respond(b'Invalid size', status=HTTPStatus.BAD_REQUEST)
                return
            self.respond(b'x' * size, 'application/octet-stream')
        elif path == '/visual.html':
            self.respond(VISUAL_HTML, 'text/html; charset=utf-8')
        elif path == '/scroll.html':
            self.respond(SCROLL_HTML, 'text/html; charset=utf-8')
        elif path == '/history-report.html':
            self.respond(HISTORY_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/fetch-report.html':
            self.respond(FETCH_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/xhr-report.html':
            self.respond(XHR_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/cookie-report.html':
            self.respond(COOKIE_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/security-report.html':
            self.respond(SECURITY_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/font-report.html':
            self.respond(FONT_REPORT_HTML, 'text/html; charset=utf-8')
        elif path == '/large-page':
            try:
                size = max(MIN_LARGE_RESPONSE_BYTES, min(MAX_LARGE_RESPONSE_BYTES, int(args.get('bytes', [str(DEFAULT_LARGE_RESPONSE_BYTES)])[0])))
            except ValueError:
                self.respond(b'Invalid size', status=HTTPStatus.BAD_REQUEST)
                return
            head = b'<!doctype html><html><head><title>Main-document budget fixture</title></head><body><h1>Budget</h1><p>'
            tail = b'</p></body></html>'
            self.respond(head + b'x' * max(0, size - len(head) - len(tail)) + tail, 'text/html; charset=utf-8')
        elif path == '/reports':
            wanted = args.get('kind', [None])[0]
            with self.server.request_lock:
                entries = [entry for entry in self.server.reports if wanted is None or entry['kind'] == wanted]
            self.respond(json.dumps(entries).encode(), 'application/json')
        elif path == '/requests':
            with self.server.request_lock:
                requests = list(self.server.requests)
            self.respond(json.dumps(requests).encode(), 'application/json')
        else:
            self.respond(b'Unknown fixture path', status=HTTPStatus.NOT_FOUND)

    def do_POST(self):
        """Record one JSON outcome report with values redacted and origin kept."""
        parts = urlsplit(self.path)
        path = parts.path
        with self.server.request_lock:
            self.server.requests.append(path)
        if path == '/report':
            try:
                length = int(self.headers.get('Content-Length') or 0)
            except ValueError:
                length = 0
            if length <= 0 or length > MAX_REPORT_BYTES:
                self.respond(b'Missing or oversized report', status=HTTPStatus.BAD_REQUEST)
                return
            try:
                payload = json.loads(self.rfile.read(length).decode('utf-8'))
            except (ValueError, UnicodeDecodeError):
                self.respond(b'Malformed report', status=HTTPStatus.BAD_REQUEST)
                return
            if not isinstance(payload, dict) or payload.get('kind') not in REPORT_KINDS:
                self.respond(b'Malformed report', status=HTTPStatus.BAD_REQUEST)
                return
            entry = {'kind': payload['kind'],
                     'origin': self.headers.get('Origin', 'none'),
                     'report': sanitize_report(payload)}
            with self.server.request_lock:
                self.server.reports.append(entry)
            self.respond(b'{"status":"recorded"}', 'application/json')
        else:
            self.respond(b'Unknown fixture path', status=HTTPStatus.NOT_FOUND)

    def do_DELETE(self):
        """Clear stored fixture reports without recording header values."""
        parts = urlsplit(self.path)
        with self.server.request_lock:
            self.server.requests.append(parts.path)
        if parts.path == '/reports':
            with self.server.request_lock:
                self.server.reports.clear()
            self.respond(b'{"status":"cleared"}', 'application/json')
        else:
            self.respond(b'Unknown fixture path', status=HTTPStatus.NOT_FOUND)


def default_font():
    """Return the repository's bundled DejaVu font used by the font fixture."""
    return Path(__file__).resolve().parents[2] / 'WebKitTestRunner/glib/fonts/DejaVuSans.ttf'


def default_tls_directory():
    """Return the directory holding the fixture TLS certificate and key."""
    return Path(__file__).resolve().parent / 'tls'


def main():
    """Parse bind/font options and run the fixture server until shutdown."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default=DEFAULT_SERVER_HOST)
    parser.add_argument('--port', type=int, default=DEFAULT_SERVER_PORT)
    parser.add_argument('--font', type=Path, default=default_font())
    parser.add_argument('--tls-port', type=int, default=None,
                        help='Serve the same fixtures over HTTPS on this port')
    parser.add_argument('--tls-cert', type=Path, default=None,
                        help='Fixture server certificate (PEM), signed by tls-ca')
    parser.add_argument('--tls-key', type=Path, default=None,
                        help='Fixture server private key (PEM)')
    parser.add_argument('--tls-ca', type=Path, default=None,
                        help='Fixture certificate authority (PEM) for documentation only')
    args = parser.parse_args()
    if not args.font.is_file():
        parser.error('Fixture font does not exist: ' + str(args.font))
    if args.tls_port is not None:
        tls_directory = default_tls_directory()
        cert = args.tls_cert or tls_directory / 'fixture-server.pem'
        key = args.tls_key or tls_directory / 'fixture-server-key.pem'
        if not cert.is_file():
            parser.error('Fixture TLS certificate does not exist: ' + str(cert))
        if not key.is_file():
            parser.error('Fixture TLS key does not exist: ' + str(key))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        tls_server = FixtureServer((args.host, args.tls_port), args.font)
        tls_server.socket = context.wrap_socket(tls_server.socket, server_side=True)
        threading.Thread(target=tls_server.serve_forever, daemon=True).start()
        print(f'Fixture TLS listening on https://{args.host}:{args.tls_port}/index.html', flush=True)
    with FixtureServer((args.host, args.port), args.font) as server:
        print(f'Fixture listening on http://{args.host}:{server.server_port}/index.html', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()
