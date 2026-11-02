#!/usr/bin/env python3
# test fixture: opens one tcp connection to a listener the test started on this machine, then just reads stdin
import os
import socket
import sys

host, port = os.environ.get("OC_TEST_HOST"), int(os.environ.get("OC_TEST_PORT", "0"))
s = None
for line in sys.stdin:
    if line.startswith("hello") and host and port:
        s = socket.create_connection((host, port), timeout=3)
        print("log connected", flush=True)
