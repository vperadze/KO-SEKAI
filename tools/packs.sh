#!/usr/bin/env bash
# Are the Fab effect packs in the project? They are not in the repository (Fab's licence: not in a public one), so
# everyone who builds the game adds them from his own Fab library. Run this before building or packaging:
#
#   tools/packs.sh        lists the packs that are missing and how to add them; exit 1 if any is
#
# Without them the game still builds and runs, but the special abilities and the hit marks have no particle effects,
# and a package made without them ships without them. The effects the game uses are the ones listed for the cook in
# unreal/Config/DefaultGame.ini (KSVfx); docs/VFX.md says what each is used for.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONTENT="$ROOT/unreal/Content"
# folder under unreal/Content | name on Fab | publisher | listing
PACKS="MixedVFX|Mixed VFX|G.G.|https://www.fab.com/listings/6518971c-c8ca-47d7-8611-1ff068b78dd5
FreeStylizedLaserBeamVFX|Stylized Laser Beam VFX - Free|QP Phantom|https://www.fab.com/listings/e63c140a-4ef9-448a-8e58-133ae1487534
PotaVFX_Smoke|Stylized Smoke VFX|PotaVFX|https://www.fab.com/listings/172235df-7a05-4d60-ac7b-23472e530ee7
Vefects/VFX_Distortion|Distortion VFX|Vefects|https://www.fab.com/listings/4f001dd6-88d3-4189-9dde-150f6c7f71c9"
missing=0
while IFS='|' read -r folder name publisher url; do
	# Every effect of this pack that the game names must be there, not just the folder.
	lost=""
	for asset in $(grep 'PrimaryAssetType="KSVfx"' "$ROOT/unreal/Config/DefaultGame.ini" | grep -o '"/Game/[^"]*"' | tr -d '"' | grep "^/Game/$folder/" | sed 's/\.[^./]*$//'); do
		[ -f "$CONTENT/${asset#/Game/}.uasset" ] || lost="$lost ${asset##*/}"
	done
	if [ -d "$CONTENT/$folder" ] && [ -z "$lost" ]; then echo "  ok       $name ($publisher)"
	else
		missing=$((missing + 1))
		echo "  MISSING  $name, by $publisher (free)   $url"
		[ -n "$lost" ] && [ -d "$CONTENT/$folder" ] && echo "           the folder is there but these effects are not:$lost"
	fi
done <<< "$PACKS"
if [ "$missing" -gt 0 ]; then
	echo
	echo "$missing effect pack(s) missing. To add one: open unreal/KoSekai.uproject in the Unreal editor, Window > Fab, sign in,"
	echo "find the pack (or add it to your library from the link first), press Add to Project, and accept the newest"
	echo "version offered even if it is older than the engine. It lands in unreal/Content/<folder>, which git ignores."
	exit 1
fi
echo "All effect packs are in the project."
