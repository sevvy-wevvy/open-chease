<h1 align="center">♟️ open-chease ♟️</h1>

<p align="center">
<b>A chess overlay for Linux that finds boards on your screen and shows you the best move.</b>
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
  open-chease watches your screen for a chess board, outlines every square in green and draws an arrow on the best move for your team.<br>
  It updates twice a second and works on Hyprland, GNOME and XFCE (and sway or most other X11 desktops too).<br>
  I mainly made this to mess with my friends, so please dont use it in rated games. chess.com and lichess will ban you for it.<br> <br>
  Do you like this project? If so, please let me know!
  <a href="https://sevvy-wevvy.com/discord">Discord</a>
</p>

<details>
<summary align="center"><b>✅ Easy Install ✅</b></summary>

<p align="center">
The easiest way is one command. It downloads everything into <code>/tmp/open-chease-(your user id)</code> so if you run it again it wont download it all again.
</p>

```sh
bash <(curl -fsSL https://raw.githubusercontent.com/sevvy-wevvy/open-chease/main/get.sh)
```

<p align="center">
No curl? wget works too:
</p>

```sh
bash <(wget -qO- https://raw.githubusercontent.com/sevvy-wevvy/open-chease/main/get.sh)
```

<p align="center">
The first run also grabs Stockfish (about 80 MB) so the moves are way stronger. Add <code>--no-stockfish</code> if you dont want it, or <code>--update</code> to get the newest open-chease.
</p>
</details>

<details>
<summary align="center"><b>📦 Run From A Clone 📦</b></summary>

<p align="center">
If you clone it, nothing gets downloaded at all. It just uses the prebuilt binary in <code>bin/</code>.
</p>

```sh
git clone https://github.com/sevvy-wevvy/open-chease
cd open-chease
./run.sh
```

<p align="center">
If the prebuilt one doesnt run on your distro (glibc older than 2.34) run.sh rebuilds it for you. All it needs is gcc, every header is already in the repo. Its x86_64 Linux only tho, no Windows or Mac sorry.
</p>
</details>

<details>
<summary align="center"><b>⌨ How To Use ⌨</b></summary>

<p align="center">
Open a game on lichess, chess.com or pretty much anywhere with a flat colored board and it should find it within a second.<br> <br>
<b>White / Black</b> - Pick your team when it asks<br>
<b>X on the popup</b> - Its not a board, ignore it (stays ignored while its on screen)<br>
<b>Gold arrow</b> - Your best move, shows on your turn<br>
<b>Top box</b> - The move written out plus the score (+0.4 means youre a little ahead)<br>
<b>X on the top box</b> - Stop following that board<br>
<b>Gear on the top box</b> - Settings, they save so they stick next time (theres a whole section on them below)<br>
<b>... on the top box</b> - Opens the menu:<br>
&nbsp;&nbsp;<b>Helper mode</b> - Flips between cheats and helper mode in one click (more on helper mode below)<br>
&nbsp;&nbsp;<b>Show what it sees</b> - Puts a little tag on every square with the piece it thinks is there (white tag = white piece, black tag = black piece)<br>
&nbsp;&nbsp;<b>Fix pieces</b> - Click a square, then pick what's really there (or Empty, or whose turn it is). Your fixes stick until that piece moves. Click Done fixing in the menu when youre done, the board isnt clickable for the game while this is on<br>
&nbsp;&nbsp;<b>Its black's/white's turn</b> - If it got whose turn it is wrong<br>
&nbsp;&nbsp;<b>Engines</b> - Opens the engine library<br>
&nbsp;&nbsp;<b>Rescan board</b> - Reads the whole board again if it got lost (moves went by too fast or something)<br>
&nbsp;&nbsp;<b>Switch team</b> - If you picked the wrong team<br>
&nbsp;&nbsp;<b>Restart</b> - Starts it fresh<br>
&nbsp;&nbsp;<b>Update</b> - Grabs the newest version and restarts<br>
&nbsp;&nbsp;<b>Quit</b> - Asks first, then closes it<br>
<b>Ctrl + C</b> - Quit (in the terminal you started it from)
</p>
</details>

