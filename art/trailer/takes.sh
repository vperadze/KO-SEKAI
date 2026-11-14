#!/bin/bash
# Every take the trailer and the jam page's stills are cut from (about 13 minutes; the game plays itself).
# Leave the game's window uncovered while it runs. art/trailer/takes.sh [name ...] records only those.
cd "$(dirname "$0")"
take() { local n="$1"; shift; if [ -z "$ONLY" ] || [[ " $ONLY " == *" $n "* ]]; then ./record.sh "$n" "$@"; sleep 3; fi; }
ONLY="$*"
B="-KSStart=fight -KSAuto"
take bout_volcano  88 $B -KSArena=9 -KSPlayer=0 -KSFighter=4
take bout_space    88 $B -KSArena=1 -KSPlayer=5 -KSFighter=2
take bout_castle   88 $B -KSArena=6 -KSPlayer=3 -KSFighter=6
take bout_beach    88 $B -KSArena=2 -KSPlayer=4 -KSFighter=1 -KSSpam
take lose_hall     50 $B -KSArena=0 -KSPlayer=2 -KSFighter=0 -KSGiveUp
take spec_studio   30 $B -KSSpecials -KSArena=10 -KSPlayer=0 -KSFighter=5
take spec_rooftop  30 $B -KSSpecials -KSArena=4 -KSPlayer=2 -KSFighter=3
take spec_dojo     30 $B -KSSpecials -KSArena=5 -KSPlayer=4 -KSFighter=6
take spec_scrap    30 $B -KSSpecials -KSArena=8 -KSPlayer=1 -KSFighter=0
take select        75 -KSStart=select -KSSelectTour
take film_menu     45
