#!/usr/bin/env python3
"""Checks the match server (tools/ko_relay.py) without the game: fake players talk to a real server.

    python3 tools/ko_relay_test.py          # prints one "ok" or "FAIL" line per check; exit code 1 on any FAIL

tools/check.sh runs it. It starts its own server on a spare port with its own ratings file.
"""
import asyncio
import json
import os
import subprocess
import sys
import tempfile

PORT = 17790
HERE = os.path.dirname(os.path.abspath(__file__))
failed = 0


def check(ok, what):
    global failed
    print(("  ok    " if ok else "  FAIL  ") + what, flush=True)
    if not ok:
        failed += 1


class Fake:
    """A player as the server sees one."""

    async def connect(self, hello):
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", PORT)
        self.lines = []
        self.say(hello)
        return self

    def say(self, line):
        self.writer.write((line + "\n").encode())

    async def hear(self, start, wait=3.0):
        """The next line that begins with `start` (earlier ones are kept in self.lines); None if none comes."""
        for i, line in enumerate(self.lines):
            if line.startswith(start):
                return self.lines.pop(i)
        try:
            while True:
                raw = await asyncio.wait_for(self.reader.readline(), wait)
                if not raw:
                    return None
                line = raw.decode().strip()
                if line.startswith(start):
                    return line
                self.lines.append(line)
        except (asyncio.TimeoutError, ConnectionError):
            return None

    async def closed(self, wait=3.0):
        try:
            while True:
                if not await asyncio.wait_for(self.reader.readline(), wait):
                    return True
        except asyncio.TimeoutError:
            return False
        except ConnectionError:
            return True

    def leave(self):
        self.writer.close()


