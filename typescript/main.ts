// open-chease plugin example in typescript
//
// same idea as the javascript one: open-chease starts this, sends lines to our stdin
// like it would to a uci engine and reads what we print. one thing per line, words split
// by spaces, the whole list is in docs/PLUGINS.md on the main repo
//
// best move arrow, a bool and a choice setting, "Say hi" in the ... menu and a box next to the board
//
// no tsc and no npm. node 22.18+ runs .ts by cutting the types out, so only types that can
// just be erased work. enum, namespace and "constructor(private x)" dont, type, interface
// and annotations are fine. nothing gets type checked either,
// "npx tsc --noEmit --strict main.ts" does that if you want it
//
// heads up, node from ubuntu/debian apt is built without this and says
// "Unknown file extension .ts". get it from nodejs.org (or nvm),
// node -p process.features.typescript has to say true

import * as readline from "node:readline";

// colors are #rgb, #rrggbb or #aarrggbb. aa is how see-through, 00 invisible, ff solid
const COLORS = ["1fe0e0", "40e070", "ff5fb0", "ffd23f"];

type Words = string[];

interface Best {
  uci: string;
  san: string;
  kind: "cp" | "mate" | string;
  score: number; // cp = hundredths of a pawn, mate = moves till mate
  depth: number;
}

const opts: Record<string, number> = { arrow: 1, color: 0 }; // the settings
let haveBoard = false;
let team = "white";
let toMove = "w"; // from the fen, the best score is for whoever moves
let last = "";
let best: Best | null = null;

// no flush needed, node sends pipe writes by itself (a moment later on windows).
// debug prints go in console.error, you see them with open-chease -v
function send(...lines: string[]): void {
  process.stdout.write(lines.join("\n") + "\n");
}

function scoreText(b: Best): string {
  // flip to whites side, the engine scores for whoever moves
  const s = toMove === "b" ? -b.score : b.score;
  if (b.kind === "mate") return (s > 0 ? "+M" : "-M") + Math.abs(s);
  return (s >= 0 ? "+" : "") + (s / 100).toFixed(2);
}

function redraw(): void {
  if (!haveBoard) return;
  const c = COLORS[opts.color] ?? COLORS[0];
  const out: string[] = [];

  // "show" replaces the last picture completely, so this sends all of it every time
  if (best && opts.arrow) {
    // e2 is still e2 when black is at the bottom. promotions (e7e8q) have a 5th letter we skip
    const from = best.uci.slice(0, 2), to = best.uci.slice(2, 4);
    out.push(`draw arrow ${from} ${to} #c0${c}`);
    out.push(`draw text ${to} #ffffff ${best.san}`);
  }

  // up to 10 rows in the box, the color on a line is optional
  out.push("panel TypeScript example");
  out.push(`line you play ${team}`);
  if (last) out.push(`line last move ${last}`);
  if (best) out.push(`line #${c} best ${best.san}  ${scoreText(best)}  d${best.depth}`);
  out.push("show");
  send(...out);
}

const handlers: Record<string, (w: Words) => void> = {
  hello() {
    // "hello 1 myplugin" (protocol version, folder name)
    // settings land in Gear > Plugins. open-chease saves them and sends "set <key> <value>"
    // with the saved value right after each line, and again on every change
    //   setting bool <key> <default 0/1> <label> | <tip>
    //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
    // menu <key> <label> | <tip> goes in the ... menu, 23 char label, 4 per plugin, clicks send
    // "click <key>". resend the key to change it, "menu hi" alone removes it.
    // old open-chease versions skip menu lines
    send(
      "setting bool arrow 1 Best move arrow | draws an arrow for the engines move",
      "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color",
      "menu hi Say hi | pops a note so you know its alive",
      "log running",
    );
  },
  set(w) {
    // whole numbers only
    if (w.length < 3) return;
    opts[w[1]] = parseInt(w[2], 10) || 0;
    redraw();
  },
  click(w) {
    if (w[1] === "hi") send("note hi from the TypeScript example");
  },
  board(w) {
    // "board 220 140 576 white" = x y on screen, size in px, side at the bottom, or "board none".
    // clear works without a show
    haveBoard = w[1] !== "none";
    if (!haveBoard) send("clear");
    redraw();
  },
  team(w) {
    team = w[1] ?? "white";
    redraw();
  },
  position(w) {
    // new position after every move, the old best is stale
    toMove = w[2] ?? "w";
    best = null;
    redraw();
  },
  move(w) {
    // "move e7e5 e5", san might be missing
    last = w[2] ?? w[1] ?? "";
    redraw();
  },
  best(w) {
    // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth. comes a lot, keep it quick
    if (w.length < 6 || w[1].length < 4) return;
    best = { uci: w[1], san: w[2], kind: w[3], score: parseInt(w[4], 10) || 0, depth: parseInt(w[5], 10) || 0 };
    redraw();
  },
};

// line endings (\r\n too) are already cut. node exits by itself once stdin closes
const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line: string) => {
  const w = line.trim().split(/\s+/);
  // unknown stuff gets skipped, hasOwn so "toString" cant sneak through
  if (!Object.hasOwn(handlers, w[0])) return;
  try {
    handlers[w[0]](w);
  } catch (e) {
    // crashing = turned off
    send(`log oops: ${e instanceof Error ? e.message : String(e)}`);
    console.error(e);
  }
});
