#!/usr/bin/env bash
#
# Screenshot the watchface across every shape it can draw.
#
#     tools/date_sweep.sh              # days, dates, then times
#     tools/date_sweep.sh days         # every day number, 1st to 31st
#     tools/date_sweep.sh dates        # one date per month
#     tools/date_sweep.sh times        # just the phrase
#     tools/date_sweep.sh stacks       # every word that can sit ABOVE past/to
#     tools/date_sweep.sh hours        # every word that can sit BELOW past/to
#     tools/date_sweep.sh all          # all five
#     tools/date_sweep.sh 2026-02-28 2026-02-29     # named dates
#     tools/date_sweep.sh 14:35 09:15               # named times
#
# Writes shots/. Look at them side by side afterwards rather than judging
# each as it appears - the point is consistency across the set, and that is
# invisible one screenshot at a time.
#
# (The name is now a slight misnomer; it sweeps times too. Renaming needs a
# git mv, which is a job for whoever touches this next.)
#
#
# WHY IT SETS THE CLOCK EARLY
# ---------------------------
# emu-set-time DOES NOT REDRAW THE WATCHFACE. It moves the emulator's clock
# and nothing else. The face is rebuilt by tick_handler, which the firmware
# calls when its OWN clock crosses a minute boundary, and a clock that has
# been shoved sideways has not crossed anything.
#
# So the face only ever changed once a real minute, showing whatever the RTC
# happened to hold at that moment. At roughly six seconds a shot that is one
# picture for every nine files. Three full sweeps - 151 shots - were taken
# and believed before anyone counted: they contained 19 distinct images, and
# not one was guaranteed to be the time named on it. The duplicates were the
# obvious damage; the shots that DID differ were no more trustworthy.
#
# The fix is to stop fighting the tick and use it. To show 09:15, set the
# clock to 09:14:57 - LEAD seconds early - and wait. The emulator's own
# clock runs on, crosses into 09:15:00 by itself, fires the tick, and the
# face is rebuilt at exactly the wanted time. No relaunch, no buttons, no
# change to the watchface, and the normal incremental reveal is exercised
# rather than a forced full redraw.
#
# LEAD has to be longer than emu-set-time takes to land. Too short and the
# clock is already past the boundary when it arrives, the rollover never
# happens, and the shot is a duplicate - which is caught rather than kept,
# see below.
#
#
# WHY EXACTLY ONE SCREENSHOT, AND WHY THE SWEEP NEEDS ITS OWN BUILD
# -----------------------------------------------------------------
# ANY pebble command resets the emulated clock to the host's real time.
#
# That was found by leaving the clock alone for 70 seconds - it held, and
# ticked on to the next minute correctly - and then taking two screenshots a
# minute apart: the first showed the time that had been set, the second had
# snapped back to the real date and time. The emulator runs pypkjs alongside
# QEMU as a stand-in phone, and a connecting client re-syncs the watch to
# the host clock.
#
# So between setting the clock and taking the shot, exactly one command may
# run, and it has to be the screenshot. No polling. No second frame. No
# waiting for the reveal to settle and no checking afterwards whether it
# had - both of those need a second screenshot, and the second screenshot is
# what destroys the thing being photographed.
#
# An earlier version of this script polled for a still frame, which is why
# it produced a sweep of real-time screenshots: every extra frame shoved the
# clock back, and "two identical frames" then settled quite happily on the
# real time, with the differs-from-last check waving it through because real
# time keeps advancing.
#
# One screenshot is only safe if the face is already complete when it is
# taken, so sweeps are shot with WRITE_SPEED 0 in config.h - the face drawn
# whole, no reveal. See the comment there. That changes only how the final
# frame is REACHED, not what it holds, and the final frame is the entire
# point of a sweep.
#
# Three other repaint routes were tried and are dead ends, recorded so they
# are not tried again:
#
#   - waiting longer. A fixed 3.5s. The face is not slow, it is not being
#     redrawn at all.
#   - polling until the picture stops moving. Worse than useless here: it is
#     the polling that resets the clock.
#   - emu-button click select / back, to leave the watchface and come back.
#     Four of five test shots came back byte-identical. The window is
#     re-shown and the layer redrawn, but update_proc draws s_face, which
#     was built at the last tick and is stale. Only window_load rebuilds it,
#     and that runs on app launch, which this is not.
#
#
# WHY IT COMPARES EACH SHOT TO THE ONE BEFORE
# -------------------------------------------
# An identical shot is the failure above, and for three sweeps - 151 files,
# 19 distinct images - it was completely silent. The check costs nothing: it
# is an md5 of a file already on disk, with no connection to the emulator,
# so it always runs. A duplicate re-arms the clock and tries again, up to
# RETRIES, and is named if it still will not move.
#
# It is not proof of a bug - in the rounded modes two different times
# genuinely draw the same face - so it reports rather than fails. What it
# rules out is finding this by eye, three sweeps later.
#
# The first shot has nothing before it to compare against, so the comparison
# is primed from the screen as it is before the clock is touched.
#
#
# WHY THESE DATES
# ---------------
# run.sh already sweeps all 366 dates in both formats and reports the widest
# line, which leaves 59px of screen unused. There is no FITTING question
# left; what a person still has to judge is whether the handwriting reads
# well, and for that every distinct shape needs to appear once:
#
#   every month           12 words, each its own bitmap
#   every weekday          7 words, date format 1 only
#   every ordinal form     st / nd / rd / th, and the 11th-13th exception
#   every digit            0-9
#
# Twelve dates in 2026 cover all four at once. 22 August is first because it
# is the widest line either format produces.
#
#
# WHY ALL THIRTY-ONE DAYS
# -----------------------
# Each digit is its own bitmap, so a two-digit day sets two of them side by
# side and the pairing is a thing in its own right - a 2 whose tail runs
# under the next character reads differently before a 1 than before a 7.
# There is no clever subset here: days 10 to 31 ARE the twenty-two possible
# pairs, and 1 to 9 are the single-digit cases. Thirty-one shots covers
# every one exactly once.
#
# All in one month, so the only thing changing is the number. August,
# because it has 31 days and is the widest month word, so each pairing is
# seen in the tightest line it will ever appear in. DAY_MONTH overrides it.
#
# The YEAR is not swept: it is four fixed digits that change once a year.
# 2026 shows the 20, 02 and 26 pairings; if a later year looks wrong, name
# a date in it directly.
#
#
# WHY THESE TIMES
# ---------------
# One per distinct LAYOUT, not per distinct wording - stacked vs centred,
# split vs whole, with and without the annotation, both sides of the hour.
# The date is pinned to 22 August throughout so the date line is at its
# widest while the phrase above it varies: one sweep stresses both.
#
# The three hedge entries only show a hedge when the reading mode is
# "nearest five, spoken". Run the whole thing once per reading mode - the
# phrase shapes differ between them, which is the point of having three.
#
#
# WHY EVERY MINUTE (stacks)
# -------------------------
# "times" above picks one shot per distinct LAYOUT, which is the right unit
# for asking does it fit. It is the wrong unit for asking does it look
# right, because the thing that looks wrong is a particular DESCENDER
# landing on a particular ASCENDER, and that is a property of the two words,
# not of the layout they are in. "past" under "quarter" and "past" under
# "twenty" are the same layout and a completely different picture.
#
# "past" and "to" are the hard ones. Every phrase has one, they are the only
# words with a row above AND a row below, and their ink is nearly all
# x-height with one deep descender ("p") and one tall ascender ("t") - so
# they are what everything else has to be balanced against.
#
# The word directly above "past"/"to" is a function of the minute alone:
#
#   split minutes (21-29)   one..nine, as the second half
#   with an annotation      minute / minutes
#   without one             one..nineteen, quarter, twenty, twenty-five, half
#
# Which of those three you get depends on the reading mode and the "minutes"
# setting, so no fixed list of times covers them all. Sweeping every minute
# does, whatever the settings are: minute m puts its word above "past" for
# m <= 30, and minute 60-m puts the same word above "to". So 1..59 is the
# complete set of both, with nothing assumed about the settings.
#
# Shots are named by the SPOKEN minute rather than the clock minute, so each
# word's two shots sit next to each other in the folder - stack-08-past
# beside stack-08-to - because past-versus-to is exactly the comparison
# being made.
#
# Run this one in EXACT mode. The rounded modes collapse most of the 59 onto
# about a dozen phrases, so you get the same picture a dozen times; EXACT is
# also the only mode where 21-29 split, which is a third of the set.
#
#
# WHY EVERY HOUR (hours)
# ----------------------
# The mirror of the above: the word BELOW "past"/"to" is always the hour
# word, and there are fourteen of them. They are not symmetric, which is
# easy to forget - "twelve" only ever appears under "to", "noon" only under
# "past", and "midnight" under both. So both sides are swept in full.
#
# The minute is pinned at :15 and :45, which are multiples of five and so
# survive every reading mode unchanged and never take a hedge. That makes
# this the one sweep that looks the same in all three modes, and the phrase
# above the relation word holds still while the word below it changes.
#
#
# TIMEZONES
# ---------
# The watchface calls localtime(), which is correct and not negotiable: a
# watch shows the time where its wearer is. So the emulator displays LOCAL
# time, and this script has to speak local time back to it.
#
# pebble emu-set-time takes "Unix UTC seconds" and ignores --utc for that
# form. An epoch built as UTC therefore arrives on screen shifted by the
# emulator's offset - which is exactly the bug this script shipped with. It
# asked for 00:00, the watch displayed 07:00, and every shot in the time
# sweep was seven hours off the name on its own file.
#
# TZ_OFFSET is the offset the EMULATOR displays, in hours east of UTC. It
# defaults to this machine's, which is right when the emulator inherits the
# host zone - but that is an assumption and not a documented one, so the
# script prints what it assumed and says how to correct it.
#
# Midday stays as the DATE sweep's time of day, now for a smaller reason: if
# TZ_OFFSET is wrong the times will be off, but the DATE on screen still
# holds for any error up to 11 hours. A month sweep that lands on the wrong
# month is a worse failure than one that lands on the wrong hour.

