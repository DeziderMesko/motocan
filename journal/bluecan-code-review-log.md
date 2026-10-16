# Bluecan code review log

> Moved here from the end of `bluecan/firmware.md` on 2026-10-12.

What has been reviewed, so the next review starts from a range rather than from a
question. The code lives in Bluecan, so the ranges are Bluecan's; this file is where
the record belongs because the patch list it describes is here.

| Date | Range reviewed | Result |
|---|---|---|
| 2026-08-22 (01:39) | Bluecan's `src/` as it stood, whole-tree rather than a range | landed as `b8bd163` — *Clamp the hardware DLC, and stop the panel reporting what it cannot know* |
| 2026-08-22 (23:13) | `b8bd163..171fe78` — 11 commits: patches 14, 15, 16 and 17 plus the display fixes | three findings, all fixed in `559f715` — the allow-list overflow counter surviving a `deny_all()`, `field()` indexing `_cache` unbounded, and the fuel gauge's charge rate overflowing an `int16_t` |
| 2026-08-22 (Fable) | Bluecan's whole `src/` again, plus `559f715..fb945fc`, **plus the whole motocan tree** — the first review to cover the documentation | no live bug. Bluecan: `56f21b8` (the logger out of `install()`'s critical section, error counting, `esp_intr_alloc()` checked, `stop()` implemented, two fields made atomic) and `7b77c05` (three wrong README claims, the stale 280 µs). motocan: `10dd898` (the allow-list outliving a disconnect, root `build/`) and the commit carrying this line (six stale or contradicting doc claims) |
| 2026-10-16 | `fb945fc..HEAD` in Bluecan — acknowledging, the R9 decoder and its latch, the LIVE page — **plus the whole motocan tree's notes** | no live bug. Bluecan: [patch 22](../bluecan/history/firmware.md#patch-22--a-reviews-fixes-the-latchs-fences-the-proof-window-a-comment--applied-and-built-not-uploaded) — the latch's sequence lock without its fences, the bit-rate proof's 200 ms counted from `start()` instead of from the first frame, a wrong comment on the interrupt mask. motocan: the terminator decision of 2026-10-16 carried into Bluecan's pages and the first checklist, Bluecan's bike checklist brought up to PWR and the wheel speeds, the front wheel's field found to end at 40.95 km/h (open question 10), a renamed DBC signal, a broken anchor and a doubled link |

**The next range starts after the commit that carries the 2026-10-16 row** (it was `fb945fc..HEAD` in Bluecan until then), and the review is worth running with the
same effort level as the three above. Since 2026-10-14 the firmware is in this repository,
under `bluecan/firmware/bluecan/`, brought in by the merge `3312b19`. Its commits from
before the move are that merge's second parent, at the old paths, and a path filter does
not find them; the two halves of the range are therefore:

```bash
git log --oneline fb945fc..3312b19^2
```

```bash
git log --oneline 3312b19..HEAD -- bluecan/firmware/bluecan
```

That third review also changed how this repository is laid out, which is the part worth
carrying forward: current state in [README.md](../bluecan/README.md), history here, values in
[numbers.md](../bluecan/history/numbers.md), and [check-numbers.sh](../bluecan/firmware/tools/check-numbers.sh) to find
the copies when one of them moves.

It is also the one that says why this table should keep including motocan:
**two of its factual errors were in Bluecan's README, in its newest commit, the one commit no
earlier review had covered**, and the rest were documents that had quietly stopped
agreeing with each other after a patch landed. Prose goes stale in exactly the places
code review does not look.

Two things that cost time and need not cost it again. **A review leaves no artefact in
either repository** — findings are reported in the session and the fixes land as an
ordinary commit, so without this table the only record is the Claude Code transcripts
under `~/.claude/projects/`, where an invocation shows up as a `<command-name>` entry
containing `/code-review`. And **the fix commit is the marker, not the review**: the
first review above ran against a tree that had no commit of its own, so what dates it is
the commit that answered it.
