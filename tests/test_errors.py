#!/usr/bin/env python3
"""Error handling tests: 4xx and 5xx status codes."""
import socket
import sys
import os

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")

PASS = 0
FAIL = 0


def raw_send(data, timeout=5):
    """Send raw bytes and return full response."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((HOST, PORT))
    s.sendall(data if isinstance(data, bytes) else data.encode())
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


def http_request(method, path, headers=None, body=None):
    hdrs = {"Host": f"{HOST}:{PORT}", "Connection": "close"}
    if headers:
        hdrs.update(headers)
    if body is not None:
        length = len(body) if isinstance(body, (bytes, bytearray)) else len(body.encode())
        hdrs["Content-Length"] = str(length)
    req = f"{method} {path} HTTP/1.1\r\n"
    for k, v in hdrs.items():
        req += f"{k}: {v}\r\n"
    req += "\r\n"
    data = req.encode()
    if body is not None:
        data += body if isinstance(body, (bytes, bytearray)) else body.encode()
    return raw_send(data)


def parse_status(response):
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


def test_404_not_found():
    r = http_request("GET", "/no_such_path_xyz_abc")
    status = parse_status(r)
    check("404 for missing resource", status == 404, f"got {status}")
    check("404 response has body", b"\r\n\r\n" in r and len(r.split(b"\r\n\r\n", 1)[1]) > 0)


def test_405_method_not_allowed():
    r = http_request("DELETE", "/")
    status = parse_status(r)
    check("405 for DELETE on GET-only route", status == 405, f"got {status}")


def test_413_body_too_large():
    # Send 2MB body against default 1M limit.
    # Server may close the connection mid-send (RST/EPIPE) after seeing Content-Length,
    # so we use a low-level socket and tolerate BrokenPipeError.
    large_body = b"X" * (2 * 1024 * 1024)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((HOST, PORT))
    hdrs = (
        f"POST /uploads HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Type: text/plain\r\n"
        f"Content-Length: {len(large_body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()
    s.sendall(hdrs)
    # Send body in chunks; server may close early
    chunk_size = 65536
    sent = 0
    response = b""
    while sent < len(large_body):
        try:
            s.send(large_body[sent:sent + chunk_size])
            sent += chunk_size
        except (BrokenPipeError, ConnectionResetError, OSError):
            break
        # Read any early response
        s.settimeout(0.1)
        try:
            chunk = s.recv(4096)
            if chunk:
                response += chunk
        except socket.timeout:
            pass
        s.settimeout(5)
    # Drain remaining response
    s.settimeout(2)
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
    except (socket.timeout, OSError):
        pass
    s.close()
    status = parse_status(response)
    check("413 for body exceeding max size", status == 413, f"got {status}")


def test_414_uri_too_long():
    long_path = "/" + "A" * 9000
    r = raw_send(f"GET {long_path} HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n")
    status = parse_status(r)
    check("414 for URI > 8192 bytes", status == 414, f"got {status}")


def test_400_bad_request():
    # Malformed request line (no HTTP version)
    r = raw_send(b"BADREQUEST\r\n\r\n")
    status = parse_status(r)
    check("400 for malformed request", status == 400, f"got {status}")


def test_400_missing_host():
    # HTTP/1.1 requires Host header
    r = raw_send(b"GET / HTTP/1.1\r\n\r\n")
    status = parse_status(r)
    check("400 for missing Host header in HTTP/1.1", status == 400, f"got {status}")


def test_custom_error_page():
    r = http_request("GET", "/definitely_missing_xyz")
    status = parse_status(r)
    if status == 404:
        body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
        check("Custom 404 page has HTML content", b"<" in body or b"404" in body)
    else:
        check("Custom 404 page — skipped (status not 404)", True)


def test_server_never_crashes():
    """After all error tests, server still responds."""
    r = http_request("GET", "/")
    status = parse_status(r)
    check("Server still alive after error tests", status == 200, f"got {status}")


if __name__ == "__main__":
    print("=== Error Handling Tests ===")
    test_404_not_found()
    test_405_method_not_allowed()
    test_413_body_too_large()
    test_414_uri_too_long()
    test_400_bad_request()
    test_400_missing_host()
    test_custom_error_page()
    test_server_never_crashes()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