set -euo pipefail
cd "$(dirname "$0")/.."

EMU="${PEBBLE_EMULATOR:-emery}"
OUT="shots"
LEAD="${LEAD:-3}"                # whole seconds to set the clock EARLY by
AFTER="${AFTER:-0.6}"            # seconds from the rollover to the shot
RETRIES="${RETRIES:-3}"          # attempts at a shot that did not change
WAIT="${WAIT:-$(python3 -c "print(float('$LEAD') + float('$AFTER'))")}"
# Hours east of UTC that the EMULATOR displays. Defaults to this machine's.
TZ_OFFSET="${TZ_OFFSET:-$(python3 -c "import datetime; print(datetime.datetime.now().astimezone().utcoffset().total_seconds()/3600)")}"
DATE_HOUR="${SWEEP_HOUR:-12}"    # local. See TIMEZONES.
DATE_MIN="${SWEEP_MINUTE:-28}"
TIME_ON="${SWEEP_DATE:-2026-08-22}"   # the widest date line
DAY_MONTH="${DAY_MONTH:-2026-08}"     # 31 days, and the widest month word

DATES=(2026-08-22 2026-01-13 2026-02-04 2026-03-01 2026-04-03 2026-05-07
       2026-06-08 2026-07-05 2026-09-06 2026-10-09 2026-11-10 2026-12-01)

