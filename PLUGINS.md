<h1 align="center">🧩 open-chease plugins 🧩</h1>

<p align="center">
Plugins are little programs that run next to open-chease. They get told whats going on (board found, every move, the best move, game over) and can draw stuff on the overlay and add their own settings. They can even read bits of the screen.<br>
Write them in anything that can read and print lines: python, C, a shell script, a .bat or .ps1 on windows, whatever you want.
</p>

<details>
<summary align="center"><b>📁 Installing One 📁</b></summary>

<p align="center">
Every plugin gets its own folder inside the <code>plugins</code> folder in the open-chease data folder:<br> <br>
<b>Linux</b> <code>~/.local/share/open-chease/plugins</code><br>
<b>Windows</b> <code>%LOCALAPPDATA%\open-chease\plugins</code><br>
<b>Mac</b> <code>~/Library/Application Support/open-chease/plugins</code><br> <br>
Easiest way there is Gear > Plugins > <b>Open plugins folder</b>. Drop the plugin folder in, restart open-chease, then turn it on in the Plugins tab.<br>
Plugins are just programs on your computer, so only run ones you trust or ones you read yourself.<br> <br>
Theres 2 examples in <code>plugins-examples/</code> you can copy over: <b>announce</b> (writes the last move on the board and in a box, has a few settings) and <b>avgcolor</b> (reads a bit of the screen and shows its average color).
</p>
</details>

<details>
<summary align="center"><b>📝 plugin.txt 📝</b></summary>

<p align="center">
Each folder needs a <code>plugin.txt</code>. Only <b>run</b> is needed, the rest is for the Plugins tab:
</p>

```
name = Move announcer
run = announce.py
description = writes the last move on the board and in a little box
version = 1.0
```

<p align="center">
The folder name is the plugins id (letters, numbers, <code>-</code> <code>_</code> <code>.</code>), its used in the settings file.<br>
<b>run</b> is a command line. If the first word is a file in the plugin folder it runs that, otherwise it looks on PATH, so <code>run = python3 main.py --fast</code> works too. It always starts inside the plugin folder.<br>
On linux and mac a <code>.py</code> without the x bit runs with python3 and anything else without it runs with sh (zips love to lose the x bit). On windows <code>.py</code> runs with py (or python), <code>.bat</code>/<code>.cmd</code> with cmd and <code>.ps1</code> with powershell.
</p>
</details>

<details>
<summary align="center"><b>💬 How Talking Works 💬</b></summary>

<p align="center">
Same idea as UCI. open-chease writes lines to your stdin and reads lines from your stdout, one thing per line, words split by spaces.<br>
<b>Flush after you print</b> (<code>print(..., flush=True)</code> in python) or nothing shows up.<br>
Anything you print that it doesnt know gets ignored, so a plugin made for a newer version still runs on an older one. stderr shows up when you run open-chease with <code>-v</code>.<br>
Positions on the board are either a square (<code>e4</code>) or <code>x y</code> in pixels from the top left corner of the board. Colors are <code>#rgb</code>, <code>#rrggbb</code> or <code>#aarrggbb</code> (aa is how see-through, 00 is invisible).
</p>
</details>

<details>
<summary align="center"><b>📥 What You Get 📥</b></summary>

<p align="center">
<code>hello 1 announce</code> - first thing you get, 1 is the protocol version and then your folder name. Send your settings here<br>
<code>board 220 140 576 white</code> - found the board: x y on screen, its size in px and which side is at the bottom<br>
<code>board none</code> - lost the board<br>
<code>team white</code> - which side the user plays<br>
<code>position rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2</code> - the FEN, after every move and when the board is found<br>
<code>move e7e5 e5</code> - the move that was just played, uci then SAN<br>
<code>best g1f3 Nf3 cp 36 11</code> - engine best move, uci, SAN, <code>cp</code> or <code>mate</code>, the score (for the side to move) and the depth. Comes again every time it gets deeper<br>
<code>gameover checkmate win</code> - checkmate or stalemate, then win, loss or draw for the user<br>
<code>set onboard 1</code> - a setting value. You get one for every setting right after you declare it (with whatever was saved) and again when the user changes it<br>
<code>click test</code> - the user hit one of your buttons<br>
<code>tick 12345</code> - only if you asked for ticks, the number is a clock in ms<br>
<code>frame 576 576 /path/to/plugins/avgcolor/frame.ppm</code> - the screen bit you asked for is ready (see below)<br>
<code>frame none busy</code> - you didnt get one, the rest says why
</p>
</details>

