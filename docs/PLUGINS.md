<h1 align="center">🧩 open-chease plugins 🧩</h1>

<p align="center">
<b>Little programs that run next to open-chease and draw on the overlay.</b>
</p>

<p align="center">
A plugin gets told whats going on (board found, every move, the best move, game over) and can draw stuff on the board, put a box next to it, add settings, add items to the ••• menu and even read bits of the screen.<br>
You can write one in python, javascript, typescript, C, C++, C# or rust, or really anything that can read and print lines.<br> <br>
Never made one? Start at Your First Plugin and go down in order, it takes like 10 minutes.<br>
Already know what youre doing? Skip to The Cheat Sheet, everything after it is the full reference.<br> <br>
Made something cool? Please let me know!
<a href="https://sevvy-wevvy.com/discord">Discord</a>
</p>

<details>
<summary align="center"><b>🐣 Your First Plugin 🐣</b></summary>

<p align="center">
<b>1. Whats a plugin?</b><br>
Its a normal program on your computer. open-chease starts it when you turn it on, then sends it lines of text like <code>move e2e4 e4</code>. The plugin answers with lines like <code>draw arrow g1 f3 #1fe0e0</code> and open-chease draws that on the board.<br> <br>
<b>2. Install python</b><br>
Python is the easiest one to start with. Check if you have it by typing <code>python3 --version</code> in a terminal (<code>py --version</code> on windows).<br>
Dont have it? On windows and mac get it from <a href="https://www.python.org/downloads/">python.org</a>, on windows tick the "Add python.exe to PATH" box. On linux its almost always there already, if not install <code>python3</code> with your package manager.<br> <br>
<b>3. Get the template</b><br>
Open Gear > <b>Plugins</b>. If you dont have any plugins yet hit the Get the template button.<br>
Already have some? Paste this into the box at the top and hit <b>Add plugin</b>:<br>
<code>https://github.com/sevvy-wevvy/open-chease/tree/plugin-template/template</code><br>
You can also just look at it on github first: <a href="https://github.com/sevvy-wevvy/open-chease/tree/plugin-template">plugin-template branch</a>. The python one uses every part of the plugin system and has notes all through it.<br>
That branch also has a small example for every other language, each in its own folder. Want one? Paste the link to its folder, like <code>.../tree/plugin-template/rust</code>.<br> <br>
<b>4. Turn it on</b><br>
<b>Plugin template</b> shows up in the list, turned off (every plugin starts off). Flip its switch.<br>
A new Template tab shows up in the settings with its settings, and when theres a game on screen it draws an arrow on the best move and a little box next to the board.<br> <br>
<b>5. Change one line</b><br>
Hit Folder on its row. Open <code>template.py</code> in any text editor (Notepad, TextEdit, VS Code, whatever).<br>
Find this line:
</p>

```python
        send("note hi from the plugin template")
```

<p align="center">
Change the text to anything, like <code>send("note my first plugin works")</code>, and save.<br> <br>
<b>6. Restart it and see it change</b><br>
Back in the Plugins tab flip the switch off and on again, that restarts it with your new code.<br>
Go to the Template tab and hit Say hi. Your text pops up in the status line. Thats it, you made a plugin!<br> <br>
<b>7. Make it yours</b><br>
Before you change a lot, copy the <code>template</code> folder to a new name inside the plugins folder (like <code>myplugin</code>). Adding the template again overwrites the <code>template</code> folder, so dont keep your work in there.<br>
The folder name is your plugins id, pick it once. Change name and description in its <code>plugin.txt</code>, open the Plugins tab again so it shows up, turn it on and turn the old template off. Then rip out whatever you dont need from the code.<br> <br>
<b>If it doesnt work</b><br>
The row turns red and says why (like "quit right away, check it runs"). Run open-chease with <code>-v</code> to see your plugins errors, anything it prints to stderr shows up there.<br>
The number one mistake is forgetting to flush, see How Talking Works.
</p>
</details>

<details>
<summary align="center"><b>🌍 Sharing It 🌍</b></summary>

