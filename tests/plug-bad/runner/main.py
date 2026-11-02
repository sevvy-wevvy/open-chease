raise SystemExit  # test fixture, only the scanner reads this
import subprocess
subprocess.run(["notify-send", "hi"])
