<h1 align="center">🧩 open-chease plugin template 🧩</h1>

<p align="center">
<b>Plugins for open-chease made to be copied: one that uses every bit of the plugin system, and a small one in 7 languages.</b>
</p>

<p align="center">
  <a href="https://sevvy-wevvy.com/discord">
    <img src="https://img.shields.io/discord/1398944180079689728?label=Discord&style=for-the-badge" alt="Discord">
  </a>
  <a href="https://sevvy-wevvy.com/donate" target="_blank" style="margin-left:10px;">
    <img src="https://img.shields.io/badge/Support%20-Me-5B5BFF?style=for-the-badge&logo=gift&logoColor=white" alt="Support Me" width="100">
  </a>
</p>

<p align="center">
  A starting point for making your own plugin for <a href="https://github.com/sevvy-wevvy/open-chease">open-chease</a>.<br>
  It draws the best move with an arrow, writes the score under the board, tints the last move, reads the screen to show the boards average color, has its own settings tab with every kind of setting, and puts a little box next to the board with whats going on.<br>
  None of that is very useful on its own, its there so you can see how each part works and rip out what you dont need. Theres notes all through <code>template.py</code> saying why things are the way they are.<br> <br>
  Dont want python? Theres the same small plugin in <b>C</b>, <b>C++</b>, <b>C#</b>, <b>Rust</b>, <b>JavaScript</b> and <b>TypeScript</b> too, each in its own folder. They all do the exact same thing (best move arrow, a bool and a choice setting, a menu item, a box next to the board) so you can put them side by side. No packages or downloads in any of them, just the language itself.<br> <br>
  The full list of messages and commands is in <a href="https://github.com/sevvy-wevvy/open-chease/blob/dev/docs/PLUGINS.md">docs/PLUGINS.md</a> on the main repo.<br> <br>
  Made something cool with it? Let me know!
  <a href="https://sevvy-wevvy.com/discord">Discord</a>
</p>

<details>
<summary align="center"><b>📁 Copying It 📁</b></summary>

<p align="center">
Copy the <code>template</code> folder (or one of the language folders, just that folder, not the whole repo) into the <code>plugins</code> folder in the open-chease data folder:<br> <br>
<b>Linux</b> <code>~/.local/share/open-chease/plugins</code><br>
<b>Windows</b> <code>%LOCALAPPDATA%\open-chease\plugins</code><br>
<b>Mac</b> <code>~/Library/Application Support/open-chease/plugins</code><br> <br>
Easiest way there is Gear > Plugins > <b>Open plugins folder</b>, it makes the folder if its not there yet.<br>
Then restart open-chease and turn it on in the Plugins tab, plugins always start off. Its settings show up in a new <b>Template</b> tab (the language examples put theirs in the Plugins tab).<br> <br>
You need python 3 installed, nothing else. On windows it runs with <code>py</code> (or <code>python</code>), so get it from python.org and tick the launcher box. On mac <code>python3</code> comes with the Xcode command line tools.
</p>

<p align="center">
On linux this does it in one go:
</p>

```sh
git clone -b plugin-template https://github.com/sevvy-wevvy/open-chease.git oc-template
mkdir -p ~/.local/share/open-chease/plugins
cp -r oc-template/template ~/.local/share/open-chease/plugins/myplugin
```
</details>

<details>
<summary align="center"><b>✏️ Renaming It ✏️</b></summary>

<p align="center">
The <b>folder name</b> is your plugins id. Its what gets saved in the settings file, so pick it once and dont change it later or your settings reset. Letters, numbers, <code>-</code> <code>_</code> <code>.</code> only, 31 chars max.<br> <br>
In <code>plugin.txt</code> change <b>name</b>, <b>description</b>, <b>version</b>, <b>author</b> and <b>url</b> to yours. If its on github set <b>update</b> to your own repo so people get an Update button.<br>
If you rename <code>template.py</code>, change <b>run</b> to match.<br>
In <code>template.py</code> change <code>TAB</code> to your own tab name, or delete the tab line so your settings go in the Plugins tab.
</p>
</details>

<details>
<summary align="center"><b>🔧 What To Change First 🔧</b></summary>

<p align="center">
<b>SETTINGS</b> at the top - delete the ones you dont want, add yours. Each one needs a key, a label and a tip<br>
<b>the on_ functions</b> - one per message open-chease sends. Delete the ones you dont care about from <code>HANDLERS</code> and the function<br>
<b>redraw()</b> - builds everything you see on the board every time, swap it for your own drawing<br>
<b>look()</b> and the ppm stuff - only needed if you read the screen, delete it and the <code>every</code>, <code>peek</code> settings if you dont<br> <br>
Run open-chease with <code>-v</code> while you work on it. Your <code>log</code> lines and anything you print to stderr show up there, and the Plugins tab says why if your plugin quit.
</p>
</details>

<details>
<summary align="center"><b>🗺️ Whats In It 🗺️</b></summary>

