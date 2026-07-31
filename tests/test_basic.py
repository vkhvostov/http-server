#!/usr/bin/env python3
"""Basic HTTP functionality tests."""
import socket
import sys
import os

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")
BASE = f"http://{HOST}:{PORT}"

PASS = 0
FAIL = 0


def http_request(method, path, headers=None, body=None):
    """Send a raw HTTP/1.1 request and return the raw response."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((HOST, PORT))
    hdrs = {"Host": f"{HOST}:{PORT}", "Connection": "close"}
    if headers:
        hdrs.update(headers)
    if body is not None:
        hdrs["Content-Length"] = str(len(body))
    request = f"{method} {path} HTTP/1.1\r\n"
    for k, v in hdrs.items():
        request += f"{k}: {v}\r\n"
    request += "\r\n"
    data = request.encode()
    if body is not None:
        data += body if isinstance(body, bytes) else body.encode()
    s.sendall(data)
    response = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
    except socket.timeout:
        pass
    s.close()
    return response


def parse_status(response):
    """Extract HTTP status code from response bytes."""
    line = response.split(b"\r\n")[0].decode(errors="replace")
    parts = line.split(" ", 2)
    return int(parts[1]) if len(parts) >= 2 else 0


def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        print(f"  PASS: {name}")
        PASS += 1
    else:
        print(f"  FAIL: {name}" + (f" — {detail}" if detail else ""))
        FAIL += 1


def test_get_root():
    r = http_request("GET", "/")
    status = parse_status(r)
    check("GET / returns 200", status == 200, f"got {status}")
    check("GET / contains HTML", b"text/html" in r.lower() or b"<!DOCTYPE" in r or b"<html" in r.lower())


def test_required_headers():
    r = http_request("GET", "/")
    check("Content-Length header present", b"content-length:" in r.lower())
    check("Content-Type header present", b"content-type:" in r.lower())
    check("Date header present", b"date:" in r.lower())


def test_connection_close():
    r = http_request("GET", "/", headers={"Connection": "close"})
    status = parse_status(r)
    check("Connection: close — 200 response", status == 200, f"got {status}")


def test_keep_alive():
    """Two requests on one persistent connection."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((HOST, PORT))
    req = (
        f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: keep-alive\r\n\r\n"
        f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n"
    ).encode()
    s.sendall(req)
    response = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
    except socket.timeout:
        pass
    s.close()
    count = response.count(b"HTTP/1.1 200")
    check("Keep-alive: two 200 responses on one connection", count >= 2, f"got {count} responses")


def test_raw_socket():
    r = http_request("GET", "/")
    check("Raw socket GET / — HTTP/1.1 200", b"HTTP/1.1 200" in r)


def test_http_version():
    r = http_request("GET", "/")
    check("Response uses HTTP/1.1", r.startswith(b"HTTP/1.1"))


if __name__ == "__main__":
    print("=== Basic HTTP Tests ===")
    test_get_root()
    test_required_headers()
    test_connection_close()
    test_keep_alive()
    test_raw_socket()
    test_http_version()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