<details>
<summary align="center"><b>🤝 Helper Mode 🤝</b></summary>

<p align="center">
Dont want it playing for you? Hit <b>Helper mode</b> in the ... menu (or Gear > Fun > Mode). It stops showing moves completely (no arrows, no move names, nothing) and just keeps you company:<br> <br>
<b>Whose turn it is</b> - and a heads up when you or they are in check (the king gets a red ring too)<br>
<b>Win %</b> - your chances right now, from the engine, plus how much the last move changed it like (+6)<br>
<b>Danger alerts</b> - "your queen is in danger" when something of yours is attacked by a cheaper piece or not defended, and "they have mate in 3" when its coming<br>
<b>Move list</b> - the last few moves written out<br>
<b>Session score</b> - your wins and losses since you started it, like W2 L1<br>
<b>Material</b> - up 2, down 3, even<br>
<b>Taken pieces</b> - what you took and what you lost, like "took Q 2P, lost R"<br>
<b>Opening & phase</b> - knows about 70 openings (yes the Bongcloud is in there). Once youre out of the opening it says middlegame or endgame instead<br>
<b>Turn timer</b> - how long the current turn has been going<br>
<b>Win graph</b> - a little line under the box of how the game went, green when youre winning, red when youre not<br>
<b>Game report</b> - when the game ends you get an accuracy % for both of you (same formula lichess uses) and how many bad moves each side made<br>
<b>Last move</b> - outlines the squares of the last move in yellow, handy when the site doesnt<br>
<b>Hanging pieces</b> - orange box around your pieces that are attacked and not defended. Off by default since its kinda a hint<br>
<b>Rate my moves</b> - still works, so you still get a ?? and a roast when you blunder<br> <br>
All of it is under Gear > Helper and you can turn each one off. The win graph, check ring, last move and hanging pieces show in normal mode too, and material and the opening go in the status line there.
</p>
</details>

<details>
<summary align="center"><b>🎛 Settings 🎛</b></summary>

<p align="center">
Settings are split into tabs: Look, Your move, Their move, Engine, Fun, Helper and Themes. Every tab has a <b>Reset this tab</b> button if you mess something up. Menus and tabs slide in instead of just popping up.<br> <br>
<b>Look</b><br>
14 themes, where the box sits (top, bottom, or stacked on the left or right), box size and how see-through it is<br>
The grid can be lines, just the outline, corners or off, in any color<br>
Score as pawns (+0.4), centipawns (+40) or win %, plus an eval bar next to the board if you want one<br>
Square names on every square and "Show what it sees" on from the start<br> <br>
<b>Your move</b><br>
Arrows can be straight, bent for knights, curved, a swoosh, dashed, dotted, glowing or double<br>
The piece to move and where it goes each get their own marker, theres 13 to pick from<br>
You can show just the piece, just the square, or wait a few seconds before it shows anything so you get to think first<br>
Rainbow is one of the colors. Its dumb and i love it<br> <br>
<b>Their move</b><br>
Same options as yours, shown as text, on the board or both<br> <br>
<b>Engine</b><br>
<b>Engine</b> picks which one it uses and remembers it. <b>Get engines</b> opens the library with 44 engines (Reckless, Pawnocchio and a bunch more besides Stockfish) and how strong and how big each one is. You can type to search in it, Esc closes it<br>
Think time, updates a second, top 2 or 3 moves, threads, memory, and whether it keeps thinking on their turn<br>
The list lives in <code>engines.txt</code> here so new ones show up without updating. Engines that come in a zip or tar keep their whole folder, so ones that need a network file next to them (like Arasan) work too. Any UCI engine you drop in <code>/tmp/open-chease-(your user id)/engines</code> shows up too<br> <br>
<b>Fun</b><br>
Mode switches between Cheats and Helper (see Helper Mode above)<br>
The move picker can give you the best move, a human looking one, one thats close enough, a random top 3, a coin flip, a totally random move, the worst move or only blunders. <br>
A strength cap (Full down to 1400), trash talk, grades for your moves and theirs with a roast attached, and confetti when you win<br> <br>
<b>Helper</b><br>
Material, taken pieces, opening & phase, win graph, turn timer, game report, last move, check warning, hanging pieces, move list, danger alerts and session score. Each one is its own switch<br> <br>
<b>Themes</b><br>
Every theme as a little preview card, click one to use it. Under that you can make up to 3 of your own: pick a slot and a part (box, text, buttons, grid, menus...), then click a color. It changes live so you see it right away. <b>Copy current theme</b> starts you off from whatever theme youre on. Your themes save with the rest of the settings
</p>
</details>

