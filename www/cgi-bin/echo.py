#!/usr/bin/env python3
# CGI script that echoes back request method, query string, and POST body.

import os
import sys

print("Content-Type: text/plain")
print("Status: 200 OK")
print()

print("=== CGI Echo ===")
print(f"Method: {os.environ.get('REQUEST_METHOD', '(not set)')}")
print(f"Query string: {os.environ.get('QUERY_STRING', '(not set)')}")
print(f"Content length: {os.environ.get('CONTENT_LENGTH', '0')}")

content_length = int(os.environ.get("CONTENT_LENGTH", 0))
if content_length > 0:
    body = sys.stdin.read(content_length)
    print(f"Body: {body}")
else:
    print("Body: (empty)")
