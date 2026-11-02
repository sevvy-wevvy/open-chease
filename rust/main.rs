// open-chease plugin example in rust
//
// how it works: open-chease starts this program and talks to it like a uci engine.
// it writes lines to our stdin, we write lines to stdout. one thing per line,
// words split by spaces. full list of lines is in docs/PLUGINS.md on the main repo.
//
// what it does: draws the best move as an arrow, has a bool and a choice setting,
// a "Say hi" item in the ... menu and a little box next to the board.
//
// only std, no cargo, no crates. open-chease builds it for you from plugin.txt,
// by hand its just
//   rustc -O -o myplugin main.rs

use std::io::{self, BufRead, Write};

// colors are #rgb, #rrggbb or #aarrggbb. aa is how see-through, 00 invisible, ff solid
const COLORS: [&str; 4] = ["1fe0e0", "40e070", "ff5fb0", "ffd23f"];

struct Best {
    uci: String,
    san: String,
    kind: String, // cp or mate
    score: i32,
    depth: i32,
}

struct State {
    arrow: bool, // settings, filled by set lines
    color: usize,
    have_board: bool,
    team: String,
    tomove: char, // from the fen, the best score is for whoever moves
    last: String,
    best: Option<Best>,
}

// everything goes through here. rust buffers stdout too, so flush once at the end
// of every batch or open-chease might not see it. errors get ignored on purpose,
// if the pipe is gone we quit anyway when stdin closes
fn send(lines: &[String]) {
    let out = io::stdout();
    let mut out = out.lock();
    for l in lines {
        let _ = writeln!(out, "{}", l);
    }
    let _ = out.flush();
}

// a bad number becomes 0 instead of crashing. if we crash open-chease turns us off
fn num(s: &str) -> i32 {
    s.parse().unwrap_or(0)
}

fn score_text(st: &State, b: &Best) -> String {
    // score is for the side to move, flip it so + always means good for white.
    // cp is hundredths of a pawn, mate is moves till mate
    let s = if st.tomove == 'b' { -b.score } else { b.score };
    if b.kind == "mate" {
        format!("{}M{}", if s > 0 { "+" } else { "-" }, s.abs())
    } else {
        format!("{:+.2}", s as f64 / 100.0)
    }
}

fn redraw(st: &State) {
    if !st.have_board {
        return;
    }
    let c = COLORS[st.color];
    let mut out = Vec::new();

    // draw lines pile up and nothing changes on screen till "show",
    // then it swaps in all of it at once. so build the whole picture every time
    if let Some(b) = &st.best {
        if st.arrow {
            // squares are names like e2, they follow the board when black is at the bottom.
            // uci is from+to (e2e4), a promotion has a 5th letter (e7e8q) we dont need
            out.push(format!("draw arrow {} {} #c0{}", &b.uci[0..2], &b.uci[2..4], c));
            out.push(format!("draw text {} #ffffff {}", &b.uci[2..4], b.san));
        }
    }

    // the box next to the board. panel starts it, each line is a row, max 10 rows.
    // a line can start with a color, without one it gets the normal text color
    out.push("panel Rust example".to_string());
    out.push(format!("line you play {}", st.team));
    if !st.last.is_empty() {
        out.push(format!("line last move {}", st.last));
    }
    if let Some(b) = &st.best {
        out.push(format!("line #{} best {}  {}  d{}", c, b.san, score_text(st, b), b.depth));
    }
    out.push("show".to_string());
    send(&out);
}

fn handle(st: &mut State, w: &[&str]) {
    match w {
        ["hello", ..] => {
            // "hello 1 myplugin": protocol version, then our folder name.
            // settings: open-chease draws them in Gear > Plugins, saves them and sends
            // "set <key> <value>" right after each one with the saved value, and again on every change.
            //   setting bool <key> <default 0/1> <label> | <tip>
            //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
            // menu <key> <label> | <tip>: an item in the ... menu, label 23 chars max, 4 per plugin.
            // clicking it sends "click <key>". same key again changes it, "menu hi" alone removes it.
            // older open-chease versions dont know menu and just skip the line
            send(&[
                "setting bool arrow 1 Best move arrow | draws an arrow for the engines move".to_string(),
                "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color".to_string(),
                "menu hi Say hi | pops a note so you know its alive".to_string(),
                "log running".to_string(),
            ]);
        }
        ["set", key, v, ..] => {
            // "set color 2", values are always whole numbers
            let v = num(v);
            match *key {
                "arrow" => st.arrow = v != 0,
                "color" if (0..4).contains(&v) => st.color = v as usize,
                _ => {}
            }
            redraw(st);
        }
        ["click", "hi", ..] => {
            // from the menu item (or a setting button if we had one)
            send(&["note hi from the Rust example".to_string()]);
        }
        ["board", x, ..] => {
            // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
            // "board none" = lost it. clear works right away, no show needed
            st.have_board = *x != "none";
            if !st.have_board {
                send(&["clear".to_string()]);
            }
            redraw(st);
        }
        ["team", t, ..] => {
            st.team = t.to_string();
            redraw(st);
        }
        ["position", _, side, ..] => {
            // "position <fen>" after every move. the old best move is for the old position
            st.tomove = side.chars().next().unwrap_or('w');
            st.best = None;
            redraw(st);
        }
        ["move", uci, rest @ ..] => {
            // "move e7e5 e5" = uci then san. san can be missing
            st.last = rest.first().unwrap_or(uci).to_string();
            redraw(st);
        }
        ["best", uci, san, kind, score, depth, ..] => {
            // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
            // comes again each time the engine gets deeper, so keep this quick.
            // is_ascii check so the [0..2] slices above cant land inside a utf-8 char
            if uci.len() >= 4 && uci.is_ascii() {
                st.best = Some(Best {
                    uci: uci.to_string(),
                    san: san.to_string(),
                    kind: kind.to_string(),
                    score: num(score),
                    depth: num(depth),
                });
            }
            redraw(st);
        }
        // anything else gets skipped. newer open-chease versions might send more
        _ => {}
    }
}

fn main() {
    let mut st = State {
        arrow: true,
        color: 0,
        have_board: false,
        team: "white".to_string(),
        tomove: 'w',
        last: String::new(),
        best: None,
    };
    // lines() ends when stdin closes, that means open-chease quit or turned us off.
    // it also cuts the \n (and \r\n on windows). a line thats not utf-8 gets skipped
    for ln in io::stdin().lock().lines() {
        let ln = match ln {
            Ok(l) => l,
            Err(e) if e.kind() == io::ErrorKind::InvalidData => continue,
            Err(_) => break,
        };
        let w: Vec<&str> = ln.split_whitespace().collect();
        if !w.is_empty() {
            handle(&mut st, &w);
        }
    }
}