<p align="center">
Put the plugin folder on github. Either the files sit at the top of the repo (<code>plugin.txt</code> right there), or each plugin is a folder inside the repo if you have a few.<br>
Then people paste the link into Gear > Plugins and hit <b>Add plugin</b>. All of these work:<br> <br>
<code>someone/their-plugin</code><br>
<code>https://github.com/someone/their-plugin</code><br>
<code>https://github.com/someone/repo/tree/main/myplugin</code> (one folder)<br>
<code>https://github.com/someone/repo/tree/some-branch</code> (a branch, every folder in it with a plugin.txt gets added)<br> <br>
A plugin at the top of a repo gets the repo name as its folder name, one in a folder keeps the folder name. Adding the same one again updates it.<br>
Put <code>url = </code> and <code>update = </code> with your repo link in plugin.txt so people can open your page and get an Update button.<br> <br>
Dont upload built programs, <code>frame.ppm</code> or <code>build.log</code>. Compiled plugins ship source only and get built on the users computer (see Languages), a program file in the folder gets flagged because nobody can read it. A <code>.gitignore</code> like this does it:
</p>

```
frame.ppm
frame.ppm.tmp
build.log
myplugin
myplugin.exe
out/
obj/
bin/
```
</details>

<details>
<summary align="center"><b>🗣️ Languages 🗣️</b></summary>

<p align="center">
Set <code>lang</code> in plugin.txt so open-chease knows what to look for. If you leave it out it guesses from <code>run</code> and your files.<br>
If that language isnt installed the plugin doesnt load, its row says whats missing with a Get link and a Check again button for after you installed it.<br> <br>
Each one below is a full working plugin that says hi and draws an arrow on the best move. Every file goes in your plugin folder next to <code>plugin.txt</code>.<br>
Bigger ones (with a menu item and a box) are in their own folders on the <a href="https://github.com/sevvy-wevvy/open-chease/tree/plugin-template">plugin-template</a> branch.
</p>

<p align="center">
<b>🐍 Python</b> - get it from <a href="https://www.python.org/downloads/">python.org</a> (linux usually has it). Runs with <code>python3</code>, or <code>py</code> on windows
</p>

```
name = My plugin
lang = python
run = main.py
```

```python
import sys

for line in sys.stdin:
    w = line.split()
    if not w:
        continue
    if w[0] == "hello":
        print("log hi from python", flush=True)
    elif w[0] == "best":
        print(f"draw arrow {w[1][:2]} {w[1][2:4]} #1fe0e0", flush=True)
        print("show", flush=True)
```

<p align="center">
<b>🟨 JavaScript</b> - get <a href="https://nodejs.org">node</a>
</p>

```
name = My plugin
lang = javascript
run = node main.js
```

```js
const rl = require("readline").createInterface({ input: process.stdin });

rl.on("line", (line) => {
  const w = line.trim().split(/ +/);
  if (w[0] === "hello") console.log("log hi from javascript");
  if (w[0] === "best") {
    console.log(`draw arrow ${w[1].slice(0, 2)} ${w[1].slice(2, 4)} #1fe0e0`);
    console.log("show");
  }
});
```

<p align="center">
<b>🟦 TypeScript</b> - <a href="https://nodejs.org">node</a> 22.18 or newer runs .ts files by itself, no tsc or build step.<br>
Node only strips the types, so stick to plain type annotations (no <code>enum</code> or <code>namespace</code>).<br>
Some linux distros ship a node thats built without this. If <code>node main.ts</code> says "Unknown file extension .ts", get node from nodejs.org instead.
</p>

```
name = My plugin
lang = typescript
needs = node 22.18
run = node main.ts
```

```ts
import { createInterface } from "node:readline";

const rl = createInterface({ input: process.stdin });

