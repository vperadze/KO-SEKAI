#!/usr/bin/env python3
"""KO-SEKAI match server: the lobby where players find each other, their ratings and the leaderboard.
It is also still the plain relay it began as.

Run it on any machine every player can reach (a small cloud server is plenty):

    python3 tools/ko_relay.py                                  # listens on port 7777
    python3 tools/ko_relay.py --port 9000 --data /mnt/ko/players.json

It needs Python 3.8 or newer and nothing else. Open the port for TCP in the machine's firewall.

--data is the one file that holds every player's rating (default: ko_players.json beside this script).
It is rewritten whole after each rated bout, by way of a temporary file, so it may sit on a network
volume (on AWS: an Amazon FSx for NetApp ONTAP volume mounted over NFS) and survive the server itself.

Two kinds of player connect, told apart by their first line.

"HELLO 1 <room>"  the relay. The first two with the same room are put in a ring together, one told
    "ROLE host" and the other "ROLE guest", and from then on every byte of each is copied to the other.
    Nothing is rated. In the game: PLAY ONLINE > JOIN A MATCH, with this machine's address.

"HELLO 2 <name> <key> <fighter>"  the lobby. In the game: PLAY ONLINE > FIND A MATCH.
    The name is the player's for good: the key (a random word the game makes up once and keeps) must be
    the same every time, or the name is refused. The server then says:
        YOU <name> <rating> <place> <wins> <losses>      place 0: no rated bout yet
        LIST <name>:<rating>:<busy> ...                  everyone else here; busy 1 is in a bout
        TOP <name>:<rating>:<wins>:<losses> ...          the ten best
    and again whenever one of them changes. The player says:
        CHALLENGE <name>        the other is told "CHALLENGED <name> <rating>"
        ACCEPT <name>           both are told "OPP <name> <rating>" and then their "ROLE"
        DECLINE <name>          the challenger is told "DECLINED <name>"
    Two players who challenge each other are put together without further asking.
    In a bout the two games' lines are passed on as by the relay, except those that begin with "S ",
    which are for the server:
        S START <n>             a bout has begun
        S RESULT <n> W|L        how it ended for the sender
    When both have reported and one says W and the other L, the ratings move (Elo, K = 32, everyone
    starts at 1000) and each is told "RATED <rating> <change> <place>". Two L are a double knockout and
    two W a disagreement: neither moves anything. Whoever leaves a bout that has begun and is not yet
    settled has lost it.
"""
import argparse
import asyncio
import json
import os
import re
import time

START_RATING = 1000
K = 32
NAME = re.compile(r"^[A-Za-z0-9_-]{2,12}$")
KEY = re.compile(r"^[A-Za-z0-9]{6,40}$")


def log(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def shut(writer):
    try:
        writer.close()
    except Exception:
        pass


# ---------------------------------------------------------------- the players' records

class Book:
    """Every player who ever came by: name, key, rating, wins, losses, draws."""

    def __init__(self, path):
        self.path = path
        self.players = {}                         # lower-case name -> record
        if path and os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                self.players = json.load(f).get("players", {})
            log("%d players read from %s" % (len(self.players), path))

    def save(self):
        if not self.path:
            return
        tmp = self.path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"players": self.players}, f, indent=1)
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp, self.path)            # never a half-written file, whatever happens meanwhile
        except OSError as e:
            log("COULD NOT SAVE %s: %s" % (self.path, e))

    def enter(self, name, key):
        """The record for this name, made if new; None if the name is another player's."""
        rec = self.players.get(name.lower())
        if rec is None:
            rec = {"name": name, "key": key, "rating": START_RATING, "wins": 0, "losses": 0, "draws": 0}
            self.players[name.lower()] = rec
            self.save()
        elif rec["key"] != key:
            return None
        return rec

    def ranked(self):
        played = [r for r in self.players.values() if r["wins"] + r["losses"] + r["draws"] > 0]
        return sorted(played, key=lambda r: (-r["rating"], -r["wins"], r["name"].lower()))

    def place(self, rec):
        for i, r in enumerate(self.ranked()):
            if r is rec:
                return i + 1
        return 0

    def settle(self, winner, loser):
        """Moves the two ratings; returns the two changes."""
        expected = 1.0 / (1.0 + 10 ** ((loser["rating"] - winner["rating"]) / 400.0))
        gain = max(1, int(round(K * (1.0 - expected))))
        winner["rating"] += gain
        loser["rating"] = max(100, loser["rating"] - gain)
        winner["wins"] += 1
        loser["losses"] += 1
        self.save()
        return gain, -gain

    def draw(self, a, b):
        a["draws"] += 1
        b["draws"] += 1
        self.save()


