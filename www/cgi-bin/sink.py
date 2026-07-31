#!/usr/bin/env python3
# Stress-test CGI: consumes the entire request body and reports the size.
# Does NOT echo body back — that would balloon server output buffers.

import os
import sys

content_length = int(os.environ.get("CONTENT_LENGTH", 0))

# Read body in chunks (don't load 100MB into memory at once)
chunk_size = 65536
total_read = 0
while total_read < content_length:
    to_read = min(chunk_size, content_length - total_read)
    chunk = sys.stdin.buffer.read(to_read)
    if not chunk:
        break
    total_read += len(chunk)

print("Content-Type: text/plain")
print("Status: 200 OK")
print()
print(f"Received {total_read} bytes")
