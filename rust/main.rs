// open-chease plugin example in rust
//
// open-chease starts this program and talks to it kinda like a uci engine. lines come in
// on stdin, we answer on stdout, one thing per line with words split by spaces.
// every line there is is in docs/PLUGINS.md on the main repo
//
// this one draws the best move as an arrow, has a bool and a choice setting,
// puts a "Say hi" item in the ... menu and a little box next to the board
//
// only std, no cargo and no crates. open-chease builds it from plugin.txt for you, by hand its
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
    arrow: bool, // these two are the settings
    color: usize,
    have_board: bool,
    team: String,
    tomove: char, // from the fen, the best score is for whoever moves
    last: String,
    best: Option<Best>,
}

// rust buffers stdout, without the flush open-chease might never see it.
// write errors get ignored on purpose, if the pipe is gone stdin closes too and we quit
fn send(lines: &[String]) {
    let out = io::stdout();
    let mut out = out.lock();
    for l in lines {
        let _ = writeln!(out, "{}", l);
    }
    let _ = out.flush();
}

// bad number = 0. a panic would get us turned off
fn num(s: &str) -> i32 {
    s.parse().unwrap_or(0)
}

fn score_text(st: &State, b: &Best) -> String {
    // the engine scores for whoever moves, flipped here so + is always good for white.
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

    // nothing changes on screen till "show", and then it replaces everything from last time.
    // so its the whole picture every time, not just whats new
    if let Some(b) = &st.best {
        if st.arrow {
            // squares are names like e2 and follow the board when black is at the bottom.
            // e7e8q style promotions have a 5th letter, we only need the first 4
            out.push(format!("draw arrow {} {} #c0{}", &b.uci[0..2], &b.uci[2..4], c));
            out.push(format!("draw text {} #ffffff {}", &b.uci[2..4], b.san));
        }
    }

    // the box next to the board, panel then up to 10 line rows.
    // no color at the start of a line = normal text color
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
            // "hello 1 myplugin" = protocol version, then our folder name
            //
            // open-chease draws settings in Gear > Plugins and saves them. right after each setting
            // line it sends "set <key> <value>" with the saved value, then again whenever it changes
            //   setting bool <key> <default 0/1> <label> | <tip>
            //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
            //
            // menu <key> <label> | <tip> adds to the ... menu (label 23 chars max, 4 per plugin)
            // and clicking it sends "click <key>". send the same key again to change it, "menu hi"
            // alone removes it. older open-chease versions dont know menu and just skip it
            send(&[
                "setting bool arrow 1 Best move arrow | draws an arrow for the engines move".to_string(),
                "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color".to_string(),
                "menu hi Say hi | pops a note so you know its alive".to_string(),
                "log running".to_string(),
            ]);
        }
        ["set", key, v, ..] => {
            // "set color 2". always whole numbers
            let v = num(v);
            match *key {
                "arrow" => st.arrow = v != 0,
                "color" if (0..4).contains(&v) => st.color = v as usize,
                _ => {}
            }
            redraw(st);
        }
        ["click", "hi", ..] => {
            // the menu item (a button setting would send this too)
            send(&["note hi from the Rust example".to_string()]);
        }
        ["board", x, ..] => {
            // "board 220 140 576 white" = x y on screen, size in px, side at the bottom.
            // "board none" means it lost the board. clear wipes it right away, no show needed
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
            // "position <fen>" comes after every move, old best move is useless now
            st.tomove = side.chars().next().unwrap_or('w');
            st.best = None;
            redraw(st);
        }
        ["move", uci, rest @ ..] => {
            // "move e7e5 e5", uci then san (san might not be there)
            st.last = rest.first().unwrap_or(uci).to_string();
            redraw(st);
        }
        ["best", uci, san, kind, score, depth, ..] => {
            // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
            // this one comes a lot (every time the engine goes deeper), keep it quick.
            // is_ascii because the [0..2] slices in redraw would panic in the middle of a utf-8 char
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
        // skip the rest, newer open-chease versions might send stuff we dont know
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
    // stdin closing means open-chease quit or turned us off, lines() just ends then.
    // \n and windows \r\n get cut for us. not utf-8? skipped
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
