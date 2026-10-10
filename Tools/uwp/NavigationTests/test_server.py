"""Verify fixture responses before exercising the native browser on-device."""
import gzip
import http.client
from http import HTTPStatus
import json
import threading
import time
import unittest
from server import (DEFAULT_LARGE_RESPONSE_BYTES, MAX_LARGE_RESPONSE_BYTES,
                    FixtureServer, INDEX, default_font, png)

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
