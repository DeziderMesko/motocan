# For agents, and for anyone about to change something

motocan works out what a Yamaha R9 (2026) sends on its CAN bus. The front page,
[README.md](README.md), says what has been found. This page is how the work is done, so
that a second pair of hands does not undo the first.

## Read in this order

1. [README.md](README.md) — the decoded table and the status. Two minutes.
2. [analysis/r9-signals.md](analysis/r9-signals.md) — every id, named or not. **This is the
   one place the current state is kept.**
3. [analysis/open-questions.md](analysis/open-questions.md) — what is left and what would
   settle it.
4. Only then, and only the one you need: a write-up under
   [analysis/captures/](analysis/captures/) for the evidence behind a row,
   [analysis/R9_CANbus.md](analysis/R9_CANbus.md) for the wiring,
   [analysis/README.md](analysis/README.md) for the tools.

`analysis/background/`, `journal/` and `bluecan/history/` are history. They are
long, and they describe states that are over. Do not load them to get oriented, and do not
take a number from them without checking it against `r9-signals.md`.

## Hard rules

- **The adapter never sends a frame of its own.** It may acknowledge (`2-session.sh
  --ack`), because this connector is silent otherwise. No diagnostic requests, no OBD
  queries, no replayed frames. Do not write a tool that transmits, and do not propose one
  as the next step; the owner has decided against it for now.
- **Nothing is captured while riding.** Sessions are at a standstill or on a stand.
- **The capture files are evidence.** Never edit a `capture.log.xz`, `ref.csv` or
  `notes.txt`; `SHA256SUMS` in each folder is there to show nobody did. Derived files go
  beside them under a new name, with a line at the top saying how they were made.
- **A record of a decision is not deleted.** When something turns out wrong or is
  overtaken, annotate it with the date and what replaced it, and leave the original
  wording. `journal/` is the exception: the owner prunes it.

## How a finding is written down

- Bytes are numbered from 0. `b3` is byte 3, `hi` and `lo` are its nibbles, bit 0 is the
  least significant. Give the mask or shift as code: `b3 >> 1`, `(b0 >> 4) << 8 | b2`.
- **Describe a field by when it changes, not by what you think it is.** "Set for half a
  second as the engine starts" is a finding; "starter relay" is a guess, and goes in the
  column for guesses with the word *guess* or *candidate* on it. A field earns a name when
  something independent of the bus agrees with it — the dash, a measurement, a second
  sensor — and the row says what that was and how many times.
- Say how sure, in counts: "12 anchors of 12", "both starts captured", "one occurrence".
- A negative result is a result. "Looked for and not found" goes into `r9-signals.md` with
  what was tried, so it is not looked for again.
- Give every claim a date when it is written, and write absolute dates.
- Check arithmetic against the capture before writing it. If a number in a document was
  computed, the reader should be able to recompute it from the files in the repository.

## Adding a capture session

1. Run `analysis/tools/2-session.sh --ack` at the bike. It writes a folder under
   `~/captures/`.
2. Copy into `analysis/captures/<session>/`: `capture.log` compressed with `xz`, `ref.csv`,
   `notes.txt`, `SHA256SUMS`. Large media stays out; put its hash in `SHA256SUMS`.
3. Write `README.md` in that folder: what was done, what was found, what was looked for
   and not found.
4. Update `analysis/r9-signals.md`, and `analysis/open-questions.md` if the session closed
   or opened something. If a decoded row changed, update the table in the root
   `README.md` and `analysis/r9.dbc` together — the three must agree.

## Working here

- The analysis runs from `analysis/.venv` (`tools/0-setup.sh` builds it):
  `analysis/.venv/bin/python …`. `tools/1-selftest.sh` and the session script need `sudo`
  for the CAN interface.
- A `candump` log line is `(epoch) can0 27C#00000006400030`. Candump timestamps are
  wall-clock, and under WSL2 the wall clock steps; do not measure a rate to better than a
  percent from them.
- `analysis/css-skill/` is vendored at a pinned commit. Do not edit it.
- Bluecan's firmware source is a separate repository,
  [DeziderMesko/racechrono-canbus](https://github.com/DeziderMesko/racechrono-canbus),
  branch `trunk`. `bluecan/firmware/` here holds only bench sketches and tools. Call the
  firmware Bluecan.
- Prose is plain English in full sentences, written for someone who has not seen the
  conversation that produced it. Match the page you are editing.
- After moving or renaming anything, check every relative link and anchor in the
  Markdown files; they are dense and they cross folders.
