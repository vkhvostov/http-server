#!/usr/bin/env python3
"""File upload tests: multipart/form-data via POST, DELETE."""
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
    s.settimeout(10)
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


def multipart_body(filename, content, boundary="boundary123"):
    if isinstance(content, str):
        content = content.encode()
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
        f"Content-Type: application/octet-stream\r\n"
        f"\r\n"
    ).encode() + content + f"\r\n--{boundary}--\r\n".encode()
    return body, boundary


def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        print(f"  PASS: {name}")
        PASS += 1
    else:
        print(f"  FAIL: {name}" + (f" — {detail}" if detail else ""))
        FAIL += 1


def test_simple_upload():
    filename = f"simple_{int(time.time())}.txt"
    body, boundary = multipart_body(filename, "hello world")
    r = http_request("POST", "/uploads",
                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
                     body=body)
    status = parse_status(r)
    check("Simple upload returns 200 or 201", status in (200, 201), f"got {status}")
    return filename


def test_upload_and_retrieve():
    filename = f"retrieve_{int(time.time())}.txt"
    content = b"test content for retrieval"
    body, boundary = multipart_body(filename, content)
    r_post = http_request("POST", "/uploads",
                          headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
                          body=body)
    post_status = parse_status(r_post)
    if post_status not in (200, 201):
        check("Upload then retrieve — upload failed", False, f"POST returned {post_status}")
        return
    r_get = http_request("GET", f"/uploads/{filename}")
    get_status = parse_status(r_get)
    check("Uploaded file is retrievable via GET", get_status == 200, f"got {get_status}")
    body_part = r_get.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in r_get else b""
    check("Retrieved file has correct content", content in body_part)
    return filename


def test_upload_delete_cycle():
    filename = f"cycle_{int(time.time())}.txt"
    body, boundary = multipart_body(filename, "temporary data")
    http_request("POST", "/uploads",
                 headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
                 body=body)
    r_del = http_request("DELETE", f"/uploads/{filename}")
    del_status = parse_status(r_del)
    check("DELETE returns 200 or 204", del_status in (200, 204), f"got {del_status}")
    r_get = http_request("GET", f"/uploads/{filename}")
    get_status = parse_status(r_get)
    check("File gone after DELETE", get_status == 404, f"got {get_status}")


def test_upload_too_large():
    filename = "toobig.bin"
    large_content = b"X" * (2 * 1024 * 1024)  # 2MB, default limit is 1M
    body, boundary = multipart_body(filename, large_content)
    # Server may close mid-send; use chunked sending with BrokenPipeError tolerance
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect((HOST, PORT))
    headers_str = (
        f"POST /uploads HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()
    s.sendall(headers_str)
    chunk_size = 65536
    sent = 0
    response = b""
    while sent < len(body):
        try:
            s.send(body[sent:sent + chunk_size])
            sent += chunk_size
        except (BrokenPipeError, ConnectionResetError, OSError):
            break
        s.settimeout(0.1)
        try:
            chunk = s.recv(4096)
            if chunk:
                response += chunk
        except socket.timeout:
            pass
        s.settimeout(10)
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
    check("Upload exceeding body limit returns 413", status == 413, f"got {status}")


def test_upload_to_wrong_route():
    filename = "wrong_route.txt"
    body, boundary = multipart_body(filename, "data")
    r = http_request("POST", "/",
                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
                     body=body)
    status = parse_status(r)
    check("POST upload to GET-only route returns 405", status == 405, f"got {status}")


def test_directory_listing():
    r = http_request("GET", "/uploads/")
    status = parse_status(r)
    check("GET /uploads/ returns 200 with autoindex", status == 200, f"got {status}")


if __name__ == "__main__":
    print("=== Upload Tests ===")
    test_simple_upload()
    test_upload_and_retrieve()
    test_upload_delete_cycle()
    test_upload_too_large()
    test_upload_to_wrong_route()
    test_directory_listing()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