rl.on("line", (line: string) => {
  const w: string[] = line.trim().split(/ +/);
  if (w[0] === "hello") console.log("log hi from typescript");
  if (w[0] === "best") {
    console.log(`draw arrow ${w[1].slice(0, 2)} ${w[1].slice(2, 4)} #1fe0e0`);
    console.log("show");
  }
});
```

<p align="center">
<b>⚙️ C</b> - <b>Linux</b>: gcc (<code>build-essential</code> on debian/ubuntu, <code>base-devel</code> on arch). <b>Mac</b>: <code>xcode-select --install</code>. <b>Windows</b>: gcc from <a href="https://www.msys2.org">MSYS2</a> or <a href="https://github.com/skeeto/w64devkit">w64devkit</a>, on your PATH
</p>

```
name = My plugin
lang = c
build = cc -O2 -o myplugin main.c
build.windows = gcc -O2 -o myplugin.exe main.c
run = myplugin
run.windows = myplugin.exe
```

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char ln[4096];
    while (fgets(ln, sizeof(ln), stdin)) {
        if (!strncmp(ln, "hello ", 6)) printf("log hi from c\n");
        if (!strncmp(ln, "best ", 5)) {
            printf("draw arrow %.2s %.2s #1fe0e0\n", ln + 5, ln + 7);
            printf("show\n");
        }
        fflush(stdout);
    }
    return 0;
}
```

<p align="center">
<b>➕ C++</b> - same compilers as C
</p>

```
name = My plugin
lang = cpp
build = c++ -O2 -o myplugin main.cpp
build.windows = g++ -O2 -o myplugin.exe main.cpp
run = myplugin
run.windows = myplugin.exe
```

```cpp
#include <iostream>
#include <sstream>
#include <string>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd, mv;
        in >> cmd >> mv;
        if (cmd == "hello") std::cout << "log hi from c++" << std::endl;
        if (cmd == "best") {
            std::cout << "draw arrow " << mv.substr(0, 2) << " " << mv.substr(2, 2) << " #1fe0e0" << std::endl;
            std::cout << "show" << std::endl;
        }
    }
}
```

<p align="center">
<b>🦀 Rust</b> - get it from <a href="https://rustup.rs">rustup.rs</a>. Plain <code>rustc</code> on one file, no cargo, no crates, no build.rs
</p>

```
name = My plugin
lang = rust
build = rustc -O -o myplugin main.rs
build.windows = rustc -O -o myplugin.exe main.rs
run = myplugin
run.windows = myplugin.exe
```

```rust
use std::io::{self, BufRead, Write};

fn main() {
    let mut out = io::stdout();
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let w: Vec<&str> = line.split_whitespace().collect();
        match w.first() {
            Some(&"hello") => writeln!(out, "log hi from rust").unwrap(),
            Some(&"best") => {
                writeln!(out, "draw arrow {} {} #1fe0e0", &w[1][..2], &w[1][2..4]).unwrap();
                writeln!(out, "show").unwrap();
            }
            _ => {}
        }
        out.flush().unwrap();
    }
}
```

<p align="center">
<b>🟪 C#</b> - get the .NET SDK from <a href="https://dot.net">dot.net</a>. You need a <code>.csproj</code>, the dll gets named after it. <code>RollForward</code> lets it run on a newer .NET than the one it was built for
</p>

```
name = My plugin
lang = csharp
build = dotnet build -c Release -o out
run = dotnet out/myplugin.dll
```

`myplugin.csproj`

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <RollForward>Major</RollForward>
  </PropertyGroup>
</Project>
```

`Program.cs`

```csharp
using System;

