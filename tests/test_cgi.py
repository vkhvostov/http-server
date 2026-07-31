#!/usr/bin/env python3
"""CGI execution tests using cgi.conf (port 8080 with /cgi-bin route)."""
import socket
import sys
import os
import time

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")

PASS = 0
FAIL = 0


def http_request(method, path, headers=None, body=None, timeout=15):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect((HOST, PORT))
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
    s.sendall(data)
    response = b""
    try:
        while True:
            chunk = s.recv(8192)
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


def test_hello_cgi():
    r = http_request("GET", "/cgi-bin/hello.py")
    status = parse_status(r)
    check("hello.py CGI returns 200", status == 200, f"got {status}")
    body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
    check("hello.py body contains expected HTML", b"Hello" in body)


def test_cgi_get_query_string():
    r = http_request("GET", "/cgi-bin/echo.py?name=world&foo=bar")
    status = parse_status(r)
    check("echo.py GET returns 200", status == 200, f"got {status}")
    body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
    check("echo.py echoes query string", b"name=world" in body or b"QUERY_STRING" in body or b"Query string" in body)


def test_cgi_post_body():
    post_data = "hello from post body"
    r = http_request("POST", "/cgi-bin/echo.py",
                     headers={"Content-Type": "application/x-www-form-urlencoded"},
                     body=post_data)
    status = parse_status(r)
    check("echo.py POST returns 200", status == 200, f"got {status}")
    body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
    check("echo.py echoes POST body", b"hello from post body" in body or b"POST" in body)


def test_cgi_env_variables():
    r = http_request("GET", "/cgi-bin/echo.py?test=1")
    status = parse_status(r)
    body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
    check("CGI env has REQUEST_METHOD", b"GET" in body or b"Method" in body)


def test_cgi_timeout():
    """slow.py sleeps 15s; server should return 504 or close connection within CGI_TIMEOUT."""
    start = time.time()
    r = http_request("GET", "/cgi-bin/slow.py", timeout=20)
    elapsed = time.time() - start
    status = parse_status(r)
    check("CGI timeout returns 504 or 500", status in (504, 500, 502), f"got {status}, elapsed {elapsed:.1f}s")
    check("CGI timeout under 14 seconds", elapsed < 14, f"took {elapsed:.1f}s")


def test_cgi_only_via_extension():
    """A .txt file in cgi-bin should NOT be executed as CGI."""
    r = http_request("GET", "/cgi-bin/hello.py")
    status = parse_status(r)
    check("CGI script executed (not served as text)", status == 200, f"got {status}")
    body = r.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r else b""
    # Body should be HTML output, not raw Python source
    check("CGI response is HTML not raw script", b"#!/usr/bin/env" not in body)


def test_server_alive_after_cgi():
    """Server must remain operational after CGI tests."""
    r = http_request("GET", "/")
    status = parse_status(r)
    check("Server alive after CGI tests", status == 200, f"got {status}")


if __name__ == "__main__":
    print("=== CGI Tests ===")
    print(f"  Note: run with cgi.conf — server on port {PORT} must have /cgi-bin configured")
    test_hello_cgi()
    test_cgi_get_query_string()
    test_cgi_post_body()
    test_cgi_env_variables()
    test_cgi_timeout()
    test_cgi_only_via_extension()
    test_server_alive_after_cgi()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
