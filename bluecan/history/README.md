# Bluecan history

How the device got to where it is: the patches, the measurements behind them, and the
decisions along the way. None of it is needed to build or use Bluecan; for that, start at
[../README.md](../README.md).

These files describe states that are over. A figure or a claim in them may have been
overtaken, and the files themselves say so where it was noticed, but not everywhere. Check
anything taken from here against the current documents.

| File | What it is |
|---|---|
| [firmware.md](firmware.md) | One section per firmware patch, 1 to 19: what was wrong, what changed, what it measured. Also the choice of upstream project, the display design, the first phone session and the code review log's starting point |
| [readme-2026-10-13.md](readme-2026-10-13.md) | The device's README until 2026-10-14: status, the decision that capture is the CANable's job and not this device's, and what the R9's first captures changed |
| [hardware-2026-10-13.md](hardware-2026-10-13.md) | The hardware page until 2026-10-14, with the reasoning behind the board, the transceiver, the battery and the bench rig |
| [numbers.md](numbers.md) | Every measured figure with a link to where it was measured, and the pattern list that `firmware/tools/check-numbers.sh` searches with |
| [bench-link.md](bench-link.md) | The two-node bench link: the tests, the one-hour soak, the display bug that lost 60% of frames, and the listen-only proof |
| [gs-usb-stall.md](gs-usb-stall.md) | The CANable's shipped firmware stalling after three frames, and the reflash that fixed it |
| [wsl-setup.md](wsl-setup.md) | Setting up the Windows and WSL2 development machine, as it was done |

The firmware's own commits, from joeroback/racechrono-canbus on, are in this repository's
history since 2026-10-14, as the second parent of the merge `3312b19` that brought them in:
`git log 3312b19^2`. They are at the paths they had then, `src/` at the top, so
`git log -- bluecan/firmware/bluecan` shows only what came after.

The review log and the list of bench work done without the bike are in the repository's
[journal/](../../journal/): [bluecan-code-review-log.md](../../journal/bluecan-code-review-log.md)
and [next-without-the-bike.md](../../journal/next-without-the-bike.md).