string? line;
while ((line = Console.ReadLine()) != null)
{
    var w = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
    if (w.Length == 0) continue;
    if (w[0] == "hello") Console.WriteLine("log hi from c#");
    if (w[0] == "best")
    {
        Console.WriteLine($"draw arrow {w[1][..2]} {w[1][2..4]} #1fe0e0");
        Console.WriteLine("show");
    }
}
```

<p align="center">
<b>🔨 How building works</b><br>
Compiled plugins ship source only. After the plugin gets added (and after the safety check, plus your OK if it got flagged) open-chease runs <code>build</code> once inside the plugin folder, then <code>run</code> starts what it built.<br>
It builds again whenever the source changes. Whatever the build prints goes to <code>build.log</code> in the plugin folder, and if it fails the last lines show on the plugins row.<br>
<code>build.windows</code>, <code>build.linux</code>, <code>build.mac</code> and <code>run.windows</code>, <code>run.linux</code>, <code>run.mac</code> win over plain <code>build</code> / <code>run</code> on that OS.<br> <br>
<b>Anything else</b> - <code>lang = other</code> and point <code>run</code> at it. Shell scripts, a <code>.bat</code>, <code>.cmd</code> or <code>.ps1</code> on windows, all fine (they might get flagged for running programs though).
</p>
</details>

<details>
<summary align="center"><b>🛡️ Safety 🛡️</b></summary>

<p align="center">
Plugins are programs and they run with all the same rights as you. So open-chease checks every plugins files when its added and whenever any of them change.<br> <br>
<b>What it looks for</b><br>
<b>internet</b> - talks to the internet (requests, urllib, sockets, fetch, curl, wget, links to websites...)<br>
<b>runs programs</b> - starts other programs (subprocess, os.system, exec, spawn, cmd /c, powershell...)<br>
<b>build runs code</b> - the build does more than compile (make, cargo, npm, scripts, build.rs, npm install scripts, msbuild tasks...)<br>
<b>has programs</b> - program files in the folder it cant read (.exe, .dll, .so, anything compiled)<br>
<b>looks shady</b> - hidden or scrambled code, like base64 that gets run, code it downloads and runs, discord webhooks<br>
<b>reads secrets</b> - goes after ssh keys, browser passwords or cookies, discord tokens, keychains, crypto wallets, the clipboard<br>
<b>starts with pc</b> - sets itself up to run when your computer starts (autostart, crontab, registry Run keys, LaunchAgents...)<br>
<b>reads keys</b> - looks like a keylogger (pynput, keyboard hooks, GetAsyncKeyState...)<br>
<b>deletes stuff</b> - deletes things outside its own folder (rm -rf, del /s, rmtree...)<br> <br>
Each one shows as a little chip on the plugins row, hover it to see the file and line it found it on.<br> <br>
<b>What happens then</b><br>
A clean plugin just turns on.<br>
A flagged one never starts until you say so. Turning it on shows what it found and asks, Turn on anyway or Cancel.<br>
Your OK is for those exact files. Edit any file in it or update it and it asks again (if its running it gets stopped till you say OK). Files the build makes dont count.<br> <br>
<b>Going online</b><br>
While a plugin runs open-chease checks every couple seconds if it (or anything it started) connected to something thats not your own computer.<br>
If it goes online without saying so in <code>internet = &lt;why&gt;</code>, it gets stopped right away and you get asked.<br>
If it did say why it still gets a went online chip and you get asked, but it keeps running and you see its reason. Hit Its fine and it doesnt ask again until its files change.<br> <br>
A few files dont count as changes: <code>frame.ppm</code> (screen reading writes it), and <code>build.log</code> and <code>.built</code> for plugins with a build line. Since those get skipped, a <code>run</code> line that points at one of them gets flagged as shady.<br> <br>
<b>Be careful anyway</b><br>
The check only knows patterns, so someone who really wants to hide something can. No chips just means nothing obvious showed up.<br>
<b>Only run plugins you trust</b>, or ones you read yourself.<br> <br>
<b>Making one that comes out clean</b><br>
Dont start other programs or ship built ones. Needs the internet? Say why with <code>internet = </code>.
</p>
</details>

<details>
<summary align="center"><b>📋 The Cheat Sheet 📋</b></summary>

<p align="center">
Every line in one place. <code>&lt;sq&gt;</code> is a square like <code>e4</code>, <code>&lt;pos&gt;</code> is a square or <code>x y</code>, colors are <code>#rgb</code> <code>#rrggbb</code> <code>#aarrggbb</code>.
</p>

```
open-chease -> you
hello 1 <folder name>
board <x> <y> <size> white|black
board none
team white|black
position <fen>
move <uci> [san]
best <uci> <san> cp|mate <score> <depth>
gameover checkmate win|loss
gameover stalemate draw
set <key> <number>
click <key>
tick <ms clock>
frame <w> <h> <path to frame.ppm>
frame none busy|no board|too small|bad numbers

