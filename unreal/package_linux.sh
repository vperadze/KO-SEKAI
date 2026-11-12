#!/usr/bin/env bash
# Packages a standalone Linux build of KO-SEKAI into ./Packaged/Linux
set -euo pipefail
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine}"
HERE="$(cd "$(dirname "$0")" && pwd)"
"$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
  -project="$HERE/KoSekai.uproject" -platform=Linux -clientconfig=Development \
  -build -cook -stage -pak -package -archive -archivedirectory="$HERE/Packaged" \
  -unattended -utf8output -nop4
