#!/usr/bin/env python3
# Crashes immediately with exit code 1, before printing anything.
# Used to verify CGI captures non-zero exit status.

import sys
sys.exit(1)
