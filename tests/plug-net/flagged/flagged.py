#!/usr/bin/env python3
# test fixture. it never runs anything, the word subprocess is just here so the check flags it
import sys

for line in sys.stdin:
    if line.startswith("hello"):
        print("log flagged and running", flush=True)
