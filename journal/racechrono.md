# RaceChrono: where it stands

Working notes on the channels in
[bluecan/racechrono-channels.md](../bluecan/racechrono-channels.md), which says only what to
enter.

**2026-10-17.**

- Every channel on that page except the Track display (Digital 3) is entered in
  RaceChrono Pro on Android and is in the export,
  [bluecan/racechrono/vehicle_2026_yamaha_r9.rcz](../bluecan/racechrono/vehicle_2026_yamaha_r9.rcz).
- Every equation on the page and in the export agrees with r9.dbc on every distinct
  payload in the captures (`analysis/tools/7-check-racechrono-export.py`).
- The Track display was left out by choice.
- Importing the export into the app has not been tried.
- Nothing has been seen in the app on the bike. Against a replay, the channels entered by
  2026-10-13 read right; the corrected wheel speeds, TCS, PWR and the valve as commanded
  have not been looked at in the app yet.
- Without a GPS fix the Live screen shows no Time and `- Hz`. That a fix brings them is
  likely and not yet checked.
