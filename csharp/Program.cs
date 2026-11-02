// open-chease plugin example in C#
//
// open-chease starts this and talks to it pretty much like a uci engine. it writes
// lines to our stdin and we write lines back on stdout, one thing per line, words split by spaces.
// every line is listed in docs/PLUGINS.md on the main repo
//
// draws the best move as an arrow, has a bool and a choice setting,
// a "Say hi" item in the ... menu and a little box next to the board
//
// only the .NET base library, no nuget packages. open-chease builds it for you
// from plugin.txt, by hand its just
//   dotnet build -c Release -o out
// and then "dotnet out/myplugin.dll" runs it

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

static class Plugin
{
    // #rgb, #rrggbb or #aarrggbb, aa is how see-through (00 invisible, ff solid)
    static readonly string[] Colors = { "1fe0e0", "40e070", "ff5fb0", "ffd23f" };

    // our own stdout writer. utf-8 with no BOM (a BOM glues junk onto the first line),
    // plain \n, AutoFlush off so its one flush per batch
    static readonly StreamWriter Out = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = false, NewLine = "\n" };

    static bool arrow = true; // set lines fill these
    static int color = 0;
    static bool haveBoard = false;
    static string team = "white";
    static string toMove = "w"; // from the fen, the best score is for whoever moves
    static string last = "";
    static Best? best = null;

    record Best(string Uci, string San, string Kind, int Score, int Depth);

    static void Send(params string[] lines)
    {
        foreach (var l in lines) Out.WriteLine(l);
        Out.Flush(); // skip this and open-chease might not see a thing
    }

    // junk turns into 0, no exception
    static int Num(string s) => int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out var v) ? v : 0;

    static string ScoreText(Best b)
    {
        // the score is for whoever moves, flipped so + is good for white.
        // cp is hundredths of a pawn, mate is moves till mate
        int s = toMove == "b" ? -b.Score : b.Score;
        if (b.Kind == "mate") return (s > 0 ? "+M" : "-M") + Math.Abs(s);
        // or a german/french windows writes 0,36
        return (s >= 0 ? "+" : "") + (s / 100.0).ToString("0.00", CultureInfo.InvariantCulture);
    }

    static void Redraw()
    {
        if (!haveBoard) return;
        string c = Colors[color];
        var o = new List<string>();

        // draw lines just pile up till "show", then the whole lot swaps in and the old
        // stuff is gone. so send everything every time
        if (best != null && arrow)
        {
            // square names follow the board when black is at the bottom.
            // uci is from+to (e2e4), a promotion has a 5th letter (e7e8q), dont need it
            string from = best.Uci.Substring(0, 2), to = best.Uci.Substring(2, 2);
            o.Add($"draw arrow {from} {to} #c0{c}");
            o.Add($"draw text {to} #ffffff {best.San}");
        }

        // the box next to the board. panel is its title, then up to 10 line rows,
        // each can start with a color (leave it out for the normal text color)
        o.Add("panel C# example");
        o.Add($"line you play {team}");
        if (last != "") o.Add($"line last move {last}");
        if (best != null) o.Add($"line #{c} best {best.San}  {ScoreText(best)}  d{best.Depth}");
        o.Add("show");
        Send(o.ToArray());
    }

    static void Handle(string[] w)
    {
        switch (w[0])
        {
            case "hello":
                // "hello 1 myplugin" = protocol version, then our folder name.
                // settings land in Gear > Plugins and get saved for us. right after each one
                // we get "set <key> <value>" with the saved value, then again on every change
                //   setting bool <key> <default 0/1> <label> | <tip>
                //   setting choice <key> <default index> <options split by commas, _ is a space> <label> | <tip>
                // menu <key> <label> | <tip> adds to the ... menu, label 23 chars max, 4 per plugin.
                // a click sends "click <key>". same key again changes it, "menu hi" alone removes it.
                // older open-chease doesnt know menu, it just skips the line
                Send("setting bool arrow 1 Best move arrow | draws an arrow for the engines move",
                     "setting choice color 0 Cyan,Green,Pink,Yellow Color | arrow and text color",
                     "menu hi Say hi | pops a note so you know its alive",
                     "log running");
                break;
            case "set" when w.Length >= 3:
                // "set color 2", always whole numbers
                int v = Num(w[2]);
                if (w[1] == "arrow") arrow = v != 0;
                if (w[1] == "color" && v >= 0 && v < Colors.Length) color = v;
                Redraw();
                break;
            case "click" when w.Length >= 2:
                // "click hi" from the menu item (a button setting sends these too)
                if (w[1] == "hi") Send("note hi from the C# example");
                break;
            case "board" when w.Length >= 2:
                // "board 220 140 576 white" = x y on screen, size in px, side at the bottom
                // "board none" = lost it. clear wipes our stuff right away, no show needed
                haveBoard = w[1] != "none";
                if (!haveBoard) Send("clear");
                Redraw();
                break;
            case "team" when w.Length >= 2:
                team = w[1];
                Redraw();
                break;
            case "position" when w.Length >= 3:
                // "position <fen>" after every move, w[2] is whos turn.
                // old best move is for the old position, drop it
                toMove = w[2];
                best = null;
                Redraw();
                break;
            case "move" when w.Length >= 2:
                // "move e7e5 e5" = uci then san. san can be missing
                last = w.Length >= 3 ? w[2] : w[1];
                Redraw();
                break;
            case "best" when w.Length >= 6 && w[1].Length >= 4:
                // "best g1f3 Nf3 cp 36 11" = uci, san, cp or mate, score, depth.
                // shows up again every time the engine gets deeper, keep it quick
                best = new Best(w[1], w[2], w[3], Num(w[4]), Num(w[5]));
                Redraw();
                break;
            // anything else falls through, newer versions might send more
        }
    }

    static void Main()
    {
        var stdin = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
        string? ln;
        // null = stdin closed, open-chease quit or turned us off. cuts \n and \r\n both
        while ((ln = stdin.ReadLine()) != null)
        {
            var w = ln.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (w.Length == 0) continue;
            try
            {
                Handle(w);
            }
            catch (Exception e)
            {
                // a crash gets us turned off, better to log it and keep going
                Send("log oops: " + e.Message);
                Console.Error.WriteLine(e);
            }
        }
    }
}
