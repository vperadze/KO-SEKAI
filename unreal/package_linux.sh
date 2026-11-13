#!/usr/bin/env bash
# Packages a standalone Linux build of KO-SEKAI into ./Packaged/Linux
set -euo pipefail
# The effect packs from Fab are not in the repository; a package made without them has no particle effects.
"$(cd "$(dirname "$0")/.." && pwd)/tools/packs.sh" || { echo "Add the missing packs first (see above), or the package ships without effects."; exit 1; }
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine}"
HERE="$(cd "$(dirname "$0")" && pwd)"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$HERE/KoSekai.uproject" -platform=Linux -clientconfig=Development \
  -build -cook -stage -pak -package -archive -archivedirectory="$HERE/Packaged" \
  -unattended -utf8output -nop4