you -> open-chease
setting bool <key> <0|1> <label> | <tip>
setting slider <key> <default> <min> <max> <label> | <tip>
setting choice <key> <default index> <a,b,c> <label> | <tip>
setting button <key> <label> | <tip>
tab <name>
menu <key> <label> | <tip>
menu <key>
draw text <pos> <color> <text>
draw big <pos> <color> <text>
draw rect <pos> <w> <h> <color>
draw box <pos> <w> <h> <color>
draw arrow <sq> <sq> <color>
draw square <sq> <color>
panel <title>
line [color] <text>
show
clear
note <text>
log <text>
tick <ms>
frame <x> <y> <w> <h>
```
</details>

<details>
<summary align="center"><b>📝 plugin.txt 📝</b></summary>

<p align="center">
Every plugin folder needs a <code>plugin.txt</code>. Its <code>key = value</code> lines, <code>#</code> lines are comments, keys it doesnt know get skipped. Only run is needed:
</p>

```
# shown in the Plugins tab, the folder name if you leave it out (39 chars)
name = Move announcer
# what starts it (see below)
run = announce.py
# under the name in the Plugins tab (159 chars)
description = writes the last move on the board and in a little box
# shows as v1.0 (23 chars)
version = 1.0
author = sev
# shown in the Plugins tab, click it to open
url = https://github.com/me/myplugin
# python, javascript, typescript, c, cpp, csharp, rust or other
lang = python
# doesnt load if this isnt met, the row says why
needs = python 3.8
# greyed out with "only for linux, mac" everywhere else, never starts
platforms = linux,windows,mac
# compiled plugins, runs once in the folder before run (see Languages)
build = cc -O2 -o myplugin main.c
build.windows = gcc -O2 -o myplugin.exe main.c
run.windows = myplugin.exe
# why it goes online, still warned about but this shows with it
internet = looks up openings on lichess
# gives the row an Update button, re-adds it from this link
update = https://github.com/me/myplugin
```

<p align="center">
<b>name</b> - the title in the Plugins tab<br>
<b>run</b> - a command line. If the first word is a file in the plugin folder it runs that, otherwise it looks on PATH, so <code>run = python3 main.py --fast</code> works too. It always starts inside the plugin folder<br>
On linux and mac a <code>.py</code> without the x bit runs with python3 and anything else without it runs with sh (zips love to lose the x bit). On windows <code>.py</code> runs with <code>py</code> (or <code>python</code>), <code>.bat</code>/<code>.cmd</code> with cmd and <code>.ps1</code> with powershell<br>
<b>run.windows</b> / <b>run.linux</b> / <b>run.mac</b> - use this instead of run on that OS<br>
<b>build</b> / <b>build.windows</b> / <b>build.linux</b> / <b>build.mac</b> - see Languages<br>
<b>description</b> / <b>version</b> / <b>author</b> / <b>url</b> - just for the Plugins tab<br>
<b>lang</b> - what it needs installed. Left out = guessed from run and your files<br>
<b>needs</b> - a minimum version: <code>python 3.8</code>, <code>node 22.18</code> or <code>open-chease 1</code> (the protocol version from hello)<br>
<b>platforms</b> - which OSes it works on, any of <code>linux</code> <code>windows</code> <code>mac</code> split by commas<br>
<b>internet</b> - why it goes online, see Safety<br>
<b>update</b> - a github link for the Update button<br> <br>
Theres no key to make a plugin start on, every plugin starts off until the user turns it on.<br> <br>
The folder name is the plugins id: letters, numbers, <code>-</code> <code>_</code> <code>.</code>, 31 chars max, cant start with a dot. Its what your settings get saved under, so changing it resets them.<br>
Plugins live in the <code>plugins</code> folder in the open-chease data folder (Gear > Plugins > <b>Open plugins folder</b> gets you there):<br>
<b>Linux</b> <code>~/.local/share/open-chease/plugins</code><br>
<b>Windows</b> <code>%LOCALAPPDATA%\open-chease\plugins</code><br>
<b>Mac</b> <code>~/Library/Application Support/open-chease/plugins</code><br>
Dropping a folder in by hand works too, it shows up next time you open the Plugins tab.
</p>
</details>

<details>
<summary align="center"><b>💬 How Talking Works 💬</b></summary>

<p align="center">
Same idea as UCI. open-chease writes lines to your stdin and reads lines from your stdout, one thing per line, words split by spaces.<br> <br>
<b>Flush after you print</b> (<code>print(..., flush=True)</code> in python, <code>fflush(stdout)</code> in C, <code>std::endl</code> in C++) or nothing shows up.<br> <br>
Lines it doesnt know get ignored, both ways. So read the first word and skip what you dont care about, newer versions might send more stuff. And a typo in a command just does nothing, no error, so check with <code>-v</code> if something doesnt show.<br>
Your stderr shows up when you run open-chease with <code>-v</code>.<br> <br>
<b>Positions</b> on the board are either a square (<code>e4</code>) or <code>x y</code> in pixels from the top left corner of the board. Squares follow the board when its flipped, <code>x y</code> doesnt (top left is top left).<br>
These are screen pixels like the <code>board</code> line uses, so one square is <code>size / 8</code> wide. Decimals and negative numbers are fine, <code>draw text 0 -20 ...</code> is just above the board.<br> <br>
<b>Colors</b> are <code>#rgb</code>, <code>#rrggbb</code> or <code>#aarrggbb</code>. <code>aa</code> is how see-through it is, <code>00</code> is invisible and <code>ff</code> is solid. <code>#80ff0000</code> is half see-through red.<br> <br>
<b>Settings</b> get saved in the open-chease settings file for you as <code>plugin.&lt;folder&gt;.&lt;key&gt; &lt;value&gt;</code>, and the on/off switch as <code>plugin.&lt;folder&gt;</code>. Values are always whole numbers.
</p>
</details>

<details>
<summary align="center"><b>📥 What You Get 📥</b></summary>

<p align="center">
<code>hello 1 announce</code> - the first thing you get, 1 is the protocol version and then your folder name. Send your settings here<br>
<code>board 220 140 576 white</code> - found the board: x y of its top left on screen, its size in px and which side is at the bottom. Comes again when it moves, resizes or flips, and right after you start if theres a board<br>
<code>board none</code> - lost the board (or theres none when you start)<br>
<code>team white</code> - which side the user plays, comes right after every <code>board</code><br>
<code>position rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2</code> - the FEN, after every move and when the board is found<br>
<code>move e7e5 e5</code> - the move that was just played, uci then SAN. Comes right after its <code>position</code>. SAN can be missing (see Quirks)<br>
<code>best g1f3 Nf3 cp 36 11</code> - engine best move: uci, SAN, <code>cp</code> (centipawns) or <code>mate</code> (moves to mate, negative is getting mated), the score for the side to move, then the depth. Comes again every time it gets deeper or changes its mind<br>
<code>gameover checkmate win</code> - <code>checkmate win</code>, <code>checkmate loss</code> or <code>stalemate draw</code>, win and loss are for the user. Comes after the <code>position</code><br>
<code>set onboard 1</code> - a setting value. You get one right after you declare a setting (with whatever was saved, or the default) and again every time the user changes it. Not for buttons<br>
<code>click test</code> - the user hit your button setting or your ••• menu item with that key<br>
<code>tick 12345</code> - only if you asked for ticks, the number is a clock in ms (only good for comparing to other ticks)<br>
<code>frame 576 576 /path/to/plugins/avgcolor/frame.ppm</code> - the screen bit you asked for is ready. Its width, height, then where the file is<br>
<code>frame none busy</code> - you didnt get one, the rest says why (see Quirks)
</p>
</details>

<details>
<summary align="center"><b>📤 What You Can Send 📤</b></summary>

<p align="center">
<b>⚙️ Settings</b><br>
They show up in Gear > Plugins under your plugin (or in your own tab), get saved for you, and you get a <code>set</code> line back for each. Label and tip are split by <code>|</code>, the tip shows when you hover it. You can send these any time, not just on hello. Sending one with a key you already used replaces it and keeps its value<br> <br>
<code>setting bool onboard 1 On the board | writes the move on the square</code> - on/off, then the default (0 or 1)<br>
<code>setting slider every 2 1 10 Every (sec) | how often it looks again</code> - default, min, max<br>
<code>setting choice style 0 Short,In_words Move as | Nf3 or knight to f3</code> - default (counting from 0), then the options split by commas with no spaces (<code>_</code> becomes a space). <code>set</code> gives you the number of the option<br>
<code>setting button test Say hi | pops a note</code> - you get <code>click test</code> when its pressed<br>
<code>tab My stuff</code> - puts your settings in their own tab instead of the Plugins one. Two plugins with the same tab name share it<br> <br>
<b>☰ Menu</b><br>
<code>menu peek Look now | reads the screen right now</code> - adds an item to the ••• menu, or replaces yours with that key. Clicking it sends you <code>click peek</code>, same as a button setting. They go away when your plugin stops<br>
<code>menu peek</code> - no label, removes it<br> <br>
<b>🎨 Drawing</b><br>
Nothing shows until you send <code>show</code>. Then it replaces everything you showed before, drawings and box too. So every time, send the whole picture and then <code>show</code><br> <br>
<code>draw text e4 #ffffff Nf3</code> - small text, inside a square or at x y<br>
<code>draw big 10 -40 #ff0 hello</code> - same but bigger<br>
<code>draw rect -34 0 26 26 #80ff0000</code> - filled box: where, width, height, color. On a square it starts at the squares top left corner<br>
<code>draw box -34 0 26 26 #fff</code> - same but just the outline<br>
<code>draw arrow g1 f3 #1fe0e0</code> - arrow from one square to another, as thick as the users arrow setting<br>
<code>draw square e4 #1fe0e0</code> - thick outline around a square<br>
<code>panel Last move</code> - a little box next to the board with this title<br>
<code>line #1fe0e0 Nf3</code> - a line in that box, the color is optional<br>
<code>show</code> - shows all of the above<br>
<code>clear</code> - wipes your drawings and box right away, no show needed<br> <br>
The box sits next to the board on the side the info box isnt on, boxes from different plugins stack under each other<br> <br>
<b>🔧 Other stuff</b><br>
<code>note hi there</code> - shows a note in the status line for a few secs<br>
<code>log something happened</code> - shows under your plugin in the Plugins tab (and in <code>-v</code>)<br>
<code>tick 1000</code> - get a <code>tick</code> every 1000 ms. 100 is the fastest (less becomes 100), 0 turns it off<br>
<code>frame 0 0 576 576</code> - read the screen: x y w h from the top left of the board. Negative works, so <code>frame 0 -60 576 50</code> is the strip above it. You get a binary PPM (P6, rgb) at <code>frame.ppm</code> in your plugin folder, it gets overwritten every time. Its in real screen pixels, so on a 2x screen its twice as big as you asked, and anything off the edge of the screen gets cut off. Read the size from the <code>frame</code> line or the file header, not from what you asked for
</p>
</details>

<details>
<summary align="center"><b>🐍 Bigger Example 🐍</b></summary>

<p align="center">
Tints the best moves target square with a see-through color you can pick, puts the score in a box, adds a menu item to hide it:
</p>

```python
#!/usr/bin/env python3
import sys

