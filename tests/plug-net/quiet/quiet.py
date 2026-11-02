#!/usr/bin/env python3
import sys

for line in sys.stdin:
    if line.startswith("hello"):
        print("menu hi Say hi | test menu item", flush=True)
        print("menu bye Wave", flush=True)
        print("log quiet and running", flush=True)
    elif line.startswith("click"):
        print("note clicked " + line.split()[1], flush=True)