# time|tag  - the tag names the LAYOUT, which is what is being looked at
TIMES=(
  "00:00|solo-midnight"        "12:00|solo-midday"
  "03:00|witching-hour"        "07:00|oclock"
  "11:00|oclock-longest"       "01:01|minute-singular-past"
  "06:07|minutes-own-line"     "09:15|quarter-past"
  "10:20|twenty-past"          "05:21|split-with-minutes"
  "08:25|twentyfive-past"      "04:28|split-widest"
  "02:30|half-past"            "07:31|split-to-side"
  "11:35|twentyfive-to"        "06:45|quarter-to"
  "09:50|ten-to"               "10:59|minute-singular-to"
  "23:58|rollover-to-midnight" "11:58|rollover-to-midday"
  "00:04|hedge-nearly"         "00:06|hedge-just-gone"
  "12:59|hedge-on-the-hour"
)

# The hour the minute sweep sits on. "four" past and "five" to: both short,
# both free of ascenders and descenders, so the row below the relation word
# stays out of the way while the row above it is what changes.
STACK_HOUR="${STACK_HOUR:-4}"

# The minute the hour sweep sits on, per side. Multiples of five, so the
# phrase above is "quarter" in every reading mode and never takes a hedge.
HOUR_MIN_PAST="${HOUR_MIN_PAST:-15}"
HOUR_MIN_TO="${HOUR_MIN_TO:-45}"