book = None


# ---------------------------------------------------------------- the relay (HELLO 1)

class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer
        self.partner = None
        self.name = "%s:%s" % writer.get_extra_info("peername")[:2]


waiting = {}          # room -> the client waiting in it


async def relay(me, room):
    reader, writer = me.reader, me.writer
    try:
        writer.write(b"WAIT\n")
        await writer.drain()

        other = waiting.get(room)
        if other is not None and not other.writer.is_closing():
            del waiting[room]
            me.partner, other.partner = other, me
            other.writer.write(b"ROLE host\n")
            writer.write(b"ROLE guest\n")
            log("room %s: %s meets %s" % (room, other.name, me.name))
        else:
            waiting[room] = me
            log("room %s: %s is waiting" % (room, me.name))

        while True:
            data = await reader.read(4096)
            if not data:
                break
            if me.partner is not None:               # until there is a partner there is nobody to tell
                me.partner.writer.write(data)
                await me.partner.writer.drain()
    except (asyncio.TimeoutError, ConnectionError, OSError):
        pass
    finally:
        if waiting.get(room) is me:
            del waiting[room]
        if me.partner is not None:
            shut(me.partner.writer)
        shut(writer)
        log("room %s: %s left" % (room, me.name))


# ---------------------------------------------------------------- the lobby (HELLO 2)

class Player:
    def __init__(self, writer, rec, fighter):
        self.writer, self.rec, self.fighter = writer, rec, fighter
        self.partner = None
        self.asked = None             # the player this one has challenged and who has not answered
        self.bout_open = False        # a bout has begun and is not settled (shared by the two of a pair)
        self.report = None            # "W" or "L" for the open bout

    @property
    def name(self):
        return self.rec["name"]

    def tell(self, line):
        if not self.writer.is_closing():
            try:
                self.writer.write((line + "\n").encode("utf-8"))
            except Exception:
                pass


lobby = {}            # lower-case name -> Player, everyone connected with HELLO 2


def tell_you(p):
    p.tell("YOU %s %d %d %d %d" % (p.name, p.rec["rating"], book.place(p.rec), p.rec["wins"], p.rec["losses"]))


def tell_everyone():
    """The list and the board, to everybody who is not in a bout (their screens show neither)."""
    top = " ".join("%s:%d:%d:%d" % (r["name"], r["rating"], r["wins"], r["losses"]) for r in book.ranked()[:10])
    for p in list(lobby.values()):
        if p.partner is not None:
            continue
        others = " ".join("%s:%d:%d" % (o.name, o.rec["rating"], 1 if o.partner is not None else 0)
                          for o in sorted(lobby.values(), key=lambda o: o.name.lower()) if o is not p)
        tell_you(p)
        p.tell(("LIST " + others).rstrip())
        p.tell(("TOP " + top).rstrip())


def pair(a, b):
    """a asked, b said yes: a runs the match."""
    a.partner, b.partner = b, a
    a.asked = b.asked = None
    a.bout_open = b.bout_open = False
    a.report = b.report = None
    # Whoever else was waiting for an answer from either of them will not get one.
    for p in lobby.values():
        if p.asked in (a, b):
            p.tell("DECLINED %s" % p.asked.name)
            p.asked = None
    a.tell("OPP %s %d" % (b.name, b.rec["rating"]))
    b.tell("OPP %s %d" % (a.name, a.rec["rating"]))
    a.tell("ROLE host")
    b.tell("ROLE guest")
    log("lobby: %s (%d) fights %s (%d)" % (a.name, a.rec["rating"], b.name, b.rec["rating"]))
    tell_everyone()


def rated(winner, loser, why):
    gain, loss = book.settle(winner.rec, loser.rec)
    winner.tell("RATED %d %d %d" % (winner.rec["rating"], gain, book.place(winner.rec)))
    loser.tell("RATED %d %d %d" % (loser.rec["rating"], loss, book.place(loser.rec)))
    log("lobby: %s beat %s (%s): %d and %d" % (winner.name, loser.name, why, winner.rec["rating"], loser.rec["rating"]))