<p align="center">
<b>Gets</b> every message: <code>hello</code>, <code>set</code>, <code>click</code>, <code>board</code>, <code>team</code>, <code>position</code>, <code>move</code>, <code>best</code>, <code>gameover</code>, <code>tick</code>, <code>frame</code><br>
<b>Settings</b> of every kind: bool, choice, slider, button, plus its own <code>tab</code><br>
<b>Menu</b> items in the ••• menu with <code>menu</code>, one that changes its own label when you click it<br>
<b>Draws</b> with every kind: <code>square</code>, <code>arrow</code>, <code>text</code> on a square and at x y, <code>big</code>, <code>rect</code> on a square and at x y, <code>box</code><br>
<b>Box</b> next to the board with <code>panel</code> and <code>line</code>, with and without a color<br>
<b>Other</b>: <code>show</code>, <code>clear</code>, <code>note</code>, <code>log</code>, <code>tick</code>, <code>frame</code> and reading the ppm it gives back<br>
<b>plugin.txt</b> with every key and a note on each: <code>lang</code>, <code>needs</code>, <code>author</code>, <code>url</code>, <code>update</code>, <code>platforms</code>, <code>internet</code>, <code>build</code>, and the per OS ones
</p>
</details>

<details>
<summary align="center"><b>⚠️ Gotchas ⚠️</b></summary>

<p align="center">
<b>Flush</b> after you print or nothing shows up. The <code>send()</code> function does it for you<br>
<b>Nothing</b> you draw shows until you send <code>show</code>, and then it replaces everything from before, so build the whole picture every time<br>
<b>Bad lines</b> just get skipped, theres no error. If something doesnt show up check the words first<br>
<b>Unknown messages</b> should be ignored, newer versions of open-chease might send more<br>
<b>Dont crash</b>, open-chease turns a plugin off when it quits. The main loop catches errors and logs them instead<br>
<b>x y</b> is in px from the top left of the board and doesnt flip when black is at the bottom, square names do<br>
<b>draw square</b> is a thick outline and ignores see-through, use <code>draw rect</code> on a square for a tint<br>
<b>Read the frame</b> right away, the next one overwrites the same file<br>
<b>Menu</b> labels are 23 chars max, 4 items per plugin and 6 from all plugins together, so dont add ones nobody needs<br>
<b>Older</b> open-chease versions dont know <code>menu</code> or the new plugin.txt keys and just skip them. Thats fine for python and node, but a compiled one needs <code>build</code>, so on an old version build it yourself once
</p>
</details>

<details>
<summary align="center"><b>🌍 Picking A Language 🌍</b></summary>

