#!/usr/bin/env bash
#
# Screenshot sweep, start to finish, against a sweep build - and put the
# tree back to a shipping one afterwards.
#
#     tools/sweep.sh                 # days, dates, times
#     tools/sweep.sh stacks          # any tools/date_sweep.sh arguments
#     tools/sweep.sh all             # everything, ~151 shots
#     tools/sweep.sh --restore       # only fix up an interrupted run
#
# Does, in order: set WRITE_SPEED 0, sync, build, install to the emulator,
# sweep, set WRITE_SPEED back, sync, build, install. You end where you
# started, with shots/ full and a shipping build on the emulator.
#
#
# WHY A SWEEP NEEDS ITS OWN BUILD
# -------------------------------
# The short version, in full in UPGRADING.md 11:
#
#   emu-set-time does not redraw the watchface. The firmware only rebuilds
#   the face when its own clock crosses a minute, so date_sweep.sh sets the
#   clock a few seconds early and lets the rollover land on the wanted
#   minute.
#
#   And any pebble command re-syncs the emulated clock to the host's real
#   time. So exactly ONE command may run between setting the clock and
#   taking the shot, and it has to be the screenshot itself.
#
# One screenshot means there is no way to wait for the reveal to finish, and
# no way to check afterwards whether it had. WRITE_SPEED 0 draws each face
# complete in a single frame, which removes the question instead of
# answering it. It changes how the final frame is REACHED, not what it
# holds, and the final frame is the whole point of a sweep.
#
#
# WHY THIS IS A SCRIPT AND NOT AN INSTRUCTION
# -------------------------------------------
# Because it is two states of the tree, and every serious bug in this
# project so far has come from two states of the tree disagreeing: geometry
# generated on one machine and images on another, font images restored from
# git after tune.py wrote them. A sweep build left in place and shipped
# would be the same shape of mistake, and a quiet one - the watchface would
# simply stop animating, and look like it had always been that way.
#
# So the restore is a trap on EXIT rather than the last line. Ctrl-C, a
# failed build, a failed sweep: the source goes back either way.
#
# What the trap CANNOT undo is what is already installed on a device. If the
# rebuild at the end fails, the emulator is still holding the sweep build,
# and the script says so rather than leaving it to be discovered.

set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG=src/c/config.h
# Same default as tools/sync.sh, and overridable the same way. That file is
# gitignored for having machine-specific paths in it; this one takes the
# path from the environment so it can stay committed.
BUILD_DIR="${BUILD_DIR:-/home/chrono/handwritten-british}"
EMU="${PEBBLE_EMULATOR:-emery}"

speed() { sed -n 's/^#define WRITE_SPEED \([0-9]\+\).*/\1/p' "$CONFIG"; }

set_speed() {
  sed -i "s/^#define WRITE_SPEED [0-9]\+/#define WRITE_SPEED $1/" "$CONFIG"
  if [ "$(speed)" != "$1" ]; then
    echo "could not set WRITE_SPEED in $CONFIG - is the #define still there?" >&2
    exit 1
  fi
}

deploy() {   # git folder -> build folder -> emulator
  bash tools/sync.sh
  ( cd "$BUILD_DIR" && pebble build && pebble install --emulator "$EMU" )
}

# The value to go back to. Read from the tree rather than hardcoded, so a
# tuned WRITE_SPEED is not quietly reset to someone else's number. Only
# falls back if the tree is ALREADY at 0, which means a previous run was
# interrupted and the original is gone.
SHIP_SPEED="$(speed)"
if [ -z "$SHIP_SPEED" ]; then
  echo "no '#define WRITE_SPEED' in $CONFIG" >&2
  exit 1
fi
if [ "$SHIP_SPEED" = "0" ]; then
  SHIP_SPEED="${SHIP_SPEED_DEFAULT:-260}"
  echo "note: $CONFIG is already at WRITE_SPEED 0, so a previous run did not"
  echo "      finish. Restoring to $SHIP_SPEED at the end; override with"
  echo "      SHIP_SPEED_DEFAULT if that is not the number you had."
  echo
fi

restored=0
restore_ok=1
restore() {
  [ "$restored" = 1 ] && return
  restored=1
  if [ "$(speed)" = "$SHIP_SPEED" ]; then
    return
  fi
  echo
  echo "putting WRITE_SPEED back to $SHIP_SPEED and rebuilding..."
  set_speed "$SHIP_SPEED"
  if ! deploy; then
    # Recorded, not just printed. The first version of this said its piece
    # here and then fell through to "done, the emulator is back on a
    # shipping build" four lines later - the two contradicting each other,
    # with the reassuring one last.
    restore_ok=0
    echo >&2
    echo "THE SOURCE IS RESTORED but the rebuild failed, so the emulator is" >&2
    echo "still running the sweep build - the one that does not animate." >&2
    echo "Fix whatever broke, then:  tools/sweep.sh --restore" >&2
  fi
}
trap restore EXIT
# Without this, Ctrl-C during the sweep still restored the tree - the EXIT
# trap saw to that - but then fell through and printed "Done", which is a
# lie about what just happened. Exit explicitly so the EXIT trap runs and
# the status says interrupted.
trap 'echo >&2; echo "interrupted" >&2; exit 130' INT TERM

if [ "${1:-}" = "--restore" ]; then
  if [ "$(speed)" = "$SHIP_SPEED" ]; then
    echo "nothing to restore: $CONFIG is at WRITE_SPEED $SHIP_SPEED."
    echo "Rebuilding anyway, in case the emulator is out of step."
  fi
  set_speed 0            # so restore() has something to do
  restored=0
  restore
  trap - EXIT
  exit 0
fi

echo "=== sweep build: WRITE_SPEED 0 ==============================="
set_speed 0
deploy

echo
echo "=== sweeping ================================================="
# Not fatal: date_sweep.sh exits non-zero when check_resources fails, and
# the tree still has to be put back either way. The trap handles that, but
# the exit status is worth reporting rather than swallowing.
sweep_rc=0
tools/date_sweep.sh "$@" || sweep_rc=$?

echo
echo "=== shipping build: WRITE_SPEED $SHIP_SPEED ==================="
restore
trap - EXIT

echo
if [ "$restore_ok" != 1 ]; then
  echo "Sweep finished, but see the warning above: the emulator is still" >&2
  echo "running the sweep build." >&2
  exit 1
fi
if [ "$sweep_rc" = 0 ]; then
  echo "Done. shots/ is up to date and the emulator is back on a shipping"
  echo "build. Read the UNCHANGED lines above before trusting the set."
else
  echo "The sweep exited $sweep_rc - see above. The tree and the emulator" >&2
  echo "have been put back to WRITE_SPEED $SHIP_SPEED regardless." >&2
fi
exit "$sweep_rc"