# clock hour | sort index | the hour word it produces.
# past: ref is the hour itself. Note "noon", and that "twelve" cannot occur.
HOURS_PAST=(
  "0|00|midnight" "1|01|one"    "2|02|two"    "3|03|three"
  "4|04|four"     "5|05|five"   "6|06|six"    "7|07|seven"
  "8|08|eight"    "9|09|nine"   "10|10|ten"   "11|11|eleven"
  "12|12|noon"
)
# to: ref is the hour AFTER. Note "twelve", and that "noon" cannot occur.
HOURS_TO=(
  "23|00|midnight" "0|01|one"   "1|02|two"    "2|03|three"
  "3|04|four"      "4|05|five"  "5|06|six"    "6|07|seven"
  "7|08|eight"     "8|09|nine"  "9|10|ten"    "10|11|eleven"
  "11|12|twelve"
)

ordinal() {   # 1 -> st, 2 -> nd, 3 -> rd, 11..13 -> th
  case "$1" in
    11|12|13) echo th ;;
    *1) echo st ;;
    *2) echo nd ;;
    *3) echo rd ;;
    *)  echo th ;;
  esac
}

command -v pebble >/dev/null || { echo "pebble is not on PATH" >&2; exit 1; }

# Refuse to photograph a build that cannot be right. Screenshotting is the
# slow step and the judgement made from it is the whole point, so spending
# four minutes capturing smeared digits and then trusting them is the worst
# available outcome. This is exactly how "the sweep shows an old version of
# the handwriting" happened.
python3 tools/check_resources.py || {
  echo "" >&2
  echo "Not sweeping: fix the above, rebuild, and reinstall to the" >&2
  echo "emulator first - otherwise the screenshots show something that" >&2
  echo "was never a valid build." >&2
  exit 1
}

# Only one screenshot can be taken per shot (see WHY EXACTLY ONE SCREENSHOT),
# so there is no way to wait for the reveal to finish or to check afterwards
# whether it had. The face has to arrive complete.
#
# This reads the SOURCE tree, which is not necessarily what is installed on
# the emulator - so it is a warning and not a refusal. It is still worth
# having: forgetting the sweep build is a whole sweep of half-written faces,
# and the failure looks like bad handwriting rather than like a mistake.
if grep -qE '^#define WRITE_SPEED\s+[1-9]' src/c/config.h; then
  cat >&2 <<'WARN'
warning: src/c/config.h has WRITE_SPEED non-zero, so this tree builds WITH
         the reveal. Only one screenshot per shot is possible, so a face
         still being written is what gets captured.

         For a sweep: set WRITE_SPEED 0 in config.h, then sync, build and
         install to the emulator. Put it back before shipping - the reveal
         is the watchface.

         Ignore this if the build already on the emulator was made that way.

WARN
fi

mkdir -p "$OUT"

# pebble writes a progress meter to stderr for every screenshot, which buries
# the one line per shot worth reading. Hold each command's output and print
# it only if the command actually fails.
run_quiet() {
  local out
  if ! out=$("$@" 2>&1); then
    printf '\nFAILED: %s\n%s\n' "$*" "$out" >&2
    return 1
  fi
}

epoch() {   # date HH MM -> unix seconds for that LOCAL time on the emulator
  python3 - "$1" "$2" "$3" "$TZ_OFFSET" <<'EPOCH'
import sys, datetime
d = datetime.date.fromisoformat(sys.argv[1])
tz = datetime.timezone(datetime.timedelta(hours=float(sys.argv[4])))
print(int(datetime.datetime(d.year, d.month, d.day, int(sys.argv[2]),
                            int(sys.argv[3]), tzinfo=tz).timestamp()))
EPOCH
}

unchanged=0
prev_hash=""

# Shots taken with the date pinned, for the date-line check at the end. The
# days and dates sweeps change the date deliberately, so they do not go in.
pinned=()

# One frame, into $1. pebble screenshot will not write over a file that is
# already there, so the target is cleared rather than assumed absent -
# otherwise a second run compares two stale frames and calls them settled.
grab() {
  rm -f "$1"
  run_quiet pebble screenshot --no-open "$1"
}

