#!/usr/bin/env python3
"""
Master test runner for http-server.

Usage:
    python3 tests/test_all.py [port]            # run all suites
    python3 tests/test_all.py [port] --no-cgi   # skip CGI suite (needs cgi.conf)
    python3 tests/test_all.py [port] --no-stress # skip stress suite

The server must already be running before invoking this script.
C++ unit tests (test_config, test_socket) are compiled and run automatically;
they do not require a running server.
"""
import subprocess
import sys
import os
import socket
import tempfile
import time


def wait_for_server(host, port, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1)
            s.connect((host, port))
            s.close()
            return True
        except Exception:
            time.sleep(0.2)
    return False


def run_cpp_test(src_path, root_dir):
    """Compile and run a C++ unit test against the server's object files.

    Returns True on success, False on compile or runtime failure.
    Prints the test output directly so it looks like any other suite.
    """
    import platform
    cxx = "c++"
    flags = ["-Wall", "-Wextra", "-Werror", "-std=c++98"]
    os_name = platform.system()
    if os_name == "Darwin":
        flags.append("-D OS_MAC")
    elif os_name == "Linux":
        flags.append("-D OS_LINUX")

    includes = [
        "-I", os.path.join(root_dir, "includes"),
        "-I", os.path.join(root_dir, "includes/network"),
        "-I", os.path.join(root_dir, "includes/http"),
    ]

    # All object files except main.o
    obj_dir = os.path.join(root_dir, "obj")
    obj_files = []
    for dirpath, _, filenames in os.walk(obj_dir):
        for fname in filenames:
            if fname.endswith(".o") and fname != "main.o":
                obj_files.append(os.path.join(dirpath, fname))

    if not obj_files:
        print("  ERROR: no object files found — run 'make' first")
        return False

    with tempfile.NamedTemporaryFile(suffix="", delete=False) as tmp:
        binary = tmp.name

    try:
        compile_cmd = [cxx] + flags + includes + [src_path] + obj_files + ["-o", binary]
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  COMPILE ERROR:\n{result.stderr}")
            return False

        run_result = subprocess.run([binary], capture_output=False, cwd=root_dir)
        return run_result.returncode == 0
    finally:
        try:
            os.unlink(binary)
        except OSError:
            pass


def main():
    port = "8080"
    skip = set()
    for arg in sys.argv[1:]:
        if arg.startswith("--no-"):
            skip.add(arg[5:])
        elif arg.isdigit():
            port = arg

    host = os.environ.get("HOST", "127.0.0.1")
    tests_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(tests_dir)

    # C++ unit tests run first — they don't need a server
    cpp_suites = [
        ("config", "test_config.cpp"),
        ("socket", "test_socket.cpp"),
    ]

    failed = []
    skipped = []

    for name, filename in cpp_suites:
        if name in skip:
            skipped.append(name)
            continue
        path = os.path.join(tests_dir, filename)
        print(f"\n{'=' * 60}")
        print(f"Running {filename}")
        print("=" * 60)
        if not run_cpp_test(path, root_dir):
            failed.append(filename)
            print(f"FAILED: {filename}")

    # Python HTTP suites require a live server
    print(f"\nConnecting to server at {host}:{port}...")
    if not wait_for_server(host, int(port)):
        print(f"ERROR: server not reachable at {host}:{port}")
        sys.exit(1)
    print("Server is up.\n")

    py_suites = [
        ("basic",      "test_basic.py"),
        ("methods",    "test_methods.py"),
        ("errors",     "test_errors.py"),
        ("upload",     "test_upload.py"),
        ("edge_cases", "test_edge_cases.py"),
        ("cgi",        "test_cgi.py"),
        ("stress",     "test_stress.py"),
    ]

    for name, filename in py_suites:
        if name in skip:
            skipped.append(name)
            continue
        path = os.path.join(tests_dir, filename)
        print(f"\n{'=' * 60}")
        print(f"Running {filename}")
        print("=" * 60)
        env = {**os.environ, "PORT": port, "HOST": host}
        result = subprocess.run([sys.executable, path], env=env)
        if result.returncode != 0:
            failed.append(filename)
            print(f"FAILED: {filename}")

    total = len(cpp_suites) + len(py_suites) - len(skipped)
    print(f"\n{'=' * 60}")
    if skipped:
        print(f"Skipped: {', '.join(skipped)}")
    if failed:
        print(f"{len(failed)} suite(s) FAILED: {', '.join(failed)}")
        sys.exit(1)
    else:
        print(f"ALL {total} TEST SUITE(S) PASSED")


if __name__ == "__main__":
    main()
