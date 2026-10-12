# AiM Yamaha CAN protocols (copies)

Four DBC files copied verbatim on 2026-08-22 from
[billyjack2/CAN-DBC-Collection](https://github.com/billyjack2/CAN-DBC-Collection)
(`YAMAHA/…/can1.dbc`), kept here because they are the strongest source in
[pre-bike-mapping.md](../../background/pre-bike-mapping.md) and the upstream collection is one person's
repository. They are **[S6]** there; read that file's "How much to trust the AiM reading"
section before using any number below.

| File | Upstream path | AiM protocol name | Covers |
|---|---|---|---|
| `FZ09_MT09_can1.dbc` | `YAMAHA/FZ09_MT09/` | `YAMAHA – FZ09MT09` | FZ-09/MT-09 2014–2020 and 2021+ Euro5, MT-07/FZ-07 2021+, YZF-R7 2022+ — **the R9's platform** |
| `FZ07_can1.dbc` | `YAMAHA/FZ07/` | `YAMAHA / FZ07` | FZ-07/MT-07, pre-2021 |
| `CAN_2015_can1v3.dbc` | `YAMAHA/CAN_2015/` | `YAMAHA / CAN_2015` | YZF-R1/R1M from 2015, YZF-R6 from 2017 — the family [S4] reverse-engineered by hand |
| `YZ-F_can1.dbc` | `YAMAHA/YZ-F/` | `YAMAHA / YZ-F` | YZ-F motocross |

`CAN_2015/can1v2.dbc` and `can1v3.dbc` are byte-identical from `BU_:` onwards; only v3 is
kept. The collection's own note says protocols converted from another format keep the
source file alongside the `.dbc` — for these there was none, so the conversion is the only
artefact.

## The user guides, and the connector photo

Copied on 2026-09-21, the day the pigtail arrived and the pin numbering had to be found
in a hurry. They are **[S7]** in [pre-bike-mapping.md](../../background/pre-bike-mapping.md).

| File | What it is |
|---|---|
| `Yamaha_FZ_MT_09_all_models_ecu_eng_100.pdf` | ["YAMAHA MT-09 / FZ-09", release 1.00](https://support.aimshop.com/downloads/stockEcu/bike/yamaha/Yamaha_FZ_MT_09_all_models_ecu_eng_100.pdf) — page 3 has the Euro5 pin table *and* the only picture found anywhere of which cavity is which |
| `aim_kits_for_sports_bikes_yamaha_doc_06_eng.pdf` | ["MT-07, FZ-07, MT-09, FZ-09 from 2021 and YZF-R7 from 2022", release 1.01](https://dwls.aim-sportline.com/doc/eng/bike_kits/yamaha/aim_kits_for_sports_bikes_yamaha_doc_06_eng.pdf) — page 2 has the cable's constructive scheme, with the male connector's cavities **numbered**, drawn from the wire side. It was missed on the first pass because every label in it is part of a bitmap and a text search finds nothing |
| `aim-cable-scheme-insertion-view.png` | that scheme, extracted at its native 2962×902 |
| `euro5-connector-can-pins.png` | the photo from page 3 of the first, extracted at its native 648×576 |

| `kds-euro5-male-pinout.jpg` | not AiM's — [KDS's adapter page](https://kds-online.de/en/obd-adapter-euro5-for-motorcycle-6-pin-to-obd2-k-line-can-bus.html), kept here because it is the other half of the same question: the only picture found that **numbers** the cavities, on the male half, with the ISO/DIS 19689 allocation beside it |

AiM's photo carries no cavity numbers, only two arrows. What it shows is in
[connector.md](../../connector.md) under the connector table.

## Reading the DBC files

Read them with `cantools`; the collection recommends `strict=False`, and the R1 family
needs it — `BO_ 608` (`0x260`) declares a 9-byte payload, which no classic CAN frame can
carry.
