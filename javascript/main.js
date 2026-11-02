// open-chease plugin example in plain javascript (node)
//
// open-chease runs this and talks to it a bit like a uci engine: it writes lines to our stdin
// and we write lines back on stdout. one thing per line, words split by spaces.
// all of them are listed in docs/PLUGINS.md on the main repo
//
// draws the best move as an arrow, has a bool and a choice setting, a "Say hi" item
// in the ... menu and a little box next to the board
//
// just node, no npm and no packages. any node from the last few years works

"use strict";
const readline = require("node:readline");

// colors are #rgb, #rrggbb or #aarrggbb. aa is how see-through, 00 invisible, ff solid
const COLORS = ["1fe0e0", "40e070", "ff5fb0", "ffd23f"];

const opts = { arrow: 1, color: 0 }; // set lines fill these
let haveBoard = false;
let team = "white";
let toMove = "w"; // from the fen, the best score is for whoever moves
let last = "";
let best = null; // { uci, san, kind, score, depth }

// one write per batch. theres no flush in node, a write to a pipe goes out by itself
// (right away on linux and mac, a moment later on windows).
// console.log would work too but its easy to mix up with debug prints. put those in
// console.error, stderr shows up when open-chease runs with -v
function send(...lines) {
  process.stdout.write(lines.join("\n") + "\n");
}

function scoreText(b) {
  // engine scores for the side to move, flip it so + is always white.
  // cp is hundredths of a pawn, mate is moves till mate
  const s = toMove === "b" ? -b.score : b.score;
  if (b.kind === "mate") return (s > 0 ? "+M" : "-M") + Math.abs(s);
  return (s >= 0 ? "+" : "") + (s / 100).toFixed(2);
}

function redraw() {
  if (!haveBoard) return;
  const c = COLORS[opts.color] || COLORS[0];
  const out = [];

  // the screen only changes on "show", which replaces whatever was there before.
  // so send the whole picture each time
  if (best && opts.arrow) {
    // square names like e2 follow the board even when black is at the bottom.
    // e2e4 = from then to, a promotion adds a 5th letter (e7e8q) thats not needed here
    const from = best.uci.slice(0, 2), to = best.uci.slice(2, 4);
    out.push(`draw arrow ${from} ${to} #c0${c}`);
    out.push(`draw text ${to} #ffffff ${best.san}`);
  }

  // box next to the board. panel starts it, then max 10 line rows, color at the start is optional
  out.push("panel JavaScript example");
  out.push(`line you play ${team}`);
  if (last) out.push(`line last move ${last}`);
  if (best) out.push(`line #${c} best ${best.san}  ${scoreText(best)}  d${best.depth}`);
  out.push("show");
  send(...out);
}

// w is the line split into words, w[0] picks the handler
const handlers = {
  hello(w) {
    // "hello 1 myplugin", the protocol version and our folder name
    //
    // settings show up in Gear > Plugins and open-chease saves them for us. it sends
    // "set <key> <value>" with the saved value right after each one, then on every change
    //   setting bool <key> <default 0/1> <label> | <tip>
    //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
    //
    // menu <key> <label> | <tip> is an item in the ... menu. label 23 chars max, 4 per plugin.
    // a click sends "click <key>", the same key again changes it and "menu hi" alone removes it.
    // older versions of open-chease dont know menu, they skip it
    send(
      "setting bool arrow 1 Best move arrow | draws an arrow for the engines move",
      "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color",
      "menu hi Say hi | pops a note so you know its alive",
      "log running",
    );
  },
  set(w) {
    // "set color 2", always a whole number
    if (w.length < 3) return;
    opts[w[1]] = parseInt(w[2], 10) || 0;
    redraw();
  },
  click(w) {
    // "click hi" from the menu item. a button setting sends click too
    if (w[1] === "hi") send("note hi from the JavaScript example");
  },
  board(w) {
    // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
    // "board none" = lost it. clear doesnt need a show after it
    haveBoard = w[1] !== "none";
    if (!haveBoard) send("clear");
    redraw();
  },
  team(w) {
    team = w[1] || "white";
    redraw();
  },
  position(w) {
    // "position <fen>", comes after every move. old best is for the old position
    toMove = w[2] || "w";
    best = null;
    redraw();
  },
  move(w) {
    // "move e7e5 e5", san is the second one and can be missing
    last = w[2] || w[1] || "";
    redraw();
  },
  best(w) {
    // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
    // gets sent again every time the engine goes deeper, dont do anything slow in here
    if (w.length < 6 || w[1].length < 4) return;
    best = { uci: w[1], san: w[2], kind: w[3], score: parseInt(w[4], 10) || 0, depth: parseInt(w[5], 10) || 0 };
    redraw();
  },
};

// readline gives one line at a time, \n and windows \r\n already cut off.
// once stdin closes (open-chease quit or turned us off) node has nothing left to do and exits
const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line) => {
  const w = line.trim().split(/\s+/);
  // skip what we dont know, newer open-chease might send more.
  // hasOwn and not handlers[w[0]] so a line like "toString" cant call something weird
  if (!Object.hasOwn(handlers, w[0])) return;
  try {
    handlers[w[0]](w);
  } catch (e) {
    // a crash gets us turned off, log it instead
    send(`log oops: ${e.message}`);
    console.error(e);
  }
});