shoot() {   # epoch, output name, label
  local out="$OUT/$2.png" hash attempt=0

  while :; do
    attempt=$((attempt + 1))

    # ONE pebble command, then ONE screenshot. Nothing may go between them.
    #
    # Aim the emulator's own clock at the target minute rather than at the
    # target: the tick fires when IT crosses the boundary, LEAD seconds from
    # now, and the face is rebuilt then. Then WAIT seconds of plain sleep -
    # no polling, no second frame, nothing that opens a connection.
    run_quiet pebble emu-set-time --emulator "$EMU" "$(( $1 - LEAD ))"
    sleep "$WAIT"
    grab "$out"

    # Did the face actually change? A shot identical to the one before it is
    # the failure this whole design exists to prevent, and for three sweeps
    # it was completely silent. Costs nothing - md5 of a file already on
    # disk, no connection to the emulator - so it is always checked.
    hash=$(md5sum < "$out" | cut -d' ' -f1)
    if [ "$hash" != "$prev_hash" ] || [ "$attempt" -ge "$RETRIES" ]; then
      break
    fi
    rm -f "$out"
  done

  if [ "$hash" = "$prev_hash" ]; then
    # Not always wrong: in the rounded modes two different times genuinely
    # draw the same face. Reported so it can be judged, not assumed either
    # way - an unreported duplicate is how three sweeps were believed.
    printf '  %-28s UNCHANGED from the shot before it, after %d tries\n' \
           "$3" "$attempt" >&2
    unchanged=$((unchanged + 1))
  fi
  prev_hash="$hash"
  [ "${PIN_DATE:-0}" = 1 ] && pinned+=("$out")

  # printf, not echo: a format string starting with "->" is read as an
  # option, which is how the first version of this line died.
  printf -- '  %-28s -> %s\n' "$3" "$out"
}

