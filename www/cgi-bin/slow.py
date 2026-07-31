#!/usr/bin/env python3
# Sleeps longer than CGI_TIMEOUT to test the timeout/SIGKILL path.

import time

time.sleep(15)  # CGI_TIMEOUT is 10 seconds, so this should be killed

print("Content-Type: text/plain")
print()
print("This line should never be sent — server killed me first.")