<details>
<summary align="center"><b>📤 What You Can Send 📤</b></summary>

<p align="center">
<b>Settings</b> (show up in Gear > Plugins under your plugin and get saved for you, label and tip are split by <code>|</code>):<br>
<code>setting bool onboard 1 On the board | writes the move on the square</code> - on/off, then the default<br>
<code>setting slider every 2 1 10 Every (sec) | how often it looks again</code> - default, min, max<br>
<code>setting choice style 0 Short,In_words Move as | Nf3 or knight to f3</code> - default, then the options split by commas (<code>_</code> becomes a space)<br>
<code>setting button test Say hi | pops a note</code> - you get <code>click test</code> when its pressed<br>
<code>tab My stuff</code> - puts your settings in their own tab instead of the Plugins one<br> <br>
<b>Drawing</b> (nothing shows until you send <code>show</code>, then it replaces everything you drew before):<br>
<code>draw text e4 #ffffff Nf3</code> - small text, on a square or at x y<br>
<code>draw big 10 -40 #ff0 hello</code> - same but bigger<br>
<code>draw rect -34 0 26 26 #80ff0000</code> - filled box: where, width, height, color<br>
<code>draw box -34 0 26 26 #fff</code> - just the outline<br>
<code>draw arrow g1 f3 #1fe0e0</code> - arrow from one square to another<br>
<code>draw square e4 #801fe0e0</code> - colors a square<br>
<code>panel Last move</code> - a little box next to the board with this title<br>
<code>line #1fe0e0 Nf3</code> - a line in that box, the color is optional<br>
<code>show</code> - shows all of the above<br>
<code>clear</code> - wipes your drawings and box right away<br> <br>
<b>Other stuff</b>:<br>
<code>note hi there</code> - shows a note in the status line for a few secs<br>
<code>log something happened</code> - shows under your plugin in the Plugins tab (and in <code>-v</code>)<br>
<code>tick 1000</code> - get a <code>tick</code> every 1000 ms (100 is the fastest, 0 turns it off)<br>
<code>frame 0 0 576 576</code> - read the screen, x y w h from the top left of the board (negative works, so <code>frame 0 -60 576 50</code> is the strip above it). Max 1024x1024, once every 100 ms. You get back a binary PPM file (P6, rgb) in your plugin folder, at real screen pixels so on a 2x screen its twice as big
</p>
</details>

<details>
<summary align="center"><b>🐍 Tiny Example 🐍</b></summary>

<p align="center">
Puts a green square on wherever the best move goes, with a setting to turn it off:
</p>

```python
#!/usr/bin/env python3
import sys

on = 1
for line in sys.stdin:
    w = line.split()
    if not w:
        continue
    if w[0] == "hello":
        print("setting bool on 1 Show it | green square on the best moves target", flush=True)
    elif w[0] == "set" and w[1] == "on":
        on = int(w[2])
        if not on:
            print("clear", flush=True)
    elif w[0] == "best" and on:
        print(f"draw square {w[1][2:4]} #8000ff00", flush=True)
        print("show", flush=True)
```

<p align="center">
With <code>run = best.py</code> in plugin.txt. Thats it.
</p>
</details>

<details>
<summary align="center"><b>🚧 Limits 🚧</b></summary>

<p align="center">
So a broken plugin cant take open-chease down with it:<br>
Lines longer than 4096 bytes get thrown away, bad lines get ignored<br>
24 settings, 160 drawings and 10 box lines per plugin, 12 plugins<br>
If you stop reading your stdin and stuff piles up, or you quit, it turns you off and says why in the Plugins tab. Turn it off and on again to restart it<br>
Plugins get killed when open-chease closes
</p>
</details>
