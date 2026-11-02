<h1 align="center">🧩 open-chease plugin template 🧩</h1>

<p align="center">
<b>A plugin for open-chease that uses every bit of the plugin system, made to be copied.</b>
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
  The full list of messages and commands is in <a href="https://github.com/sevvy-wevvy/open-chease/blob/dev/PLUGINS.md">PLUGINS.md</a> on the main repo.<br> <br>
  Made something cool with it? Let me know!
  <a href="https://sevvy-wevvy.com/discord">Discord</a>
</p>

<details>
<summary align="center"><b>📁 Copying It 📁</b></summary>

<p align="center">
Copy the <code>template</code> folder (just that folder, not the whole repo) into the <code>plugins</code> folder in the open-chease data folder:<br> <br>
<b>Linux</b> <code>~/.local/share/open-chease/plugins</code><br>
<b>Windows</b> <code>%LOCALAPPDATA%\open-chease\plugins</code><br>
<b>Mac</b> <code>~/Library/Application Support/open-chease/plugins</code><br> <br>
Easiest way there is Gear > Plugins > <b>Open plugins folder</b>, it makes the folder if its not there yet.<br>
Then restart open-chease and turn it on in the Plugins tab. Its settings show up in a new <b>Template</b> tab.<br> <br>
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
In <code>plugin.txt</code> change <b>name</b>, <b>description</b> and <b>version</b> to yours.<br>
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
<b>Draws</b> with every kind: <code>square</code>, <code>arrow</code>, <code>text</code> on a square and at x y, <code>big</code>, <code>rect</code> on a square and at x y, <code>box</code><br>
<b>Box</b> next to the board with <code>panel</code> and <code>line</code>, with and without a color<br>
<b>Other</b>: <code>show</code>, <code>clear</code>, <code>note</code>, <code>log</code>, <code>tick</code>, <code>frame</code> and reading the ppm it gives back
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
<b>Read the frame</b> right away, the next one overwrites the same file
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
