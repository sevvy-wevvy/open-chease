<h1 align="center">🛠 Contributing to open-chease 🛠</h1>

<p align="center">
Thanks for wanting to help! Small PRs that fix one thing are the easiest for me to check and merge, so keep them small and they'll get in way faster.<br>
Boards it cant find and pieces it reads wrong are the most useful things to fix.
</p>

<details>
<summary align="center"><b>📋 The short version 📋</b></summary>

<p align="center">
• One thing per PR. One fix or one feature, if it does two things it gets closed<br>
• Open your PR against the <b>dev</b> branch, not main<br>
• Test it and say how you tested it<br>
• CI has to pass<br>
• AI is fine, but say you used it, and you need to know what every line does
</p>
</details>

<details>
<summary align="center"><b>💬 Before you start 💬</b></summary>

<p align="center">
Small fixes can go straight to a PR.<br>
For a new feature or anything big, <a href="https://github.com/sevvy-wevvy/open-chease/issues">open an issue</a> first so we can talk about it. Nobody wants to spend a week on something that doesnt fit.<br> <br>
Changes to the engine lists (<code>engines*.txt</code>) or where anything gets downloaded from always need an issue first, those files decide what runs on peoples computers.
</p>
</details>

<details>
<summary align="center"><b>🌿 Branches 🌿</b></summary>

<p align="center">
Everything lands on <b>dev</b> first. Once its been around for a bit it moves to <b>beta</b>, then <b>main</b>. I do the moving, you only ever PR into dev.<br> <br>
Dont touch <code>bin/</code> in your PR. Those are the prebuilt binaries people download, they get rebuilt before shipping.<br> <br>
PRs against any branch besides dev, or that change anything in <code>bin/</code>, get closed automatically.
</p>
</details>

<details>
<summary align="center"><b>🤖 Using AI 🤖</b></summary>

<p align="center">
Using AI to help write your PR is allowed. Just:<br> <br>
• Say in the PR that AI helped<br>
• Know what your code does. If you get asked about something you should know how to answer and not need to ask an AI.<br>
• Test it yourself. Untested AI dumps get closed
</p>
</details>

<details>
<summary align="center"><b>🧪 Testing 🧪</b></summary>

<p align="center">
Build it first. On linux:
</p>

```sh
./build.sh
```

<p align="center">
<code>./build.sh win</code> cross builds the windows one with mingw. Then run what fits your change:
</p>

```sh
# engine changes, has to say ok
E=bin/open-chease-engine tests/perft.sh

# vision, needs python3 with pillow, plus librsvg and cairo
tests/fetch_pieces.sh cburnett staunty alpha maestro merida
python3 tests/run_vision.py 9 40

# board tracking, needs pip install chess
python3 tests/track_sim.py --games 10 --seed 21
```

<p align="center">
Vision and the sim cant get worse than they are now. Right now its:
</p>

```
total 40  board-geometry 40  colors/occupancy 37  full-pieces 37
moves right 1114/1124
```

<p align="center">
If your change makes those numbers better, nice, update the numbers in <code>.github/workflows/test.yml</code> in the same PR.<br> <br>
In the PR write a few lines on what you ran and on which OS. Anything you can see (the overlay, the menu, arrows, settings) needs a before and after screenshot.<br> <br>
CI runs on linux, windows and both macs when you open the PR. It has to be green before I merge.
</p>
</details>

<details>
<summary align="center"><b>✏️ Code style ✏️</b></summary>

<p align="center">
• Plain C, no new libraries<br>
• Match the code around it. Same naming, same formatting, short names are fine inside functions<br>
• Comments only where something isnt obvious. Short and casual, like the ones already in there<br>
• Linux builds have to keep working on glibc 2.34. That means no <code>atoi</code>, <code>sscanf</code>, <code>fscanf</code>, <code>strtol</code> and friends, parse numbers by hand like the rest of the code does. CI checks this with objdump<br>
• Dont reformat whole files, it makes the diff impossible to read
</p>
</details>

<details>
<summary align="center"><b>✅ PR checklist ✅</b></summary>

<p align="center">
☐ Its against dev<br>
☐ It does one thing<br>
☐ No changes in <code>bin/</code><br>
☐ I tested it and wrote how<br>
☐ Screenshots if you can see it<br>
☐ Vision and sim numbers didnt drop<br>
☐ CI is green<br>
☐ I said if AI helped, and I know what every line does
</p>
</details>

<details>
<summary align="center"><b>🛡️ Security 🛡️</b></summary>

<p align="center">
Found a security hole? Dont open a public issue or PR for it, read <a href="SECURITY.md">SECURITY.md</a> and tell me privately first.
</p>
</details>

<p align="center">
By sending a PR you agree your code goes out under the same <a href="LICENSE">GPL-3.0 license</a> as the rest of open-chease.
</p>
