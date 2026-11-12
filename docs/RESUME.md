# Where things stand

Updated 2026-11-13. Deadline 2026-11-16 02:59 CET.

## Works (checked by `tools/check.sh` or seen in a self-playing fight)

- Seven fighters, random player fighter, ladder of three random opponents.
- Punches connect: aimed clips, recoil, hit-stop, flash, sound, camera shake.
- Smooth transitions between clips, with momentum carried over.
- Arena: Dreamina ring mat and crowd-stand wall; eight old-arcade sprite spectators who cheer and wince.
- Built-in webcam tracker: camera opens, body is found, 30 frames/s when the machine is otherwise idle.
- Session recording and offline replay of the move detector.

## Built but not verified with a person

- Early punch call (punch starts while the arm is still extending).
- Absolute block rule (both fists at face height).
- STEP BACK warning and the fist-below-picture rejection.
- **Calibration screen** before the first bout: framing mirror and checklist, guard, three jabs each side (sets the
  detector's sensitivity from the player's own punch speed), block, lean left, lean right, duck. Steps through in
  demo mode and with a live camera; a person completing each step by doing the move is unverified.
- Retry after a loss (goes through the same fresh-opponent path as the next bout).

## Next

1. **Scripted recording from the player**, standing far enough back: 3 s still, 5 left jabs, 5 right jabs,
   3 hooks and 3 uppercuts each side, 3 blocks of 2 s. Turn it into `tests/tracking/` cases and tune from it.
2. Commit and push (nothing since the `real-fighters` push at `ddc971e` is committed).
3. Windows package: build, size under 1 GB, test the webcam there (the OpenCV camera backend differs on Windows).
4. The 30–60 s gameplay video.

## Known gaps

- No duck or get-up clips.
- Fab assets: none in the project; Fab cannot be driven from here.
- `Content/KS/Tracker/pose_lite.uasset` (5 MB) is an accidental editor import the game does not use.
- Spectators are flat sprites facing the far-side camera; they would look thin from the side.