opts = {"on": 1, "color": 0}
colors = ["801fe0e0", "8040e070", "80ff5fb0"]
size = 0


def send(s):
    print(s, flush=True)


for line in sys.stdin:
    w = line.split()
    if not w:
        continue
    if w[0] == "hello":
        send("setting bool on 1 Show it | tints the square the best move goes to")
        send("setting choice color 0 Cyan,Green,Pink Color | what color the tint is")
        send("menu hide Hide the tint | turns it off till you turn it back on")
    elif w[0] == "set":
        opts[w[1]] = int(w[2])
        if not opts["on"]:
            send("clear")
    elif w[0] == "click" and w[1] == "hide":
        opts["on"] = 0
        send("clear")
    elif w[0] == "board" and w[1] != "none":
        size = float(w[3]) / 8
    elif w[0] == "best" and opts["on"] and size:
        send(f"draw rect {w[1][2:4]} {size:.0f} {size:.0f} #{colors[opts['color']]}")
        send("panel Best move")
        send(f"line {w[2]}  {w[3]} {w[4]}")
        send("show")
```

<p align="center">
With <code>run = best.py</code> in plugin.txt. The <code>hide</code> item only flips the value inside the plugin, the switch in the settings stays where it was. If you want both to match, track it yourself.<br>
Theres 2 more in <code>plugins-examples/</code>: announce (writes the last move on the board and in a box, has a few settings) and avgcolor (reads a bit of the screen and shows its average color). The <a href="https://github.com/sevvy-wevvy/open-chease/tree/plugin-template">template</a> uses every single thing on this page.
</p>
</details>

<details>
<summary align="center"><b>🚧 Limits 🚧</b></summary>

<p align="center">
So a broken plugin cant take open-chease down with it:<br> <br>
<b>12</b> plugins<br>
<b>24</b> settings per plugin<br>
<b>10</b> choices per choice setting<br>
<b>23</b> chars per choice<br>
<b>31</b> chars for setting keys, menu keys or folder names (letters, numbers, <code>-</code> <code>_</code> <code>.</code>)<br>
<b>39</b> chars for setting labels and the box title<br>
<b>159</b> chars for setting tips, <b>99</b> for menu tips<br>
<b>23</b> chars for tab names and menu labels<br>
<b>5</b> plugin tabs in total, after that the rest go in the Plugins tab<br>
<b>4</b> menu items per plugin<br>
<b>6</b> plugin items in the menu in total, first come first served<br>
<b>160</b> drawings per show, the rest get dropped<br>
<b>79</b> chars of text per <code>draw text</code> / <code>draw big</code><br>
<b>10</b> lines per box<br>
<b>89</b> chars per box line<br>
<b>100</b> ms is the fastest tick<br>
<b>1024x1024</b> biggest frame (bigger gets cut down)<br>
<b>100</b> ms between frames<br>
<b>4096</b> bytes per line, longer ones get thrown away. Bad lines get ignored<br>
Slider min has to be smaller than max, and theyre at most 100000 apart<br>
x y for drawings has to be within 4000 of the board. Rect and box sizes over 4000 get cut down<br> <br>
If you stop reading your stdin and stuff piles up (32 KB), or you quit, it turns you off and says why in the Plugins tab. Turn it off and on again to restart it.<br>
A plugin that prints a ton gets read a slice at a time, so it slows down instead of freezing open-chease.<br>
Plugins get killed when open-chease closes.
</p>
</details>

<details>
<summary align="center"><b>🐛 Quirks 🐛</b></summary>

<p align="center">
<b>draw square ignores see-through</b> - its a thick outline and the <code>aa</code> part of the color does nothing. For a tint use <code>draw rect e4 72 72 #801fe0e0</code> with your square size (<code>size / 8</code> from <code>board</code>)<br>
<b>SAN can be missing on move</b> - if it couldnt check the move was legal (like it missed one) you only get <code>move e2e4</code>. Always check if the third word is there<br>
<b>Your tab only shows with at least one setting</b> - <code>tab</code> on its own does nothing, and the tab goes away when your plugin stops<br>
<b>A log line replaces the hint</b> - the line under your plugin in the Plugins tab is your description until you send a <code>log</code>, then its your last log line until the plugin stops<br>
<b>Settings dont match a menu item</b> - clicking your menu item only sends <code>click</code>, if it changes something a setting also controls, the setting doesnt move<br>
<b>x y dont flip</b> - squares follow the board when black is at the bottom, x y doesnt<br>
<b>An arrow from a square to itself</b> draws nothing<br>
<b>frame none</b> reasons:<br>
<code>busy</code> - you asked again within 100 ms<br>
<code>no board</code> - theres no board right now, or it couldnt read the screen or write the file<br>
<code>too small</code> - w or h under 1<br>
<code>bad numbers</code> - it couldnt read the 4 numbers
</p>
</details>

<details>
<summary align="center"><b>🛠 Pull Requests 🛠</b></summary>

<p align="center">
The plugin system itself is the one part of open-chease I dont take outside pull requests for. Plugins run other peoples programs on your computer, so only I change that code.<br>
PRs that touch <code>src/plug*</code>, <code>docs/PLUGINS.md</code>, <code>plugins-examples/</code> or the plugin tests get closed automatically.<br>
Found a bug in it or want something new a plugin could use? <a href="https://github.com/sevvy-wevvy/open-chease/issues">Open an issue</a> and Ill look at it.<br> <br>
Your own plugins are yours though, put them on your own github and share the link!
</p>
</details>
