# Particle effects

The special abilities are drawn with Niagara effects from free Fab packs (added 2026-11-13, replacing glowing engine
shapes). `KSVfx.h/.cpp` plays them; which effect belongs to which special is in `KSGameMode.cpp` (`namespace Vx`,
`SpecialLaunch`, `SpecialLand`, the charge-up in `TickSpecial`).

## The packs (sourced assets: list them in the jam submission)

| Folder under `unreal/Content` | Pack on Fab | Publisher | Used |
|---|---|---|---|
| `MixedVFX` | Mixed VFX | G.G. | flames (`NS_BigFire`), the flying ball, bats, lightning forks, green splash |
| `FreeStylizedLaserBeamVFX` | Stylized Laser Beam VFX – Free | QP Phantom | beams, sparkle, rings, crackle, charge-up rings |
| `PotaVFX_Smoke` | Stylized Smoke VFX | PotaVFX | hairspray puffs, dust ring |
| `Vefects/VFX_Distortion` | Distortion VFX | Vefects | the ring of bent air where a special lands |
| `Vefects/Zap_VFX`, `Vefects/Easy_Impact_Frames` | Zap VFX, Easy Impact Frames | Vefects | nothing (see below) |
| `NiagaraExamples` | Niagara Examples Pack | Epic Games | nothing yet |

All were free on Fab on 2026-11-13 and came in through the editor's Fab window (it works on Linux). Keep the Fab
library entries: the jam may ask how each asset was obtained. Several listings say "usage with AI: no": do not feed
them to Meshy or Dreamina.

The folders are git-ignored (public repository, Fab's licence) and large (1.7 GB). `tools/packs.sh` says which are
missing and where to get them; `CLAUDE.md` tells whoever builds to run it first.

Only what the code names is cooked. The effects are loaded by path, which the cook cannot see, so each one is listed
in `DefaultGame.ini` (`KSVfx` under `AssetManagerSettings`; `tools/check.sh` compares that list with `namespace Vx`).
The packs' showroom levels stay out (`DirectoriesToNeverCook`, and `MapsToCook` naming the only level). In the
Linux package the 15 effects and what they need come to about 43 MB before compression, 11 MB in the `.ucas`.

## Rules learnt

- **Nothing covers the picture, and nothing stays longer than two seconds** (player, 2026-11-13). The first version
  stacked a dozen effects at double size with their lights tinted: the whole screen went white. Now each special is a
  few effects at their own size, an effect's lights are left alone, and `KSVfx` switches every effect off after
  1.1 s (a loop after its `Life`) and removes it 0.8 s later.
- **Two effects compiling at once crashed the engine.** Run from the editor's binaries, an effect is compiled when it
  is first loaded. A special that started two new effects in one frame hit an assertion in Niagara's type registry
  (`NiagaraTypeRegistry.cpp:338`). `FKSVfx::Load` waits for each effect to finish, and the game loads every effect it
  uses at start-up (`Vx::All`). A new effect must be added to that list.
- **The pack's explosions draw black smoke here** (`NS_Explosion*` of Mixed VFX): a black blot over the fighters.
  Flames come from `NS_BigFire`, given half a second.
- **`NS_FireSlash` (Mixed VFX) was compiling when the engine crashed** and logs "Failed to convert default value" for
  an enum of an older engine. Not used.
- **Easy Impact Frames and Zap VFX are not used.** The zaps showed as a black patch on the floor in the tour's stills.
  The impact frame was dropped together with everything else that covered the whole picture; it was not looked at
  on its own (a cyan bar blamed on it turned out to be a ring rope, see LESSONS).
- **An effect on every punch must be tiny and gone in a third of a second** (`HitSpark`, `GuardRing`: they replaced
  the glowing ball that marked a hit, at the player's request). Punches come three a second: a burst of dots with
  its puff of smoke hung a beige cloud over the opponent's face for a whole combo, and the ring of bent air (a glassy
  dome a metre across whatever its scale) sat on the blocker like a bubble. Then thin rings at 0.3-0.4 of their
  size for 0.3 s, a few sparks more on a counter; and at the player's word half of that again (`Size *= 0.5` in
  `HitSpark`): a small flash at the glove.
- **A colour handed to an effect is multiplied by 2** (`Vx::Glow`); at 6 everything burnt to white.

## Tools

- `art/tools/vfx_loadtest.py` (headless): loads every effect of the packs, lists failures and missing dependencies in
  `unreal/Saved/vfx_loadtest.txt`. 179 of 179 loaded in 5.8.
- `-KSVfxTour`: plays 123 effects one by one between the fighters and photographs each twice
  (`Saved/Screenshots/vfx_NNN_a.png`, `_b.png`), logging each one's own settings by name and type. This is how the
  effects were chosen; the list is `FKSVfx::TourList`.
- `-KSSpecials`: the player's special two seconds into the bout, the opponent's three seconds later, then quit. With
  `-KSHitShots` it leaves five stills of each (`special_NN_hero.png`, `hit_NN_special_0..3.png`).
