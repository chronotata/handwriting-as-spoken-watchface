#!/usr/bin/env bash
#
# Screenshot the watchface across every shape it can draw.
#
#     tools/date_sweep.sh              # days, dates, then times
#     tools/date_sweep.sh days         # every day number, 1st to 31st
#     tools/date_sweep.sh dates        # one date per month
#     tools/date_sweep.sh times        # just the phrase
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
# WHY IT WAITS AS LONG AS IT DOES
# -------------------------------
# The reveal draws each changed word a slice at a time, so a screenshot taken
# too early catches the face half-written - which is exactly what the first
# version of this script produced, with the year still going down.
#
# tools/test/run.sh reports the worst case:
#
#     longest reveal       629px of stroke, 2.42s at WRITE_SPEED 260
#
# So SETTLE is derived from that rather than guessed. If WRITE_SPEED in
# config.h changes, re-read that line and change this to match.
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
SETTLE="${SETTLE:-3.5}"          # seconds; worst-case reveal is 2.42
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

shoot() {   # epoch, output name, label
  run_quiet pebble emu-set-time --emulator "$EMU" "$1"
  sleep "$SETTLE"
  run_quiet pebble screenshot --no-open "$OUT/$2.png"
  # printf, not echo: a format string starting with "->" is read as an
  # option, which is how the first version of this line died.
  printf -- '  %-28s -> %s/%s.png\n' "$3" "$OUT" "$2"
}

# --- what to do -------------------------------------------------------
want_days=0; want_dates=0; want_times=0; named_d=(); named_t=()
if [ $# -eq 0 ]; then
  want_days=1; want_dates=1; want_times=1
else
  for a in "$@"; do
    case "$a" in
      days)  want_days=1 ;;
      dates) want_dates=1 ;;
      times) want_times=1 ;;
      *:*)   named_t+=("$a|custom") ;;
      *)     named_d+=("$a") ;;
    esac
  done
  [ ${#named_d[@]} -gt 0 ] && { DATES=("${named_d[@]}"); want_dates=1; }
  [ ${#named_t[@]} -gt 0 ] && { TIMES=("${named_t[@]}"); want_times=1; }
fi

printf 'emulator %s, %ss settle, assuming it displays UTC%+g\n\n' \
       "$EMU" "$SETTLE" "$TZ_OFFSET"
n=0

if [ "$want_days" = 1 ]; then
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
  printf 'dates (%02d:%02d local on each)\n' "$DATE_HOUR" "$DATE_MIN"
  for d in "${DATES[@]}"; do
    shoot "$(epoch "$d" "$DATE_HOUR" "$DATE_MIN")" "date-$d" "$d"
    n=$((n + 1))
  done
  echo
fi

if [ "$want_times" = 1 ]; then
  printf 'times (dated %s)\n' "$TIME_ON"
  for entry in "${TIMES[@]}"; do
    hhmm="${entry%%|*}"; tag="${entry##*|}"
    shoot "$(epoch "$TIME_ON" "${hhmm%%:*}" "${hhmm##*:}")" \
          "time-${hhmm/:/}-$tag" "$hhmm  $tag"
    n=$((n + 1))
  done
  echo
fi

cat <<EOF
$n screenshots in $OUT/

CHECK ONE SHOT AGAINST ITS OWN FILENAME before trusting the rest.

If the TIME is wrong, TZ_OFFSET is wrong - the emulator is not displaying
UTC${TZ_OFFSET} as assumed. Work out the difference and re-run, eg:

    TZ_OFFSET=7 tools/date_sweep.sh

If the DATE is wrong too, the offset is out by more than 11 hours.

If a shot looks half-drawn the reveal had not finished: raise SETTLE.

Then change Lettering, or the reading mode, in the settings and run it again.
Two sets side by side is what makes a difference visible.
EOF
