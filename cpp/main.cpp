// open-chease plugin example in C++
//
// open-chease starts this program and talks to it kind of like a uci engine.
// lines come in on stdin, we write ours to stdout. one thing per line, words split by spaces.
// the full list of lines is in docs/PLUGINS.md on the main repo
//
// it draws the best move as an arrow, has a bool and a choice setting,
// a "Say hi" item in the ... menu and a little box next to the board
//
// only the standard library, C++17. open-chease builds it for you from plugin.txt,
// by hand its just
//   c++ -O2 -std=c++17 -o myplugin main.cpp

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using std::string;

// #rgb, #rrggbb or #aarrggbb work. aa is how see-through it is (00 invisible, ff solid)
static const char *colors[] = {"1fe0e0", "40e070", "ff5fb0", "ffd23f"};

struct Best {
    string uci, san, kind; // kind is cp or mate
    int score = 0, depth = 0;
};

static int arrow = 1, color = 0; // set lines fill these
static bool have_board = false, have_best = false;
static string team = "white", last;
static char tomove = 'w'; // from the fen, the best score is for whoever moves
static Best best;

// '\n' not std::endl, then one flush at the end of the batch.
// stdout into a pipe is block buffered so without the flush open-chease never sees it
static void send(const std::vector<string> &lines)
{
    for (auto &l : lines) std::cout << l << '\n';
    std::cout.flush();
}

static string score_text()
{
    // flipped so + is always good for white.
    // cp is hundredths of a pawn, mate is moves till mate
    int s = tomove == 'b' ? -best.score : best.score;
    char buf[32];
    if (best.kind == "mate")
        std::snprintf(buf, sizeof(buf), "%sM%d", s > 0 ? "+" : "-", std::abs(s));
    else
        std::snprintf(buf, sizeof(buf), "%+.2f", s / 100.0);
    return buf;
}

static void redraw()
{
    if (!have_board) return;
    string c = colors[color];
    std::vector<string> out;

    // nothing changes on screen till "show", then all of it swaps in at once
    // and the old stuff is gone. so send the whole picture every time
    if (have_best && arrow) {
        // square names follow the board when black is at the bottom.
        // uci is from+to (e2e4), promotions add a 5th letter (e7e8q) we dont need here
        string from = best.uci.substr(0, 2), to = best.uci.substr(2, 2);
        out.push_back("draw arrow " + from + " " + to + " #c0" + c);
        out.push_back("draw text " + to + " #ffffff " + best.san);
    }

    // panel is the title of the box, then up to 10 line rows.
    // the color at the start of a line is optional, no color = normal text color
    out.push_back("panel C++ example");
    out.push_back("line you play " + team);
    if (!last.empty()) out.push_back("line last move " + last);
    if (have_best)
        out.push_back("line #" + c + " best " + best.san + "  " + score_text() + "  d" + std::to_string(best.depth));
    out.push_back("show");
    send(out);
}

static void handle(const std::vector<string> &w)
{
    const string &cmd = w[0];
    size_t n = w.size();
    if (cmd == "hello") {
        // "hello 1 myplugin" = protocol version, then our folder name.
        // settings show up in Gear > Plugins and get saved for us. open-chease sends
        // "set <key> <value>" right after each one with the saved value, and again on every change
        //   setting bool <key> <default 0/1> <label> | <tip>
        //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
        // menu <key> <label> | <tip> is an item in the ... menu (label 23 chars max, 4 per plugin).
        // clicking it sends "click <key>". same key again changes it, "menu hi" alone removes it.
        // old open-chease versions dont know menu and skip it
        send({
            "setting bool arrow 1 Best move arrow | draws an arrow for the engines move",
            "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color",
            "menu hi Say hi | pops a note so you know its alive",
            "log running",
        });
    } else if (cmd == "set" && n >= 3) {
        // "set color 2", always a whole number
        int v = std::atoi(w[2].c_str());
        if (w[1] == "arrow") arrow = v != 0;
        if (w[1] == "color" && v >= 0 && v < 4) color = v;
        redraw();
    } else if (cmd == "click" && n >= 2) {
        // from the menu item, a button setting would send one too
        if (w[1] == "hi") send({"note hi from the C++ example"});
    } else if (cmd == "board" && n >= 2) {
        // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
        // "board none" = lost it. clear wipes our stuff right away, no show needed
        have_board = w[1] != "none";
        if (!have_board) send({"clear"});
        redraw();
    } else if (cmd == "team" && n >= 2) {
        team = w[1];
        redraw();
    } else if (cmd == "position" && n >= 3) {
        // "position <fen>" after every move, w[2] is whos turn.
        // the old best move was for the old position so drop it
        tomove = w[2][0];
        have_best = false;
        redraw();
    } else if (cmd == "move" && n >= 2) {
        // "move e7e5 e5" = uci then san. san can be missing
        last = n >= 3 ? w[2] : w[1];
        redraw();
    } else if (cmd == "best" && n >= 6) {
        // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
        // comes in every time the engine goes deeper, keep it quick
        best = {w[1], w[2], w[3], std::atoi(w[4].c_str()), std::atoi(w[5].c_str())};
        have_best = best.uci.size() >= 4;
        redraw();
    }
    // skip the rest, newer open-chease versions might send more
}

int main()
{
    string ln;
    // getline fails when stdin closes = open-chease quit or turned us off
    while (std::getline(std::cin, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back(); // windows \r\n
        std::istringstream in(ln);
        std::vector<string> w;
        for (string s; in >> s;) w.push_back(s);
        if (!w.empty()) handle(w);
    }
    return 0;
}