<p align="center">
<b>New to this?</b> Start with <code>template</code> (python). Nothing to build, you change a line, turn the plugin off and on, done. Its also the only one that shows every feature.<br>
<b>Know another language better?</b> Grab that folder. Theyre short on purpose, about 150 lines, with notes on every part.<br> <br>
They all speak the same thing: open-chease writes lines to your stdin, you print lines to stdout, words split by spaces. Thats it, no library, no sdk. So any language that can read and print lines works, these are just the ones with an example.<br> <br>
<b>Compiled ones</b> (C, C++, Rust, C#) ship just the source. open-chease runs the <code>build</code> line from plugin.txt once inside the folder and again when the source changes, what it prints goes in <code>build.log</code> there. Then it starts <code>run</code>. If it fails the Plugins tab shows the last lines of the log.<br>
If the language isnt installed the plugin doesnt load, its row says whats missing with a <b>Get</b> link and a <b>Check again</b> button.
</p>
</details>

<details>
<summary align="center"><b>🐍 Python 🐍</b></summary>

<p align="center">
<b>Folder</b> <code>template</code>, the big one with everything in it<br>
<b>Install</b> python 3.7 or newer from <a href="https://www.python.org/downloads/">python.org</a>. Windows: tick the <b>py launcher</b> box. Mac: <code>xcode-select --install</code> gives you <code>python3</code>. Linux: you already have it<br>
<b>Build</b> none, <code>run = template.py</code> and open-chease picks python3 or py for you<br>
<b>Check it</b> <code>python3 -m py_compile template.py</code> catches typos before you restart anything
</p>
</details>

<details>
<summary align="center"><b>⚙️ C ⚙️</b></summary>

<p align="center">
<b>Folder</b> <code>c</code>, one file <code>main.c</code>, only the C standard library<br>
<b>Install</b> Linux: <code>sudo apt install gcc</code> (or your distros gcc). Mac: <code>xcode-select --install</code>. Windows: gcc from <a href="https://winlibs.com/">winlibs.com</a> or <a href="https://www.msys2.org/">msys2</a>, and put its <code>bin</code> folder on PATH<br>
<b>Build</b> <code>cc -O2 -o myplugin main.c</code> turns main.c into a program called myplugin, <code>-O2</code> makes it fast. Windows has its own line, <code>gcc ... -o myplugin.exe</code>, and <code>run.windows = myplugin.exe</code><br>
<b>Watch out</b> stdout into a pipe is block buffered, <code>fflush(stdout)</code> after every batch or nothing shows up
</p>
</details>

<details>
<summary align="center"><b>➕ C++ ➕</b></summary>

<p align="center">
<b>Folder</b> <code>cpp</code>, one file <code>main.cpp</code>, C++17 and the standard library<br>
<b>Install</b> same as C, g++ comes with gcc (<code>sudo apt install g++</code> on linux, winlibs or msys2 on windows, Xcode tools on mac)<br>
<b>Build</b> <code>c++ -O2 -std=c++17 -o myplugin main.cpp</code>. Windows uses <code>g++ ... -static -o myplugin.exe</code> so it doesnt need the mingw dlls next to it<br>
<b>Watch out</b> use <code>'\n'</code> and one <code>std::cout.flush()</code> per batch, <code>std::endl</code> flushes every line which works but is slow
</p>
</details>

<details>
<summary align="center"><b>🦀 Rust 🦀</b></summary>

<p align="center">
<b>Folder</b> <code>rust</code>, one file <code>main.rs</code>, only std, no cargo and no crates<br>
<b>Install</b> <a href="https://rustup.rs/">rustup.rs</a> on any OS (it comes with cargo too, we just dont use it), or <code>sudo apt install rustc</code><br>
<b>Build</b> <code>rustc -O -o myplugin main.rs</code>, plain rustc so theres nothing to download. Windows: <code>-o myplugin.exe</code><br>
<b>Watch out</b> stdout is buffered here too, lock it, write, <code>flush()</code>. Dont <code>unwrap()</code> on stuff from open-chease, a panic quits the plugin and it gets turned off
</p>
</details>

<details>
<summary align="center"><b>#️⃣ C# #️⃣</b></summary>

<p align="center">
<b>Folder</b> <code>csharp</code>, <code>Program.cs</code> and a tiny <code>myplugin.csproj</code>, no nuget packages<br>
<b>Install</b> the <b>.NET SDK</b> 8 or newer from <a href="https://dotnet.microsoft.com/download">dotnet.microsoft.com</a> (the runtime alone cant build). Linux: <code>sudo apt install dotnet-sdk-8.0</code> or whatever version your distro has<br>
<b>Build</b> <code>dotnet build -c Release -o out</code> builds the csproj into <code>out/</code>, then <code>run = dotnet out/myplugin.dll</code>. Same line on every OS. The very first build on a machine is slow, dotnet sets itself up<br>
<b>Watch out</b> the example makes its own stdout writer with no BOM and <code>\n</code> line ends, and flushes it. Rename the csproj and the dll name changes with it, so fix <code>run</code> too
</p>
</details>

<details>
<summary align="center"><b>🟨 JavaScript 🟨</b></summary>

<p align="center">
<b>Folder</b> <code>javascript</code>, one file <code>main.js</code>, only node itself, no npm<br>
<b>Install</b> <a href="https://nodejs.org/">node</a> 18 or newer<br>
<b>Build</b> none, <code>run = node main.js</code><br>
<b>Watch out</b> theres no flush in node, <code>process.stdout.write</code> sends it on its own. Use <code>console.error</code> for debug prints, stdout is only for open-chease
</p>
</details>

<details>
<summary align="center"><b>🟦 TypeScript 🟦</b></summary>

<p align="center">
<b>Folder</b> <code>typescript</code>, one file <code>main.ts</code>, no tsc, no npm<br>
<b>Install</b> <a href="https://nodejs.org/">node</a> 22.18 or newer, it runs .ts files by itself by cutting the types out. The node from ubuntu/debian apt is built without that and says <code>Unknown file extension ".ts"</code>, so get it from nodejs.org (or nvm). <code>node -p process.features.typescript</code> has to say true<br>
<b>Build</b> none, <code>run = node main.ts</code> with <code>needs = node 22.18</code><br>
<b>Watch out</b> only types that can just be deleted work: no <code>enum</code>, no <code>namespace</code>, no <code>constructor(private x)</code>. node doesnt type check either, <code>npx tsc --noEmit --strict main.ts</code> does if you want it
</p>
</details>

<details>
<summary align="center"><b>⚖️ License ⚖️</b></summary>

<p align="center">
  <small>
    <b>open-chease</b> and this template are released under the
    <a href="https://www.gnu.org/licenses/gpl-3.0.html" target="_blank">GPL-3.0 License</a>.
    This means you're free to use, modify, and share the code. As long as you:
  </small>
</p>

<p align="center">
  <small>
    • Keep your changes <b>open-source</b> under the same license. <br>
    • <b>Give proper credit</b> to the project and original contributors. <br>
    • Include a copy of the <a href="https://www.gnu.org/licenses/gpl-3.0.html">GPL-3.0 license</a> with your distribution.
  </small>
</p>

<p align="center">
  <small>
    In short: play fair, keep it open, and give credit where it's due.
  </small>
</p>
</details>