def for_server(me, words):
    """A line that began with "S " from a player in a bout."""
    other = me.partner
    if len(words) >= 1 and words[0] == "START":
        if not me.bout_open:
            me.bout_open = other.bout_open = True
            me.report = other.report = None
    elif len(words) >= 3 and words[0] == "RESULT" and words[2] in ("W", "L") and me.bout_open:
        me.report = words[2]
        if other.report is None:
            return
        if me.report != other.report:
            winner, loser = (me, other) if me.report == "W" else (other, me)
            rated(winner, loser, "both agree")
        elif me.report == "L":
            book.draw(me.rec, other.rec)
            for p in (me, other):
                p.tell("RATED %d 0 %d" % (p.rec["rating"], book.place(p.rec)))
            log("lobby: %s and %s knocked each other out" % (me.name, other.name))
        else:
            log("lobby: %s and %s both say they won; nothing is rated" % (me.name, other.name))
        me.bout_open = other.bout_open = False
        me.report = other.report = None


def in_lobby(me, words):
    """A line from a player who is not in a bout."""
    if len(words) < 2:
        return
    other = lobby.get(words[1].lower())
    if words[0] == "CHALLENGE":
        if other is None or other is me or other.partner is not None:
            me.tell("DECLINED %s" % words[1])
        elif other.asked is me:                       # each has asked the other
            pair(other, me)
        else:
            me.asked = other
            other.tell("CHALLENGED %s %d" % (me.name, me.rec["rating"]))
    elif words[0] == "ACCEPT":
        if other is not None and other.asked is me and other.partner is None:
            pair(other, me)
    elif words[0] == "DECLINE":
        if other is not None and other.asked is me:
            other.asked = None
            other.tell("DECLINED %s" % me.name)


async def play(reader, writer, words):
    name = words[2] if len(words) > 2 else ""
    key = words[3] if len(words) > 3 else ""
    fighter = words[4] if len(words) > 4 else "-1"
    writer.write(b"WAIT\n")
    if not NAME.match(name) or not KEY.match(key):
        writer.write(b"REFUSED NAME A name is 2 to 12 letters or digits.\n")
        await writer.drain()
        shut(writer)
        return
    rec = book.enter(name, key)
    if rec is None:
        writer.write(b"REFUSED NAME That name belongs to another player. Choose another.\n")
        await writer.drain()
        shut(writer)
        return
    old = lobby.get(name.lower())
    if old is not None:                               # the same player, come again: the old line is dead
        shut(old.writer)
        gone(old)
    me = Player(writer, rec, fighter)
    lobby[name.lower()] = me
    log("lobby: %s is here (%d players)" % (me.name, len(lobby)))
    tell_everyone()
    try:
        while True:
            raw = await reader.readline()
            if not raw:
                break
            line = raw.decode("utf-8", "replace").strip()
            if not line:
                continue
            if me.partner is not None:
                if line.startswith("S "):
                    for_server(me, line.split()[1:])
                else:
                    me.partner.tell(line)
            else:
                in_lobby(me, line.split())
            await writer.drain()
            if me.partner is not None:
                await me.partner.writer.drain()
    except (asyncio.TimeoutError, ConnectionError, OSError, ValueError):
        pass
    finally:
        shut(writer)
        gone(me)


def gone(me):
    """A player's line has closed."""
    if lobby.get(me.name.lower()) is not me:
        return                                        # already seen off
    del lobby[me.name.lower()]
    other = me.partner
    if other is not None:
        if me.bout_open:
            rated(other, me, "%s left the bout" % me.name)
        me.partner = other.partner = None
        other.bout_open = False
        shut(other.writer)                            # the game shows "The opponent left." and comes back to the lobby
    for p in lobby.values():
        if p.asked is me:
            p.asked = None
            p.tell("DECLINED %s" % me.name)
    log("lobby: %s left (%d players)" % (me.name, len(lobby)))
    tell_everyone()


# ---------------------------------------------------------------- the door

async def serve(reader, writer):
    try:
        hello = await asyncio.wait_for(reader.readline(), 10)
    except (asyncio.TimeoutError, ConnectionError, OSError, ValueError):
        shut(writer)
        return
    words = hello.decode("utf-8", "replace").split()
    if len(words) < 2 or words[0] != "HELLO":
        shut(writer)                                  # not a KO-SEKAI game
        return
    if words[1] == "2":
        try:
            await play(reader, writer, words)
        except (ConnectionError, OSError):
            shut(writer)
    else:
        await relay(Client(reader, writer), (words[2] if len(words) > 2 else "-")[:32])


async def main():
    global book
    parser = argparse.ArgumentParser(description="KO-SEKAI match server")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--host", default="0.0.0.0", help="address to listen on (default: every network interface)")
    parser.add_argument("--data", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "ko_players.json"),
                        help="the file that keeps the players' ratings")
    args = parser.parse_args()
    book = Book(args.data)
    server = await asyncio.start_server(serve, args.host, args.port)
    log("KO-SEKAI relay listening on %s port %d; ratings in %s" % (args.host, args.port, args.data))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
