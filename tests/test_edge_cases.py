#!/usr/bin/env python3
"""Edge case and robustness tests."""
import socket
import time
import sys
import os

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")

PASS = 0
FAIL = 0


def raw_send(data, timeout=5):
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


def server_still_alive():
    """Return True if server responds to a normal GET."""
    try:
        r = raw_send(f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n")
        return parse_status(r) == 200
    except Exception:
        return False


def test_empty_connection():
    """Connect and immediately close without sending anything."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2)
    s.connect((HOST, PORT))
    s.close()
    time.sleep(0.2)
    check("Server alive after empty connection", server_still_alive())


def test_partial_request():
    """Send request byte by byte."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((HOST, PORT))
    request = f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n".encode()
    for byte in request:
        s.send(bytes([byte]))
        time.sleep(0.005)
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
    status = parse_status(response)
    check("Byte-by-byte request returns 200", status == 200, f"got {status}")


def test_binary_garbage():
    """Send binary garbage — server must not crash."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3)
    s.connect((HOST, PORT))
    s.sendall(bytes(range(256)) * 4)
    response = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
    except (socket.timeout, ConnectionResetError):
        pass
    s.close()
    status = parse_status(response)
    check("Binary garbage handled gracefully (400 or closed)", status == 400 or len(response) == 0)
    check("Server alive after binary garbage", server_still_alive())


def test_uri_too_long():
    long_path = "/" + "A" * 9000
    r = raw_send(f"GET {long_path} HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n")
    status = parse_status(r)
    check("URI too long returns 414", status == 414, f"got {status}")


def test_very_long_header():
    long_val = "X" * 10000
    r = raw_send(f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nX-Long: {long_val}\r\nConnection: close\r\n\r\n")
    status = parse_status(r)
    # Server may reject with 400 or accept (implementation-defined) — must not crash
    check("Long header value handled (no crash)", status in (200, 400, 431), f"got {status}")
    check("Server alive after long header", server_still_alive())


def test_multiple_simultaneous_connections():
    """Open 50 simultaneous connections."""
    sockets = []
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2)
            s.connect((HOST, PORT))
            sockets.append(s)
        except Exception:
            break
    opened = len(sockets)
    # Send a valid request on the last socket
    got_response = False
    if sockets:
        try:
            sockets[-1].sendall(
                f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n".encode()
            )
            r = b""
            while True:
                chunk = sockets[-1].recv(4096)
                if not chunk:
                    break
                r += chunk
            got_response = b"HTTP/1.1" in r
        except Exception:
            pass
    for s in sockets:
        try:
            s.close()
        except Exception:
            pass
    check(f"Opened {opened} simultaneous connections", opened >= 10, f"only {opened}")
    check("Last socket got valid HTTP response", got_response)
    check("Server alive after many connections", server_still_alive())


def test_chunked_transfer_encoding():
    """Send chunked-encoded POST body."""
    request = (
        f"POST /uploads HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Transfer-Encoding: chunked\r\n"
        f"Content-Type: text/plain\r\n"
        f"Connection: close\r\n"
        f"\r\n"
        f"5\r\n"
        f"Hello\r\n"
        f"6\r\n"
        f" World\r\n"
        f"0\r\n"
        f"\r\n"
    ).encode()
    r = raw_send(request)
    status = parse_status(r)
    # Accept 200/201 (processed) or 411/501 (not supported) — must not hang or crash
    check("Chunked request gets a valid HTTP response", status != 0, f"got {status}")


def test_double_crlf_in_body():
    """Body containing CRLF sequences must not confuse header parsing."""
    body = b"line1\r\nline2\r\n\r\nline3"
    hdrs = (
        f"POST /uploads HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Type: text/plain\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + body
    r = raw_send(hdrs)
    status = parse_status(r)
    check("CRLF in body doesn't corrupt parsing", status in (200, 201, 405, 413), f"got {status}")


if __name__ == "__main__":
    print("=== Edge Case Tests ===")
    test_empty_connection()
    test_partial_request()
    test_binary_garbage()
    test_uri_too_long()
    test_very_long_header()
    test_multiple_simultaneous_connections()
    test_chunked_transfer_encoding()
    test_double_crlf_in_body()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
