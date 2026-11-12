#!/usr/bin/env python3
"""Summarise a webcam session recording: framing, and what the move detector decided.  usage: recstat.py <rec.csv>"""
import sys, re, collections
rows = []; events = []
for line in open(sys.argv[1], errors="replace"):
    if line.startswith("#"): events.append(line[2:].strip()); continue
    v = line.strip().split(",")
    if len(v) >= 200:
        try: rows.append([float(x) for x in v[:200]])
        except ValueError: pass
if not rows: sys.exit("no frames")
secs = (rows[-1][0] - rows[0][0]) / 1000
def col(i, k): return sorted(r[2 + i * 6 + k] for r in rows)
def med(a): return a[len(a) // 2]
print(f"{secs:.0f} s, {len(rows)} frames ({len(rows) / max(secs, 1):.0f} per second), body seen {100 * sum(r[1] for r in rows) / len(rows):.0f}%")
for name, i in (("shoulders", 11), ("elbows", 13), ("fists", 15)):
    y = sorted(col(i, 1) + col(i + 1, 1)); below = 100 * sum(1 for t in y if t > 1.0) / len(y)
    print(f"  {name:9s} height in picture: median {med(y):.2f} (0 = top, 1 = bottom), below the picture {below:.0f}% of the time")
c = collections.Counter()
for e in events:
    m = re.search(r"arm (\d) PUNCH (\w+) (early|at end) after (\d+) ms", e)
    if m: c[f"PUNCH {'left' if m.group(1) == '0' else 'right'} {m.group(2)} ({m.group(3)})"] += 1; continue
    m = re.search(r"rejected, ([a-z ]+)", e)
    if m: c["rejected: " + m.group(1).strip()] += 1; continue
    m = re.search(r"stance (\d) (ON|off)", e)
    if m and m.group(2) == "ON": c["stance on: " + ["", "block", "evade left", "evade right", "duck"][int(m.group(1))]] += 1
for k, n in sorted(c.items(), key=lambda t: (-t[1], t[0])): print(f"  {n:3d}  {k}")
early = [int(m.group(1)) for e in events for m in [re.search(r"early after (\d+) ms", e)] if m]
if early: print(f"  early punch calls come a median {sorted(early)[len(early) // 2]} ms after the stroke starts")
