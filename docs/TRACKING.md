# Webcam tracking and move recognition

## Pipeline

```
webcam (640×480, 30 frames/s)  ->  crop around the body (256×256)  ->  pose model (MediaPipe pose, ONNX, CPU)
  ->  33 joints, smoothed (One-Euro)  ->  KSDetector (punches, block, evade, duck)  ->  game thread
```

| Stage | File | Notes |
|---|---|---|
| Camera, crop, model, smoothing, recording | `KSCameraTracker.cpp` | Own thread. Logs timing every 3 s under `LogKSCamera`. |
| Moves | `KSDetector.cpp` | Port of `tracking-test/tracker.js`, now ahead of it (early punch call, absolute block). |
| Hand-off to the game | `KSTracking.cpp` | `TakePunches()`, `IsHeld()`, HUD status. |

Healthy numbers, game alone on the machine: 30 frames/s, about 23 ms waiting for a frame, about 10 ms crop +
model. If the tracker reports less than 25 frames/s, something else is using the machine.

## What the player must do

The camera has to see **elbows and fists**, not just head and shoulders. Stand back, or tilt the laptop screen
back, until the STEP BACK warning goes away. Shoulders should be in the upper half of the picture.

Block is both fists held up at face height. Evade and duck are measured from where the head was at calibration
(stand still for a second at the start; the HUD shows HOLD STILL).

## Calibration screen

Webcam players get it before the first bout (`AKSGameMode::TickCalibrate`, drawn by `AKSHUD::DrawCalibrate`):

| Step | The player does | It ends when |
|---|---|---|
| 1 Framing | Stands so the mirror shows shoulders above the upper line and elbows above the lower one | All four checks hold for 2 s |
| 2 Guard | Fists up, still | The detector's calibration completes (at least 2.5 s) |
| 3, 4 Jabs | Three left, three right | Three punches from that arm are recognised |
| 5 Block | Both fists to the face, three times | Each held 0.6 s, lowered in between |
| 6, 7 Dodge | Leans left three times, then right three times | Each recognised for 0.2 s, back to the middle in between |
| 8 Duck | Bends the knees three times | Each recognised for 0.2 s |
| 9 Ready | | 2 s |

Every move step starts with 2.2 s to read the instruction, during which nothing counts, shows a large `n / 3`
counter, and ends with GOOD! for 0.9 s.

After the jabs the detector's sensitivity is set from the median peak speed of the strokes just thrown
(`3.5 / (0.55 × median)`, limited to 0.8–1.5). SPACE skips a step, K skips the lot. Every step writes a
`# MARK calibration step N <name>` line into the recording, so a calibration run is also a labelled test session.

## How a punch is decided

1. Each arm has an activity value: wrist speed across the picture, plus fist coming nearer, growing and the elbow
   opening. A stroke starts above `StrokeStart`.
2. From 60 ms into the stroke, the detector scores it as straight, hook or uppercut every frame. If one kind
   scores at least `EarlyScore` and leads by `EarlyLead`, the punch is called at once ("early").
3. Otherwise it is judged when the arm slows to half its peak speed, or after 450 ms ("at end").
4. Rejections: too slow, tracking jump, hand hidden, fist below the picture, body moved, both hands moved, unclear.

Every decision is logged, for example:

```
29513236 arm 1 PUNCH straight early after 332 ms, peak 7.4 (straight 0.57 hook 0.00 uppercut 0.00)
29513036 arm 0 stroke 132 ms peak 2.7: rejected, too slow
29514000 stance 1 ON
```

`arm 0` is the player's left. Stances: 1 block, 2 evade left, 3 evade right, 4 duck.

## Outside a fight: swipes and the left-right jab

On the film, the menu and the select screen nothing a person does by accident is a button press (player,
2026-11-13: "the slight movement of mine interrupts the cinematic, moves the menu or picks a character").

| The player does | It means | Where |
|---|---|---|
| Sweeps a hand down / up (a shoulder's width in a quarter second) | next / previous line | menu |
| Sweeps a hand to his right / left | next / previous fighter | select |
| Jabs with one hand, then the other (0.2 to 1.2 s apart, the first out of 0.8 s of stillness) | choose, continue, skip the film | everywhere but a bout |

`KSGesture.cpp` reads the swipes from the wrists in the camera picture; the way back with the same hand within 0.9 s
does not count, nor anything for 0.45 s after a swipe. `AKSGameMode::NoteWebPunch` pairs the jabs. Outside a bout the
detector is also run at 0.7 of the player's sensitivity (a bar of 7 and more), so the jabs have to be meant.

Measured on the player's session of 22:17 (first 30 s, in front of the film and the menu): the old rule took 12 punches
as button presses; the new one sees no left-right jab at all, and the swipe rule (tried in Python on the same
recording) fires once. **Nobody has used them yet**: whether a deliberate swipe is always seen is untested.

Phase changes are now written into the recording (`# MARK phase N ...`), so the next session can be cut into menu and
bout.

## Recordings

Every webcam session is saved to `unreal/Saved/KSTrack/rec_<date>_<time>.csv`. Numbers only, no pictures:

```
time_ms,seen, then for each of 33 joints: image_x,image_y,visibility,world_x,world_y,world_z
# <a detector decision, as logged>
```

Image positions are 0..1 from the top left; world positions are metres from between the hips.

Replay one through the detector (fast, no camera):

```
UnrealEditor KoSekai.uproject -game -windowed -ResX=1280 -ResY=720 -KSStart=fight -KSReplay=<file> -KSReplayExit
```

and read the `REPLAY` lines in the log.

## Adding a recorded test

1. Ask the player for a scripted session: say exactly what to do and in what order (for example five left jabs,
   five right jabs, three blocks), with a pause between moves.
2. Copy the recording to `tests/tracking/<name>.csv`.
3. Write `tests/tracking/<name>.expect`, one expectation per line: `<what> <min> <max>`, where `<what>` is
   `punches`, `straight`, `hook`, `uppercut`, `left`, `right` or `block`. Leave room for one or two errors, for
   example `left 4 6` for five left jabs.
4. Run `tools/check.sh`. Tune `KSDetector.cpp` until it passes, without breaking the other recordings.

Recordings are git-ignored: they are measurements of a person's body.

## Tuning knobs (`KSDetector.cpp`, top of file)

| Knob | Now | Effect of raising it |
|---|---|---|
| `StrokeStart` | 2.0 | Fewer strokes start; slower punches are ignored. |
| `StrokePeak` | 5.0 (was 3.5) | A punch must be faster to count. Fewer phantom punches, more missed ones. |
| `CrossArmMs` | 120 | One arm cannot punch within this long of the other (the body shifting moves both wrists). At 200 one punch in seven was thrown out. |
| `AutoShare`, `AutoFloor` | 0.6, 0.8 | The bar follows the player: 60% of the median of his last 15 punches, at most 1/0.8 above `StrokePeak` (6.25). At 0.7 nearly half the recorded punches were gone. |
| `StrokeEnd` | 0.5 | End-of-stroke verdict comes sooner. |
| `EarlyScore`, `EarlyLead` | 0.38, 0.12 | Early calls need more evidence: later but safer. |
| `MinScore` | 0.55 | End-of-stroke calls need more evidence. |
| `TwoHand` | 0.6 | Both-hands moves are rejected less readily. |
| `HoldMs` | 60 | Block must be held longer before it counts. |