async def run(data):
    # --- the lobby: two players see each other, one challenges, the other accepts
    ann = await Fake().connect("HELLO 2 Ann keyann1 0")
    you = await ann.hear("YOU ")
    check(you == "YOU Ann 1000 0 0 0", "a new player starts at 1000 with no place on the board (%s)" % you)
    bob = await Fake().connect("HELLO 2 Bob keybob1 3")
    await bob.hear("YOU ")
    seen = await ann.hear("LIST Bob")
    check(seen == "LIST Bob:1000:0", "the one who was there first is told of the newcomer (%s)" % seen)
    seen = await bob.hear("LIST ")
    check(seen == "LIST Ann:1000:0", "the newcomer is told who is there (%s)" % seen)

    thief = await Fake().connect("HELLO 2 ann otherkey 0")
    refused = await thief.hear("REFUSED")
    check(refused is not None and refused.startswith("REFUSED NAME"), "a taken name with another key is refused (%s)" % refused)
    bad = await Fake().connect("HELLO 2 x keykey1 0")
    check((await bad.hear("REFUSED")) is not None, "a one-letter name is refused")

    ann.say("CHALLENGE Bob")
    asked = await bob.hear("CHALLENGED")
    check(asked == "CHALLENGED Ann 1000", "the challenged player is asked (%s)" % asked)
    bob.say("DECLINE Ann")
    check((await ann.hear("DECLINED")) == "DECLINED Bob", "a refusal reaches the challenger")
    ann.say("CHALLENGE Bob")
    await bob.hear("CHALLENGED")
    bob.say("ACCEPT Ann")
    opp_a, role_a = await ann.hear("OPP "), await ann.hear("ROLE ")
    opp_b, role_b = await bob.hear("OPP "), await bob.hear("ROLE ")
    check((opp_a, role_a, opp_b, role_b) == ("OPP Bob 1000", "ROLE host", "OPP Ann 1000", "ROLE guest"),
          "both are told their opponent and their role (%s / %s / %s / %s)" % (opp_a, role_a, opp_b, role_b))

    # --- a third player sees them busy and cannot challenge them
    cat = await Fake().connect("HELLO 2 Cat keycat1 1")
    seen = await cat.hear("LIST ")
    check(seen == "LIST Ann:1000:1 Bob:1000:1", "a third player sees the two as busy (%s)" % seen)
    cat.say("CHALLENGE Ann")
    check((await cat.hear("DECLINED")) == "DECLINED Ann", "a busy player cannot be challenged")

    # --- the bout: the games' lines pass, the server's do not, the result moves the ratings
    ann.say("ME 0")
    ann.say("S START 1")
    bob.say("S START 1")
    ann.say("P 1 0 0")
    check((await bob.hear("ME")) == "ME 0" and (await bob.hear("P ")) == "P 1 0 0", "the games' own lines are passed on")
    ann.say("S RESULT 1 W")
    bob.say("S RESULT 1 L")
    ra, rb = await ann.hear("RATED"), await bob.hear("RATED")
    check(ra == "RATED 1016 16 1" and rb == "RATED 984 -16 2", "the winner gains what the loser gives up (%s / %s)" % (ra, rb))
    check(not any(l.startswith("S ") for l in ann.lines + bob.lines), "lines for the server are not passed to the other game")

    # --- a rematch that both claim to have won moves nothing
    ann.say("S START 2")
    ann.say("S RESULT 2 W")
    bob.say("S RESULT 2 W")
    check((await ann.hear("RATED", 1.0)) is None, "two players who both claim the win are not rated")

    # --- leaving a bout that has begun loses it
    ann.say("S START 3")
    bob.leave()
    ra = await ann.hear("RATED")
    check(ra is not None and ra.startswith("RATED 1031 15 1"), "whoever leaves a bout in progress loses it (%s)" % ra)
    check(await ann.closed(), "the one left behind is disconnected, so the game returns to the lobby")
    top = await cat.hear("TOP Ann:1031")
    check(top == "TOP Ann:1031:2:0 Bob:969:0:2", "the board lists the rated players, best first (%s)" % top)

    # --- two who challenge each other are put together; the ratings were kept in the file
    ann = await Fake().connect("HELLO 2 Ann keyann1 0")
    you = await ann.hear("YOU ")
    check(you == "YOU Ann 1031 1 2 0", "a returning player has the same rating and place (%s)" % you)
    ann.say("CHALLENGE Cat")
    await cat.hear("CHALLENGED")
    cat.say("CHALLENGE Ann")
    check((await ann.hear("ROLE ")) == "ROLE host" and (await cat.hear("ROLE ")) == "ROLE guest", "two who challenge each other are put together")
    for p in (ann, cat):
        p.leave()
    await asyncio.sleep(0.5)
    with open(data, encoding="utf-8") as f:
        saved = json.load(f)["players"]
    check(saved["ann"]["rating"] == 1031 and saved["bob"]["rating"] == 969 and saved["ann"]["wins"] == 2,
          "the ratings are in the file (%s)" % {k: (v["rating"], v["wins"], v["losses"]) for k, v in saved.items()})

    # --- the old relay still pairs two games by room and copies their bytes
    one = await Fake().connect("HELLO 1 testroom")
    two = await Fake().connect("HELLO 1 testroom")
    check((await one.hear("ROLE ")) == "ROLE host" and (await two.hear("ROLE ")) == "ROLE guest", "the plain relay still pairs two games by room")
    one.say("S START 1")
    check((await two.hear("S START")) == "S START 1", "the plain relay copies every line, whatever it says")
    one.leave()
    two.leave()


def main():
    folder = tempfile.mkdtemp(prefix="ko_relay_test_")
    data = os.path.join(folder, "players.json")
    server = subprocess.Popen([sys.executable, os.path.join(HERE, "ko_relay.py"), "--port", str(PORT), "--host", "127.0.0.1", "--data", data],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        import time
        time.sleep(1.5)
        asyncio.run(asyncio.wait_for(run(data), 90))
    except Exception as e:                            # a server that is not there, or a check that hung
        check(False, "the test ran to its end (%s: %s)" % (type(e).__name__, e))
    finally:
        server.terminate()
        server.wait()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
