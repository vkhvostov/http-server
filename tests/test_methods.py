#!/usr/bin/env python3
"""HTTP method tests: GET, POST, DELETE, and unsupported methods."""
import socket
import sys
import os
import time

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")

PASS = 0
FAIL = 0


def http_request(method, path, headers=None, body=None):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect((HOST, PORT))
    hdrs = {"Host": f"{HOST}:{PORT}", "Connection": "close"}
    if headers:
        hdrs.update(headers)
    if body is not None:
        hdrs["Content-Length"] = str(len(body) if isinstance(body, (bytes, bytearray)) else len(body.encode()))
    request = f"{method} {path} HTTP/1.1\r\n"
    for k, v in hdrs.items():
        request += f"{k}: {v}\r\n"
    request += "\r\n"
    data = request.encode()
    if body is not None:
        data += body if isinstance(body, (bytes, bytearray)) else body.encode()
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


def test_get_existing_file():
    r = http_request("GET", "/index.html")
    status = parse_status(r)
    check("GET /index.html returns 200", status == 200, f"got {status}")
    check("GET /index.html returns body", b"\r\n\r\n" in r and len(r.split(b"\r\n\r\n", 1)[1]) > 0)


def test_get_nonexistent():
    r = http_request("GET", "/does_not_exist_xyz.html")
    status = parse_status(r)
    check("GET missing file returns 404", status == 404, f"got {status}")


def test_post_upload():
    boundary = "testboundary1234"
    filename = f"test_upload_{int(time.time())}.txt"
    content = b"Hello from POST upload test"
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n"
        f"\r\n"
    ).encode() + content + f"\r\n--{boundary}--\r\n".encode()
    r = http_request(
        "POST", "/uploads",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        body=body
    )
    status = parse_status(r)
    check("POST multipart upload returns 201 or 200", status in (200, 201), f"got {status}")
    return filename


def test_delete_uploaded_file():
    # First upload a file
    boundary = "delboundary5678"
    filename = f"test_delete_{int(time.time())}.txt"
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: text/plain\r\n"
        f"\r\n"
        f"delete me\r\n"
        f"--{boundary}--\r\n"
    ).encode()
    r = http_request(
        "POST", "/uploads",
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        body=body
    )
    upload_status = parse_status(r)
    if upload_status not in (200, 201):
        check("DELETE uploaded file — upload succeeded first", False, f"upload returned {upload_status}")
        return
    # Now delete
    r2 = http_request("DELETE", f"/uploads/{filename}")
    status = parse_status(r2)
    check("DELETE uploaded file returns 200 or 204", status in (200, 204), f"got {status}")
    # Verify gone
    r3 = http_request("GET", f"/uploads/{filename}")
    status3 = parse_status(r3)
    check("File is gone after DELETE", status3 == 404, f"got {status3}")


def test_put_not_allowed():
    r = http_request("PUT", "/index.html", body="data")
    status = parse_status(r)
    check("PUT returns 405", status == 405, f"got {status}")


def test_patch_not_allowed():
    r = http_request("PATCH", "/index.html", body="data")
    status = parse_status(r)
    check("PATCH returns 405", status == 405, f"got {status}")


def test_post_to_readonly_route():
    r = http_request("POST", "/", body="data",
                     headers={"Content-Type": "text/plain"})
    status = parse_status(r)
    check("POST to GET-only route returns 405", status == 405, f"got {status}")


def test_delete_nonexistent():
    r = http_request("DELETE", "/uploads/no_such_file_xyz.txt")
    status = parse_status(r)
    check("DELETE non-existent file returns 404", status == 404, f"got {status}")


if __name__ == "__main__":
    print("=== Method Tests ===")
    test_get_existing_file()
    test_get_nonexistent()
    test_post_upload()
    test_delete_uploaded_file()
    test_put_not_allowed()
    test_patch_not_allowed()
    test_post_to_readonly_route()
    test_delete_nonexistent()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
