"""Verify fixture responses before exercising the native browser on-device."""
import contextlib
import gzip
import http.client
from http import HTTPStatus
import io
import json
import threading
import time
import unittest
from server import (DEFAULT_LARGE_RESPONSE_BYTES, MAX_LARGE_RESPONSE_BYTES,
                    FixtureHandler, FixtureServer, INDEX, REDACTED,
                    check_budget_response, check_cookie_report, check_fetch_report,
                    check_font_report, check_history_report, check_policy_oracle,
                    check_report_isolation, check_scroll_report, check_security_report,
                    check_visual_evidence, check_xhr_report, default_font, png)

HTTP_TIMEOUT_SECONDS = 5
THREAD_START_TIMEOUT_SECONDS = 2
THREAD_JOIN_TIMEOUT_SECONDS = 3
CONCURRENT_REQUEST_BUDGET_SECONDS = 0.4
CONCURRENCY_PROBE_DELAY_MS = 500
CANCELLED_REQUEST_DELAY_MS = 100
OVERSIZED_RESPONSE_REQUEST_BYTES = 99999999


class FixtureTests(unittest.TestCase):
    """Host one ephemeral fixture server and verify each HTTP contract."""

    @classmethod
    def setUpClass(cls):
        """Start a server on an OS-selected local port for the test class."""
        cls.server = FixtureServer(('127.0.0.1', 0), default_font())
        cls.worker = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.worker.start()

    @classmethod
    def tearDownClass(cls):
        """Stop the server and join its request loop before the class ends."""
        cls.server.shutdown()
        cls.server.server_close()
        cls.worker.join()

    def request(self, path, headers=None, method='GET'):
        """Return status, headers and body for one local fixture request."""
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=HTTP_TIMEOUT_SECONDS)
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def test_external_resource_response_types_and_font(self):
        """Verify index, stylesheet, scripts, PNG and the configured web font."""
        self.assertEqual(self.request('/index.html')[2], INDEX)
        for path, mime in [('style.css', 'text/css'), ('first.js', 'application/javascript'),
                           ('second.js', 'application/javascript'), ('image.png', 'image/png'), ('font.ttf', 'font/ttf')]:
            status, headers, body = self.request('/assets/' + path)
            self.assertEqual(status, HTTPStatus.OK, path)
            self.assertTrue(headers['Content-Type'].startswith(mime), path)
            self.assertEqual(int(headers['Content-Length']), len(body), path)
            self.assertTrue(body)
        self.assertEqual(self.request('/assets/image.png')[2], png())

    def test_pushstate_fixture_page(self):
        """Verify the pushState fixture includes its history mutation script."""
        status, headers, body = self.request('/pushstate.html')
        self.assertEqual(status, HTTPStatus.OK)
        self.assertTrue(headers['Content-Type'].startswith('text/html'))
        self.assertIn(b'history.pushState', body)

    def test_scheduler_resource_oracle_routes(self):
        """Check MIME/closure and distinct page identities before real DOM execution on phone."""
        for page, identity in (('A', b'1'), ('B', b'2')):
            status, headers, body = self.request('/scheduler.html?page=' + page)
            self.assertEqual(status, HTTPStatus.OK)
            self.assertTrue(headers['Content-Type'].startswith('text/html'))
            self.assertIn(b'data-identity="' + identity + b'"', body)
        for route, mime in (('/scheduler.css', 'text/css'), ('/scheduler.js', 'application/javascript')):
            status, headers, body = self.request(route)
            self.assertEqual(status, HTTPStatus.OK)
            self.assertTrue(headers['Content-Type'].startswith(mime))
            self.assertEqual(int(headers['Content-Length']), len(body))
        self.assertIn(b'setTimeout', body)
        self.assertIn(b'fetch("/api/data")', body)

    def test_replacestate_fixture_page(self):
        """Verify the replaceState fixture includes both state mutations."""
        status, headers, body = self.request('/replacestate.html')
        self.assertEqual(status, HTTPStatus.OK)
        self.assertTrue(headers['Content-Type'].startswith('text/html'))
        self.assertIn(b'history.replaceState', body)

    def test_redirect_error_encoding_compression_and_download(self):
        """Verify redirect, 404, legacy encoding, gzip and attachment responses."""
        status, headers, _ = self.request('/redirect')
        self.assertEqual((status, headers['Location']), (HTTPStatus.FOUND, '/index.html'))
        self.assertEqual(self.request('/redirect-loop')[1]['Location'], '/redirect-loop')
        self.assertEqual(self.request('/missing.html')[0], HTTPStatus.NOT_FOUND)
        self.assertIn('caf\u00e9', self.request('/latin1.html')[2].decode('latin-1'))
        status, headers, body = self.request('/gzip.html')
        self.assertEqual(headers['Content-Encoding'], 'gzip')
        self.assertEqual(gzip.decompress(body), INDEX)
        self.assertIn('attachment', self.request('/download')[1]['Content-Disposition'])

    def test_cors_and_policy_headers(self):
        """Verify preflight, CORS, CSP and cross-origin fixture payloads."""
        self.assertEqual(self.request('/cors/allow')[1]['Access-Control-Allow-Origin'], '*')
        self.assertNotIn('Access-Control-Allow-Origin', self.request('/cors/deny')[1])
        self.assertEqual(self.request('/cors/allow', method='OPTIONS')[0], HTTPStatus.OK)
        self.assertEqual(self.request('/cors/deny', method='OPTIONS')[0], HTTPStatus.FORBIDDEN)
        self.assertIn("script-src 'none'", self.request('/policy.html')[1]['Content-Security-Policy'])
        self.assertIn(b'PASS blocked', self.request('/cross-origin.html?origin=http://127.0.0.1:8001')[2])

    def test_cookie_request_echo_and_data(self):
        """Verify Set-Cookie, request-cookie echo and JSON data responses."""
        self.assertEqual(self.request('/cookies/set')[1]['Set-Cookie'], 'm9=fixture; Path=/; SameSite=Lax')
        self.assertEqual(json.loads(self.request('/cookies/echo', {'Cookie': 'm9=fixture'})[2])['cookie'], 'm9=fixture')
        self.assertEqual(json.loads(self.request('/cookies/echo')[2])['cookie'], '')
        self.assertEqual(json.loads(self.request('/api/data')[2])['value'], 'fixture-data')

    def test_delayed_request_does_not_block_other_clients(self):
        """Verify the threaded server serves another client during a slow response."""
        result = []
        started = threading.Event()
        def delayed():
            """Hold one client open while another request checks concurrency."""
            connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=HTTP_TIMEOUT_SECONDS)
            connection.request('GET', f'/delay?ms={CONCURRENCY_PROBE_DELAY_MS}')
            started.set()
            response = connection.getresponse()
            result.append(response.read())
            connection.close()
        worker = threading.Thread(target=delayed)
        worker.start()
        started.wait(THREAD_START_TIMEOUT_SECONDS)
        start = time.monotonic()
        self.assertEqual(self.request('/api/data')[0], HTTPStatus.OK)
        self.assertLess(time.monotonic() - start, CONCURRENT_REQUEST_BUDGET_SECONDS)
        worker.join(THREAD_JOIN_TIMEOUT_SECONDS)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result, [png()])

    def post_report(self, payload, origin=None, raw=None):
        """POST one JSON report and return status, headers and body."""
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=HTTP_TIMEOUT_SECONDS)
        body = raw if raw is not None else json.dumps(payload)
        headers = {'Content-Type': 'application/json'}
        if origin is not None:
            headers['Origin'] = origin
        connection.request('POST', '/report', body=body, headers=headers)
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def delete_reports(self):
        """Clear stored reports and return status, headers and body."""
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=HTTP_TIMEOUT_SECONDS)
        connection.request('DELETE', '/reports')
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def test_policy_oracle_reports_nonexecution_not_static_text(self):
        """The CSP fixture must prove inline nonexecution with a live-script control."""
        status, headers, body = self.request('/policy.html')
        self.assertEqual(status, HTTPStatus.OK)
        self.assertIn("script-src 'none'", headers['Content-Security-Policy'])
        self.assertNotIn(b'PASS script blocked', body)
        self.assertIn(b'pending', body)
        self.assertIn(b'fixtureInlineExecuted', body)
        self.assertIn(b'FAIL script executed', body)
        self.assertTrue(check_policy_oracle(body, 'pending: inline script must stay blocked', True))
        self.assertFalse(check_policy_oracle(body, 'FAIL script executed', True))
        self.assertFalse(check_policy_oracle(body, 'pending: inline script must stay blocked', False))
        self.assertFalse(check_policy_oracle(b'<p id="policy">PASS script blocked</p>', 'PASS script blocked', True))

    def test_large_page_exercises_main_document_budget(self):
        """The HTML budget route must serve capped text/html of exact length."""
        status, headers, body = self.request('/large-page?bytes=1024')
        self.assertEqual(status, HTTPStatus.OK)
        self.assertTrue(headers['Content-Type'].startswith('text/html'))
        self.assertEqual(len(body), 1024)
        self.assertEqual(len(self.request(f'/large-page?bytes={OVERSIZED_RESPONSE_REQUEST_BYTES}')[2]), MAX_LARGE_RESPONSE_BYTES)
        self.assertEqual(self.request('/large-page?bytes=bad')[0], HTTPStatus.BAD_REQUEST)
        self.assertTrue(check_budget_response('text/html; charset=utf-8', 1024, 1024))
        self.assertTrue(check_budget_response('text/html', MAX_LARGE_RESPONSE_BYTES, OVERSIZED_RESPONSE_REQUEST_BYTES))
        self.assertFalse(check_budget_response('application/octet-stream', 1024, 1024))
        self.assertFalse(check_budget_response('text/html; charset=utf-8', 512, 1024))

    def test_visual_oracle_requires_computed_style_and_decoded_image(self):
        """Static PASS text must fail without matching computed color and pixels."""
        status, headers, body = self.request('/visual.html')
        self.assertEqual(status, HTTPStatus.OK)
        self.assertTrue(headers['Content-Type'].startswith('text/html'))
        self.assertIn(b'<p id="vresult">pending', body)
        self.assertIn(b'getComputedStyle', body)
        self.assertIn(b'naturalWidth', body)
        self.assertIn(b'/report', body)
        self.assertTrue(check_visual_evidence('PASS visual', 'rgb(0, 128, 0)', 2))
        self.assertFalse(check_visual_evidence('PASS visual', 'rgb(255, 0, 0)', 2))
        self.assertFalse(check_visual_evidence('PASS visual', 'rgb(0,128,0)', 0))
        self.assertFalse(check_visual_evidence('pending: needs computed style', 'rgb(0,128,0)', 2))

    def test_report_lifecycle_origin_isolation_and_redaction(self):
        """Reports round-trip per origin with credential values redacted and logs silent."""
        self.assertEqual(self.delete_reports()[0], HTTPStatus.OK)
        self.assertEqual(self.post_report({})[0], HTTPStatus.BAD_REQUEST)
        self.assertEqual(self.post_report({'kind': 'nope', 'ok': True})[0], HTTPStatus.BAD_REQUEST)
        self.assertEqual(self.post_report(None, raw='{not json')[0], HTTPStatus.BAD_REQUEST)
        status, _, _ = self.post_report({'kind': 'cookie', 'present': True, 'cookie': 'm9=fixture',
                                         'Authorization': 'secret'}, origin='http://127.0.0.1:8001')
        self.assertEqual(status, HTTPStatus.OK)
        status, _, body = self.request('/reports')
        self.assertEqual(status, HTTPStatus.OK)
        entries = json.loads(body)
        self.assertTrue(check_report_isolation(entries))
        self.assertNotIn('m9=fixture', body.decode())
        self.assertNotIn('secret', body.decode())
        self.assertIn(REDACTED, body.decode())
        self.assertEqual(entries[0]['origin'], 'http://127.0.0.1:8001')
        self.assertFalse(check_report_isolation([{'kind': 'cookie', 'origin': 'none',
                                                 'report': {'cookie': 'm9=fixture'}}]))
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            FixtureHandler.log_message(None, 'GET %s', '/cookies/echo m9=fixture')
        self.assertEqual(captured.getvalue(), '')
        self.assertEqual(self.delete_reports()[0], HTTPStatus.OK)
        self.assertEqual(json.loads(self.request('/reports')[2]), [])

    def test_scroll_history_fixtures_require_browser_outcomes(self):
        """Scroll/history pages must carry renderer evidence and strict checkers."""
        _, _, scroll = self.request('/scroll.html')
        self.assertIn(b'<p id="scroll">pending', scroll)
        self.assertIn(b'scrollTo', scroll)
        self.assertIn(b'scrollY', scroll)
        self.assertIn(b'"scroll"', scroll)
        _, _, history = self.request('/history-report.html')
        self.assertIn(b'history.pushState', history)
        self.assertIn(b'history.replaceState', history)
        self.assertIn(b'"history"', history)
        self.assertTrue(check_scroll_report({'kind': 'scroll', 'y': 800, 'ok': True}))
        self.assertFalse(check_scroll_report({'kind': 'scroll', 'y': 0, 'ok': True}))
        self.assertFalse(check_scroll_report({'kind': 'scroll', 'y': 800, 'ok': False}))
        self.assertTrue(check_history_report({'kind': 'history', 'search': '?v=2', 'ok': True}))
        self.assertFalse(check_history_report({'kind': 'history', 'search': '?v=1', 'ok': True}))

    def test_fetch_xhr_cookie_security_font_reports(self):
        """Data/session/security/font pages must post value-free outcome reports."""
        _, _, fetch = self.request('/fetch-report.html')
        self.assertIn(b'fetch("api/data")', fetch)
        _, _, xhr = self.request('/xhr-report.html')
        self.assertIn(b'XMLHttpRequest', xhr)
        _, _, cookie = self.request('/cookie-report.html')
        self.assertIn(b'cookies/set', cookie)
        self.assertIn(b'{kind: "cookie", present: present}', cookie)
        _, _, security = self.request('/security-report.html')
        self.assertIn(b'location.protocol', security)
        _, _, font = self.request('/font-report.html')
        self.assertIn(b'document.fonts.check', font)
        self.assertIn(b'fixture-font', font)
        for page in (fetch, xhr, cookie, security, font):
            self.assertIn(b'pending', page)
            self.assertIn(b'/report', page)
        self.assertTrue(check_fetch_report({'kind': 'fetch', 'ok': True}))
        self.assertFalse(check_fetch_report({'kind': 'fetch', 'ok': False}))
        self.assertTrue(check_xhr_report({'kind': 'xhr', 'ok': True}))
        self.assertFalse(check_xhr_report({'kind': 'xhr', 'ok': True, 'value': 'm9=fixture'}))
        self.assertTrue(check_cookie_report({'kind': 'cookie', 'present': True}))
        self.assertFalse(check_cookie_report({'kind': 'cookie', 'present': False}))
        self.assertFalse(check_cookie_report({'kind': 'cookie', 'present': True, 'cookie': 'm9=fixture'}))
        self.assertTrue(check_security_report({'kind': 'security', 'protocol': 'http:', 'cookie_visible': False}))
        self.assertFalse(check_security_report({'kind': 'security', 'protocol': 'file:', 'cookie_visible': False}))
        self.assertTrue(check_font_report({'kind': 'font', 'matched': True}))
        self.assertFalse(check_font_report({'kind': 'font', 'matched': False}))
        self.delete_reports()
        self.assertEqual(self.post_report({'kind': 'font', 'matched': True},
                                          origin='http://127.0.0.1:8000')[0], HTTPStatus.OK)
        entries = json.loads(self.request('/reports?kind=font')[2])
        self.assertEqual(len(entries), 1)
        self.assertTrue(check_font_report(entries[0]))
        self.assertTrue(check_report_isolation(entries))
        self.assertEqual(self.delete_reports()[0], HTTPStatus.OK)

    def test_cancelled_socket_and_bounded_large_response(self):
        """Verify client cancellation is tolerated and response sizes are bounded."""
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=HTTP_TIMEOUT_SECONDS)
        connection.request('GET', f'/delay?ms={CANCELLED_REQUEST_DELAY_MS}')
        connection.close()
        self.assertEqual(len(self.request(f'/large?bytes={DEFAULT_LARGE_RESPONSE_BYTES}')[2]), DEFAULT_LARGE_RESPONSE_BYTES)
        self.assertEqual(len(self.request(f'/large?bytes={OVERSIZED_RESPONSE_REQUEST_BYTES}')[2]), MAX_LARGE_RESPONSE_BYTES)
        self.assertEqual(self.request('/large?bytes=bad')[0], HTTPStatus.BAD_REQUEST)
        self.assertEqual(self.request('/delay?ms=bad')[0], HTTPStatus.BAD_REQUEST)
        self.assertEqual(self.request('/api/data')[0], HTTPStatus.OK)


if __name__ == '__main__':
    unittest.main()