<details>
<summary align="center"><b>⚙️ Options ⚙️</b></summary>

<p align="center">
These work with both run.sh and get.sh.<br> <br>
<b>--team white</b> or <b>--team black</b> - Skips the question and always plays that side<br>
<b>--builtin</b> - Uses my engine even if Stockfish is there<br>
<b>--engine PATH</b> - Use any other UCI engine<br>
<b>--backend wayland / gnome / x11</b> - Force how it grabs the screen if it picks wrong<br>
<b>-v</b> or <b>-vv</b> - Prints what its seeing
</p>
</details>

<details>
<summary align="center"><b>📝 Good To Know 📝</b></summary>

<p align="center">
Start it before the game starts if you can. It learns your piece set from the starting position and in my tests it got 99.9% of moves right that way. Jumping in mid game works too but it has to guess the pieces from shapes it already knows, so with a weird piece set its more like 80%. If it gets something wrong use Fix pieces.<br> <br>
Flat board themes work best (lichess brown/blue/green, chess.com green, most others). Wood, marble and weird colored boards work too, just less reliably: in my tests it finds almost all of them but reads every piece right on about 7 out of 10. Recolored piece sets (pink, gold, whatever) are hit or miss, mostly when the pieces are close to the square color. If it reads one wrong use Fix pieces.<br> <br>
If part of the board is covered or scrolled off screen it keeps following the moves it can see, but it wont re-read the whole board until you can see all of it again. Same with the settings or ... menu, if they sit on the board it pauses and catches up when you close them.<br> <br>
GNOME shows the screen sharing icon while its running, thats normal. On XFCE the compositor has to be on (its on by default, it'll tell you if it isnt).
</p>
</details>

<details>
<summary align="center"><b>🩹 If Something Breaks 🩹</b></summary>

<p align="center">
<b>It never finds the board</b> - Make sure the whole board is on screen and its a flat theme. Run it with <code>-v</code>, it prints how close it got ("best N/64")<br>
<b>Every square says ?</b> - Try Rescan board in the ... menu. If it keeps happening send me the <code>-v</code> output<br>
<b>Wrong piece somewhere</b> - ... > Fix pieces, click the square and pick the right one. It sticks until that piece moves<br>
<b>Lots of pieces wrong after jumping in mid game</b> - It reads pieces with the shapes it learned last time, and if you switched sites those can be off. It tries its built in shapes too when those fit better, but starting it before a game starts is still the best fix since then it learns the new set<br>
<b>Thinks its the wrong turn</b> - ... > Its black's/white's turn<br>
<b>It lost track after a fast few moves</b> - ... > Rescan board<br>
<b>The engine died</b> - It restarts by itself. If one keeps dying pick another in Gear > Engine<br>
<b>Two monitors</b> - Works, it watches all of them. If a monitor gets plugged in after you start it, restart it from the ... menu<br>
<b>Nothing shows up on Hyprland/sway</b> - Run with <code>-v</code> and check it says what screen formats it got. If it says it cant read them please open an issue with that line<br> <br>
Still broken? Open an <a href="https://github.com/sevvy-wevvy/open-chease/issues">issue</a> or yell at me on <a href="https://sevvy-wevvy.com/discord">Discord</a>, the <code>-v</code> output helps a ton.
</p>
</details>

<details>
<summary align="center"><b>🧠 How It Works 🧠</b></summary>

<p align="center">
<b>Grabbing the screen</b> - wlr-screencopy on Hyprland/sway, Mutters ScreenCast on GNOME, and XGetImage on X11 stuff like XFCE<br>
<b>The overlay</b> - a click through layer-shell surface on Hyprland, and a click through override-redirect window on GNOME and X11 since those dont have layer-shell. It only sends the parts of the screen that changed<br>
<b>Finding boards</b> - looks for rows of alternating square colors, tries every 8x8 grid that fits, then snaps it to the real square edges. If that finds nothing it tries again on a blurred copy looking for square edges instead of exact colors (thats how wood and marble boards get found)<br>
<b>Following the game</b> - checks which squares have a white piece, a black piece or nothing, and matches that against every legal move (and pairs of moves) to figure out what was played<br>
<b>The engine</b> - its my own, in src/engine.c, does about 2 million positions a second. bin/open-chease-engine is the same thing as a normal UCI engine if you want to use it somewhere else. If Stockfish is around it uses that instead
</p>
</details>

<details>
<summary align="center"><b>🔨 Building & Tests 🔨</b></summary>

<p align="center">
<code>./build.sh</code> builds bin/open-chease and bin/open-chease-engine<br>
<code>tests/perft.sh</code> checks the move generator against known numbers<br>
<code>tests/fetch_pieces.sh cburnett merida alpha staunty maestro</code> grabs piece sets for the vision test<br>
<code>python3 tests/run_vision.py 1 60</code> runs 60 random boards through the vision code. Add <code>TEXTURES=wood,marble,gradient,noise</code>, <code>THEMES=purple,night,wood,red</code> or <code>TINTS="wFFB0B0,b3050C0"</code> to throw custom looking boards and recolored pieces at it<br>
<code>python3 tests/track_sim.py --games 60</code> plays 60 whole fake games through the tracker with all the annoying stuff sites do (dragging, move dots, promotion popups, check glow, missed moves) and tells you how many moves it got right. Needs <code>pip install chess</code><br> <br>
tests/page/index.html is a board that plays a game by itself (add <code>?flip=1</code>, <code>?fen=...</code> or <code>?ms=2500</code>). Its really handy for watching the overlay. Serve it with <code>python3 -m http.server</code> from tests/page.
</p>
</details>

<details>
<summary align="center"><b>🛠 Pull Requests 🛠</b></summary>

<p align="center">
Want to help with this? <a href="https://github.com/sevvy-wevvy/open-chease/pulls">Make A pull request!</a><br>
Pull request allow for anyone to request a change to this code. In turn meaning that <b>YOU</b> can contribute to this project.<br>
New desktops, board themes it cant find yet or piece sets it reads wrong are all super welcome. Run <code>tests/perft.sh</code> and the vision test before you send it.<br> <br>
As long as it works, and doesnt vary off too far from the main idea its very likely it get accepted!
</p>
</details>

<details>
<summary align="center"><b>💖 Credits 💖</b></summary>

<p align="center">
The built in piece shapes come from the lichess piece sets (cburnett, merida, leipzig, tatiana, companion, chessnut, kosal, cardinal, governor, fresca and riohacha), squished down to tiny 24x24 outlines.<br>
The overlay font is DejaVu Sans Bold.<br>
The Wayland and X11 headers in third_party/ are MIT licensed.
</p>
</details>

<details>
<summary align="center"><b>⚖️ License ⚖️</b></summary>

<p align="center">
  <small>
    <b>open-chease</b> is released under the
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

<details>
<summary align="center"><b>🩷 Donate 🩷</b></summary>

<p align="center">
I accept donations over few platforms, below are where you can help support my projects and more.<br>
<b>Ko-fi</b> (Preferred) - https://sevvy-wevvy.com/kofi<br>
<b>Patreon</b> - https://sevvy-wevvy.com/patreon<br>
<b>Throne</b> - https://sevvy-wevvy.com/throne
</p>
</details>
