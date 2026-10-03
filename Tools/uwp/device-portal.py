#!/usr/bin/env python3
"""Deploy and diagnose the runtime smoke app through Windows Device Portal."""
import argparse
import base64
import http.cookiejar
import json
from pathlib import Path
import ssl
import urllib.error
import urllib.parse
import urllib.request
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--insecure", action="store_true", help="Accept this device's self-signed HTTPS certificate")
    parser.add_argument("--pin", help="Current Device Discovery pairing PIN")
    sub = parser.add_subparsers(dest="command", required=True)
    get = sub.add_parser("get")
    get.add_argument("path")
    post = sub.add_parser("post")
    post.add_argument("path")
    install = sub.add_parser("install")
    install.add_argument("package", type=Path)
    launch = sub.add_parser("launch")
    launch.add_argument("app_id")
    launch.add_argument("package_full_name")
    args = parser.parse_args()
    jar = http.cookiejar.CookieJar()
    context = ssl._create_unverified_context() if args.insecure else ssl.create_default_context()
    opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context), urllib.request.HTTPCookieProcessor(jar))
    base = args.url.rstrip("/")
    with opener.open(base + "/api/os/info", timeout=30) as response:
        response.read()
    token = next((cookie.value for cookie in jar if cookie.name == "CSRF-Token"), None)
    headers = {"X-CSRF-Token": urllib.parse.unquote(token)} if token else {}
    if args.pin:
        pair_path = "/api/authorize/pair?" + urllib.parse.urlencode({"pin": args.pin, "persistent": "0"})
        try:
            with opener.open(urllib.request.Request(base + pair_path, data=b"", headers=headers), timeout=30) as response:
                response.read()
        except urllib.error.HTTPError as error:
            print(f"Device Portal pairing rejected: HTTP {error.code}")
            print(error.read().decode("utf-8", errors="replace"))
            raise SystemExit(1)
    data = None
    if args.command in ("get", "post"):
        path = args.path
        if args.command == "post":
            data = b""
    elif args.command == "install":
        boundary = "WebKitSmoke" + uuid.uuid4().hex
        data = (f'--{boundary}\r\nContent-Disposition: form-data; name="package"; filename="{args.package.name}"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode() + args.package.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
        headers["Content-Type"] = "multipart/form-data; boundary=" + boundary
        path = "/api/app/packagemanager/package?" + urllib.parse.urlencode({"package": args.package.name})
    else:
        encode = lambda text: base64.b64encode(text.encode()).decode()
        path = "/api/taskmanager/app?" + urllib.parse.urlencode({"appid": encode(args.app_id), "package": encode(args.package_full_name)})
        data = b""
    request = urllib.request.Request(base + path, data=data, headers=headers)
    try:
        with opener.open(request, timeout=180) as response:
            body = response.read().decode("utf-8", errors="replace")
            print(f"HTTP {response.status}")
            try:
                print(json.dumps(json.loads(body), indent=2))
            except json.JSONDecodeError:
                print(body)
    except urllib.error.HTTPError as error:
        print(f"HTTP {error.code}")
        print(error.read().decode("utf-8", errors="replace"))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
