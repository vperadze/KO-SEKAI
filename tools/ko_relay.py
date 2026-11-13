#!/usr/bin/env python3
"""KO-SEKAI relay server: lets two players meet without either of them opening a port.

Run it on any machine both players can reach (a small cloud server is plenty):

    python3 tools/ko_relay.py              # listens on port 7777
    python3 tools/ko_relay.py --port 9000

Both players then choose PLAY ONLINE > JOIN A MATCH in the game and type this machine's address.
The first two to arrive are put in a ring together. To meet one particular friend, both add the same
room word after a '#':  myserver.net#ourroom

It needs Python 3.8 or newer and nothing else. It knows nothing about boxing: it reads the first line of
each connection ("HELLO 1 <room>"), pairs two connections with the same room, tells one "ROLE host" and the
other "ROLE guest", and from then on copies every byte from each to the other. When one leaves, the other
is disconnected, which the game shows as "The opponent left."
"""
import argparse
import asyncio
import time


class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer
        self.partner = None
        self.name = "%s:%s" % writer.get_extra_info("peername")[:2]


waiting = {}          # room -> the client waiting in it


def log(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def shut(writer):
    try:
        writer.close()
    except Exception:
        pass


async def serve(reader, writer):
    me = Client(reader, writer)
    room = "-"
    try:
        hello = await asyncio.wait_for(reader.readline(), 10)
        words = hello.decode("utf-8", "replace").split()
        if len(words) < 2 or words[0] != "HELLO":
            return                                   # not a KO-SEKAI game
        room = (words[2] if len(words) > 2 else "-")[:32]
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


async def main():
    parser = argparse.ArgumentParser(description="KO-SEKAI relay server")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--host", default="0.0.0.0", help="address to listen on (default: every network interface)")
    args = parser.parse_args()
    server = await asyncio.start_server(serve, args.host, args.port)
    log("KO-SEKAI relay listening on %s port %d" % (args.host, args.port))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
