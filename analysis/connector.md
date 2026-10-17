# The diagnostic connector, and finding the CAN pair on it

Where the adapter goes. The R9 has the red 6-pin Euro5 connector (ISO 19689) under the
seat; the service manual calls it the YDT coupler.

| Pin | Function | Measured on the R9, 2026-10-10 |
|---|---|---|
| 2 | CAN High | 3.2 V against ground, key on |
| 3 | Ground | 0 Ω to battery negative |
| 4 | Battery +12 V, unswitched | 12 V with the key off. Left unconnected |
| 5 | CAN Low | 1.7 V against ground, key on |
| 6 | K-line | not used here |

<img src="sources/r9/pigtail-verified-pins.jpg" width="320" alt="The male half of the red 6-pin connector on a pigtail, seen from the wire side, marked in pen: H and a ground mark on the latch row, +12V on the other row">

*The pigtail after metering, from the wire side, latch up: `H` on CAN High with ground
beside it, `+12V` at the opposite corner.*

500 kbps. About 120 Ω across the pair with the key off, pigtail alone: one terminator, not
the two of a standard CAN bus, which would read 60 Ω. The pair has only the ECU on it
([R9_CANbus.md](R9_CANbus.md)), so the ECU's frames go unanswered unless the adapter
acknowledges them — see the [first](captures/session-2026-10-11_115626/README.md) and
[second](captures/session-2026-10-11_130905/README.md) captures. An acknowledging adapter is
the bus's other end, so its own 120 Ω terminator is **on**, and with it attached the pair
reads about 60 Ω. About 120 Ω with the adapter attached means its terminator is off.

Key on, engine off, against ground, CAN High reads 3.2 V and CAN Low 1.7 V: High above
2.5 V and Low below it is the pair the right way round with frames moving on it.

## Identifying the pins

`[S7]` and the other source tags are listed in
[background/pre-bike-mapping.md](background/pre-bike-mapping.md#sources).

| | Connector | Pins |
|---|---|---|
| MT-09 2014–2020 | 4-pin Sumitomo, under the **tail** | Blu/Red = CAN High, Blu/Black = CAN Low, Red/Black = +VB (unswitched), Black = GND |
| MT-09 **2021+ Euro5**, and the R9 | ISO 19689 6-pin red connector, **under the seat** | 2 = CAN High, 3 = GND, 4 = battery voltage, 5 = CAN Low |

Adapters for it are sold as "6-pin red Euro5 to OBD2".

**The CAN pair needs no pin numbers.** [S7]'s photo of the red connector
([copy](sources/aim-yamaha/euro5-connector-can-pins.png)) has two arrows: the cavities are
two rows of three, **CAN High is the middle cavity of the row next to the latch, CAN Low is
the middle cavity of the other row.** The middle column is the same cavity from the mating
face and from the wire side, and on either half of a mated pair, so left/right mirroring
does not touch it. Only the rows have to be told apart, and the latches of the two halves
line up.

**Ground and battery are 3 and 4.** [S7] says so for the MT-09, and KDS, a German adapter
seller citing ISO/DIS 19689, prints the same table with the K-line added: 2 = CAN_H,
3 = Ground, 4 = VBAT +12 V, 5 = CAN_L, 6 = K-Line, pin 1 unallocated
([copy](sources/aim-yamaha/kds-euro5-male-pinout.jpg), from
[their product page](https://kds-online.de/en/obd-adapter-euro5-for-motorcycle-6-pin-to-obd2-k-line-can-bus.html)).
Their picture numbers the cavities of the **male** half, the pigtail's: looking into its
mating face, `3 2 1` across one row, `6 5 4` across the other. The female is the mirror
image, `1 2 3` / `4 5 6`. Page 2 of [S7]'s 2021+ guide
([copy](sources/aim-yamaha/aim-cable-scheme-insertion-view.png)) draws the male as `1 2 3`
over `4 5 6` in *contact insertion view*, the face the terminals are pushed into, with the
wires coming towards you: KDS's view from the other end. AiM's wire colours are AiM's
cable's and say nothing about a pigtail bought elsewhere.

**Upside down swaps ground with battery.** Turning the connector 180° swaps 3 with 4 as
well as 2 with 5, and the populated cavities 2, 3, 4, 5 look identical both ways up. CAN
High is in the row next to the latch, so that row is `1 2 3`; a fifth wire, if there is
one, is the K-line on 6 in CAN Low's row, which leaves the empty cavity as pin 1.

So **meter before anything is screwed down**. Plugged into the bike with the key off,
+12 V is unswitched and reads 12 V against battery negative, ground reads 0 Ω to it, and
the CAN pair reads about 120 Ω. The +12 V wire is left unconnected and insulated; the
CANable is powered from USB.

On this R9 the pigtail is marked in pen on the male half
([photo](sources/r9/pigtail-verified-pins.jpg)), seen from the wire side with the latch
up: `H` over the middle cavity of the latch row with the ground mark beside it, and `+12V`
on the other row, at the corner diagonally opposite ground. That is `1 2 3` over `4 5 6`
as AiM's insertion view draws it — CAN High on 2, ground on 3, battery on 4 — and CAN Low
is the unmarked middle cavity of the lower row. The wire colours do not help: the latch row
is all green and the other row brown.
