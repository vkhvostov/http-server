#!/usr/bin/env python3
"""Stress and load tests — server must stay available under load."""
import threading
import socket
import time
import sys
import os

PORT = int(os.environ.get("PORT", 8080))
HOST = os.environ.get("HOST", "127.0.0.1")

PASS = 0
FAIL = 0


def simple_get(timeout=10):
    """Return HTTP status code for GET /, or 0 on error."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((HOST, PORT))
        s.sendall(f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nConnection: close\r\n\r\n".encode())
        response = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
        s.close()
        line = response.split(b"\r\n")[0].decode(errors="replace")
        parts = line.split(" ", 2)
        return int(parts[1]) if len(parts) >= 2 else 0
    except Exception:
        return 0


def check(name, condition, detail=""):
    global PASS, FAIL
    if condition:
        print(f"  PASS: {name}")
        PASS += 1
    else:
        print(f"  FAIL: {name}" + (f" — {detail}" if detail else ""))
        FAIL += 1


def test_concurrent_load():
    """50 concurrent GET requests — >95% must succeed."""
    results = {"success": 0, "fail": 0}
    lock = threading.Lock()

    def worker():
        code = simple_get()
        with lock:
            if code == 200:
                results["success"] += 1
            else:
                results["fail"] += 1

    threads = [threading.Thread(target=worker) for _ in range(50)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    total = results["success"] + results["fail"]
    rate = results["success"] / total * 100 if total else 0
    print(f"  {results['success']}/{total} succeeded ({rate:.1f}%)")
    check("50 concurrent requests >95% success", rate > 95, f"{rate:.1f}%")


def test_sustained_load():
    """Sequential requests for 10 seconds — >95% must succeed."""
    end_time = time.time() + 10
    success = 0
    total = 0
    while time.time() < end_time:
        code = simple_get(timeout=5)
        total += 1
        if code == 200:
            success += 1
    rate = success / total * 100 if total else 0
    print(f"  {success}/{total} succeeded over 10s ({rate:.1f}%)")
    check("Sustained 10s load >95% success", rate > 95, f"{rate:.1f}%")


def test_rapid_connect_disconnect():
    """Open and immediately close 200 connections — server must survive."""
    for _ in range(200):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1)
            s.connect((HOST, PORT))
            s.close()
        except Exception:
            pass
    check("Server alive after 200 rapid connect/disconnect", simple_get() == 200)


def test_server_under_concurrent_load_still_responds():
    """While 20 threads hammer the server, a serial request must still get 200."""
    results = {"done": False}

    def hammerer():
        while not results["done"]:
            simple_get(timeout=5)

    threads = [threading.Thread(target=hammerer, daemon=True) for _ in range(20)]
    for t in threads:
        t.start()
    time.sleep(1)
    code = simple_get(timeout=10)
    results["done"] = True
    for t in threads:
        t.join(timeout=2)
    check("Serial request succeeds under concurrent load", code == 200, f"got {code}")


if __name__ == "__main__":
    print("=== Stress Tests ===")
    test_concurrent_load()
    test_sustained_load()
    test_rapid_connect_disconnect()
    test_server_under_concurrent_load_still_responds()
    print(f"\nResults: {PASS} passed, {FAIL} failed")
    if FAIL:
        sys.exit(1)
