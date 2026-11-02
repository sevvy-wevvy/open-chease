raise SystemExit  # test fixture, only the scanner reads this
A = "~/.config/autostart/thing.desktop"
B = r"Software\Microsoft\Windows\CurrentVersion\Run"
C = "~/Library/LaunchAgents/thing.plist"
D = "schtasks /create /tn thing"
# crontab -l
