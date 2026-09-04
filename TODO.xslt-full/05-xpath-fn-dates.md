# 05 — XPath fn: catalog: dates & durations (#691-E)

xs:date/time/dateTime/duration constructors + components
(year-from-dateTime, months-from-duration, ...), current-dateTime/
current-date/current-time, adjust-to-timezone (fixed-offset),
implicit-timezone, plus arithmetic (add/subtract durations,
dateTime ± duration) exposed as functions where the 1.0 model
allows (date-difference via days-... family). Civil-calendar math
only (no timezone DB beyond fixed offsets — Saxon-HE parity needs
implicit TZ only).

Gate: Xslt30.FnDates spec (Saxon-probed) + suite green.


## Status 2026-09-04 — accessor matrix complete (value-level)

All accessors registered: year/month/day-from-date,
hours/minutes/seconds-from-dateTime, minutes/seconds-from-duration
(new), plus the pre-existing set. Duration parsing is now a full
ISO 8601 walker (P[nY][nM][nD][T[nH][nM][nS]], negatable) — the old
sscanf only understood P<days>DT<hours>H. xs:dayTimeDuration /
xs:yearMonthDuration are passthrough constructor aliases. Remaining
value-level gaps (tracked, non-blocking): adjust-*-to-timezone (no
timezone model), format-date/time/dateTime, current-* functions.
