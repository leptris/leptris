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