# --- what to do -------------------------------------------------------
want_days=0; want_dates=0; want_times=0; want_stacks=0; want_hours=0
named_d=(); named_t=()
if [ $# -eq 0 ]; then
  # stacks and hours are 85 more shots on top of these 66, and they are for
  # a different question - so they are opt-in rather than part of the daily
  # look. "all" runs the lot.
  want_days=1; want_dates=1; want_times=1
else
  for a in "$@"; do
    case "$a" in
      days)   want_days=1 ;;
      dates)  want_dates=1 ;;
      times)  want_times=1 ;;
      stacks) want_stacks=1 ;;
      hours)  want_hours=1 ;;
      all)    want_days=1; want_dates=1; want_times=1
              want_stacks=1; want_hours=1 ;;
      *:*)    named_t+=("$a|custom") ;;
      *)      named_d+=("$a") ;;
    esac
  done
  [ ${#named_d[@]} -gt 0 ] && { DATES=("${named_d[@]}"); want_dates=1; }
  [ ${#named_t[@]} -gt 0 ] && { TIMES=("${named_t[@]}"); want_times=1; }
fi

printf 'emulator %s, %ss lead, assuming it displays UTC%+g\n\n' \
       "$EMU" "$LEAD" "$TZ_OFFSET"

# The first shot has nothing to be compared against, so it is the one shot
# a stale face could slip through. Prime the comparison with whatever is on
# screen right now, before the clock is touched.
grab "$OUT/.priming.png"
prev_hash=$(md5sum < "$OUT/.priming.png" | cut -d' ' -f1)
rm -f "$OUT/.priming.png"

n=0

if [ "$want_days" = 1 ]; then
  PIN_DATE=0
  last=$(python3 -c "import calendar,sys; y,m=sys.argv[1].split('-'); print(calendar.monthrange(int(y),int(m))[1])" "$DAY_MONTH")
  printf 'every day number in %s (%d days)\n' "$DAY_MONTH" "$last"
  for day in $(seq 1 "$last"); do
    dd=$(printf '%02d' "$day")
    o=$(ordinal "$day")
    shoot "$(epoch "$DAY_MONTH-$dd" "$DATE_HOUR" "$DATE_MIN")" \
          "day-$dd-$o" "$day$o"
    n=$((n + 1))
  done
  echo
fi

if [ "$want_dates" = 1 ]; then
  PIN_DATE=0
  printf 'dates (%02d:%02d local on each)\n' "$DATE_HOUR" "$DATE_MIN"
  for d in "${DATES[@]}"; do
    shoot "$(epoch "$d" "$DATE_HOUR" "$DATE_MIN")" "date-$d" "$d"
    n=$((n + 1))
  done
  echo
fi

if [ "$want_times" = 1 ]; then
  PIN_DATE=1
  printf 'times (dated %s)\n' "$TIME_ON"
  for entry in "${TIMES[@]}"; do
    hhmm="${entry%%|*}"; tag="${entry##*|}"
    shoot "$(epoch "$TIME_ON" "${hhmm%%:*}" "${hhmm##*:}")" \
          "time-${hhmm/:/}-$tag" "$hhmm  $tag"
    n=$((n + 1))
  done
  echo
fi

if [ "$want_stacks" = 1 ]; then
  PIN_DATE=1
  printf 'every word that can sit above past/to (hour %02d, dated %s)\n' \
         "$STACK_HOUR" "$TIME_ON"
  for mins in $(seq 1 30); do
    shoot "$(epoch "$TIME_ON" "$STACK_HOUR" "$mins")" \
          "$(printf 'stack-%02d-past' "$mins")" \
          "$(printf ':%02d  %d past' "$mins" "$mins")"
    n=$((n + 1))
    # 30 has no mirror: :30 IS "half past", and 60-30 is itself.
    if [ "$mins" -le 29 ]; then
      m=$((60 - mins))
      shoot "$(epoch "$TIME_ON" "$STACK_HOUR" "$m")" \
            "$(printf 'stack-%02d-to' "$mins")" \
            "$(printf ':%02d  %d to' "$m" "$mins")"
      n=$((n + 1))
    fi
  done
  echo
fi

if [ "$want_hours" = 1 ]; then
  PIN_DATE=1
  printf 'every word that can sit below past (:%02d) and below to (:%02d)\n' \
         "$HOUR_MIN_PAST" "$HOUR_MIN_TO"
  for entry in "${HOURS_PAST[@]}"; do
    IFS='|' read -r hh idx word <<< "$entry"
    shoot "$(epoch "$TIME_ON" "$hh" "$HOUR_MIN_PAST")" \
          "hour-past-$idx-$word" "past $word"
    n=$((n + 1))
  done
  for entry in "${HOURS_TO[@]}"; do
    IFS='|' read -r hh idx word <<< "$entry"
    shoot "$(epoch "$TIME_ON" "$hh" "$HOUR_MIN_TO")" \
          "hour-to-$idx-$word" "to $word"
    n=$((n + 1))
  done
  echo
fi

# The duplicate check above cannot see a shot that caught the emulator's real
# clock: a real-time face is not a duplicate of anything, because real time
# keeps advancing. But the fixed-date sweeps all show one date, so a shot
# whose DATE LINE differs from the rest is one of those. Two of 59 came back
# that way once, and the only thing that noticed was a person looking.
odd_dates=0
if [ ${#pinned[@]} -gt 2 ]; then
  python3 tools/check_shots.py "${pinned[@]}" || odd_dates=1
  echo
fi

if [ "$unchanged" -gt 0 ]; then
  printf '%d shot(s) came out identical to the shot before them, after %d\n' \
         "$unchanged" "$RETRIES" >&2
  printf 'tries each. In the rounded modes that can be correct - two times\n' >&2
  printf 'drawing one face. Otherwise the rollover is being missed: raise\n' >&2
  printf 'LEAD (currently %ss) and run those shots again by name.\n\n' \
         "$LEAD" >&2
fi

cat <<EOF
$n screenshots in $OUT/

CHECK ONE SHOT AGAINST ITS OWN FILENAME before trusting the rest.

If the TIME is wrong, TZ_OFFSET is wrong - the emulator is not displaying
UTC${TZ_OFFSET} as assumed. Work out the difference and re-run, eg:

    TZ_OFFSET=7 tools/date_sweep.sh

If the DATE is wrong too, the offset is out by more than 11 hours.

emu-set-time does not redraw the face, and any pebble command resets the
emulated clock to real time. So this sets the clock ${LEAD}s early, lets the
emulator's own minute rollover fire the tick, and takes exactly ONE
screenshot - which is why the sweep needs a WRITE_SPEED 0 build. Each shot
is checked against the one before it; anything identical was named above.

Then change Lettering, or the reading mode, in the settings and run it again.
Two sets side by side is what makes a difference visible.

For the ascender/descender hunt around "past" and "to":

    tools/date_sweep.sh stacks    # 59 shots: everything that sits ABOVE them
    tools/date_sweep.sh hours     # 26 shots: everything that sits BELOW them

stacks wants EXACT mode - the rounded modes collapse most of the 59 onto a
dozen phrases and never split 21-29. hours looks the same in all three.

Sorted by name, stack-NN-past and stack-NN-to land next to each other, which
is the comparison worth making: the same word over each relation word in
turn. An offset that fixes one and breaks the other is the trap.
EOF
