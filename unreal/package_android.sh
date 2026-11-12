#!/usr/bin/env bash
# Packages a Development Android build (the format the jam accepts) into ./Packaged/Android_ASTC
set -euo pipefail
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android}"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine}"
export NDKROOT=$ANDROID_HOME/ndk/28.2.13676358 NDK_ROOT=$ANDROID_HOME/ndk/28.2.13676358
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
HERE="$(cd "$(dirname "$0")" && pwd)"
"$UE_ROOT"/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
  -project="$HERE/KoSekai.uproject" -platform=Android -cookflavor=ASTC \
  -clientconfig=Development -build -cook -stage -pak -package -archive \
  -archivedirectory="$HERE/Packaged" -unattended -utf8output -nop4
