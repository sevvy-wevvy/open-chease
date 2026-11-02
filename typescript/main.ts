// open-chease plugin example in typescript
//
// how it works: open-chease starts this program and talks to it like a uci engine.
// it writes lines to our stdin, we write lines to stdout. one thing per line,
// words split by spaces. full list of lines is in docs/PLUGINS.md on the main repo.
//
// what it does: draws the best move as an arrow, has a bool and a choice setting,
// a "Say hi" item in the ... menu and a little box next to the board.
//
// no tsc, no npm. node 22.18 and newer run .ts files by just cutting the types out.
// that means only types that can be erased: no enum, no namespace, no
// "constructor(private x)" stuff. type, interface and annotations are all fine.
// it doesnt type check either, run "npx tsc --noEmit --strict main.ts" for that if you want.
// heads up: the node from ubuntu and debian apt is built without this and says
// "Unknown file extension .ts". get node from nodejs.org (or nvm) instead,
// node -p process.features.typescript has to say true

import * as readline from "node:readline";

// colors are #rgb, #rrggbb or #aarrggbb. aa is how see-through, 00 invisible, ff solid
const COLORS = ["1fe0e0", "40e070", "ff5fb0", "ffd23f"];

type Words = string[];

interface Best {
  uci: string;
  san: string;
  kind: "cp" | "mate" | string;
  score: number; // cp is hundredths of a pawn, mate is moves till mate
  depth: number;
}

const opts: Record<string, number> = { arrow: 1, color: 0 }; // settings, filled by set lines
let haveBoard = false;
let team = "white";
let toMove = "w"; // from the fen, the best score is for whoever moves
let last = "";
let best: Best | null = null;

// everything goes through here, one write per batch.
// node has no flush to call: a write to a pipe goes out on its own (right away
// on linux and mac, a moment later on windows). debug prints go to console.error,
// stderr only shows when open-chease runs with -v
function send(...lines: string[]): void {
  process.stdout.write(lines.join("\n") + "\n");
}

function scoreText(b: Best): string {
  // score is for the side to move, flip it so + always means good for white
  const s = toMove === "b" ? -b.score : b.score;
  if (b.kind === "mate") return (s > 0 ? "+M" : "-M") + Math.abs(s);
  return (s >= 0 ? "+" : "") + (s / 100).toFixed(2);
}

function redraw(): void {
  if (!haveBoard) return;
  const c = COLORS[opts.color] ?? COLORS[0];
  const out: string[] = [];

  // draw lines pile up and nothing changes on screen till "show",
  // then it swaps in all of it at once. so build the whole picture every time
  if (best && opts.arrow) {
    // squares are names like e2, they follow the board when black is at the bottom.
    // uci is from+to (e2e4), a promotion has a 5th letter (e7e8q) we dont need
    const from = best.uci.slice(0, 2), to = best.uci.slice(2, 4);
    out.push(`draw arrow ${from} ${to} #c0${c}`);
    out.push(`draw text ${to} #ffffff ${best.san}`);
  }

  // the box next to the board. panel starts it, each line is a row, max 10 rows.
  // a line can start with a color, without one it gets the normal text color
  out.push("panel TypeScript example");
  out.push(`line you play ${team}`);
  if (last) out.push(`line last move ${last}`);
  if (best) out.push(`line #${c} best ${best.san}  ${scoreText(best)}  d${best.depth}`);
  out.push("show");
  send(...out);
}

// one function per message, w is the line split into words, w[0] the message name
const handlers: Record<string, (w: Words) => void> = {
  hello() {
    // "hello 1 myplugin": protocol version, then our folder name.
    // settings: open-chease draws them in Gear > Plugins, saves them and sends
    // "set <key> <value>" right after each one with the saved value, and again on every change.
    //   setting bool <key> <default 0/1> <label> | <tip>
    //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
    // menu <key> <label> | <tip>: an item in the ... menu, label 23 chars max, 4 per plugin.
    // clicking it sends "click <key>". same key again changes it, "menu hi" alone removes it.
    // older open-chease versions dont know menu and just skip the line
    send(
      "setting bool arrow 1 Best move arrow | draws an arrow for the engines move",
      "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color",
      "menu hi Say hi | pops a note so you know its alive",
      "log running",
    );
  },
  set(w) {
    // "set color 2", values are always whole numbers
    if (w.length < 3) return;
    opts[w[1]] = parseInt(w[2], 10) || 0;
    redraw();
  },
  click(w) {
    // "click hi", from the menu item (or a setting button if we had one)
    if (w[1] === "hi") send("note hi from the TypeScript example");
  },
  board(w) {
    // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
    // "board none" = lost it. clear works right away, no show needed
    haveBoard = w[1] !== "none";
    if (!haveBoard) send("clear");
    redraw();
  },
  team(w) {
    team = w[1] ?? "white";
    redraw();
  },
  position(w) {
    // "position <fen>" after every move. the old best move is for the old position
    toMove = w[2] ?? "w";
    best = null;
    redraw();
  },
  move(w) {
    // "move e7e5 e5" = uci then san. san can be missing
    last = w[2] ?? w[1] ?? "";
    redraw();
  },
  best(w) {
    // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
    // comes again each time the engine gets deeper, so keep this quick
    if (w.length < 6 || w[1].length < 4) return;
    best = { uci: w[1], san: w[2], kind: w[3], score: parseInt(w[4], 10) || 0, depth: parseInt(w[5], 10) || 0 };
    redraw();
  },
};

// readline hands us one line at a time with the \n (and windows \r\n) cut off.
// when stdin closes (open-chease quit or turned us off) node runs out of things to do and exits
const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line: string) => {
  const w = line.trim().split(/\s+/);
  // anything we dont know gets skipped, newer open-chease versions might send more
  if (!Object.hasOwn(handlers, w[0])) return;
  try {
    handlers[w[0]](w);
  } catch (e) {
    // if we crash open-chease turns us off, so log it and keep going
    send(`log oops: ${e instanceof Error ? e.message : String(e)}`);
    console.error(e);
  }
});
