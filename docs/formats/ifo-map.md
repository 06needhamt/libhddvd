# IFO, VTI and MAP observations

These facts come from externally supplied discs (October 2026):
- 11 IFO files from one Standard Content disc.
- 130 MAP files from five Advanced Content discs.
- The `HVA00001.VTI` and one EVO file (`FBILogo.EVO`, 3373 sectors) from one of those discs.

No disc bytes are kept in the repository. All integers are big-endian, and every file was a whole number of 2048-byte sectors.

## IFO

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 12 | identifier | `HVDVD-VMG100` (1 file, `HV000I01.IFO`), `STANDARD-VTS` (10 files) or `ADVANCED-VTS` (the VTI) |
| 0x0c | 4 | unknown | in title files it is much larger than the IFO itself; in DVD-Video this position holds the end sector of the whole title set |
| 0x1c | 4 | end sector of the IFO | equals the sector count minus one in all 12 files, as in DVD-Video |

`hddvd_ifo_read_header` accepts the three identifiers above and requires the end sector to match the size.

## VTI (`ADVANCED-VTS`)

The VTI is the IFO-shaped file that Advanced Content time maps name. The one available VTI is 4 sectors long.

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0xbc | 4 | sector of the EVOB table | 3; the table there lists both EVO files on the disc |
| 0xb8 | 4 | sector of a second table | 1; not decoded (see below) |

**EVOB table** (at the sector named at 0xbc):

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | zero |
| 2 | 2 | number of records |
| 4 | 4 | offset of the table's last byte, relative to the table |
| 8 | 4 × n | offset of each record, relative to the table |

Each record is 0x140 bytes:

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 2 | unknown | `0x2000` in both records (MAP byte 0x14 also holds `0x20`) |
| 2 | 256 | EVO file name, NUL-terminated | `FBILogo.EVO`, `feature.EVO` |
| 0x108 | 2 | EVOB number | 1 and 2; MAP offset 0x184 names this number |
| 0x10a | 4 | start time, 90 kHz | 3424 for `FBILogo.EVO`, equal to that EVO's first video PTS and to the start time in its first navigation pack |
| 0x10e | 4 | end time, 90 kHz | 1084504; end minus start equals the summed time-map playback time exactly, for both EVOBs |
| 0x112 | 4 | size in sectors | 3373, the size of `FBILogo.EVO`; for both EVOBs it equals the summed time-map sizes |
| 0x116 | 2 | attribute record number | 1 and 2 (see below) |
| 0x11e | 16 | `0xff` bytes | unknown |

The second table, at sector 1, holds two 1024-byte records using the same count, end-address and offset layout, but with the count at offset 0. Their contents look like stream attributes: the record for the feature, which has audio, differs from the record for `FBILogo.EVO`, whose EVO has only a video stream. They are not decoded. In both VTI records the EVOB number equals the attribute record number, so this one file cannot tell the two fields apart.

## MAP (time map)

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 12 | identifier | `HDDVD_TMAP00` in 130 of 130 files |
| 0x0c | 4 | end sector of the MAP | equals the sector count minus one in 130 of 130 files |
| 0x10 | 0x170 | unknown | identical in all 130 files apart from the VTI name |
| 0x72 | NUL-terminated | VTI file name | `HVA00001.VTI` in all 130 files |
| 0x180 | 4 | offset of the entry table | `0x1a0` in all 130 files |
| 0x184 | 2 | EVOB number in the VTI | unique within each disc |
| 0x186 | 2 | number of entries | the bytes after the table are zero in all 130 files |

Each entry is 32 bits and describes one EVOBU, which is a navigation pack and the packs up to the next one:

| Bits | Field | Evidence |
|---:|---|---|
| 31–21 | first reference picture size, sectors | equals the end address of the first reference picture plus one, as given in each EVOBU's navigation pack |
| 20–11 | playback time, in units of 3003/8 ticks (a tenth of a 24000/1001 frame) | the per-EVOBU start times in `FBILogo.EVO`'s navigation packs differ by exactly these amounts |
| 10–0 | EVOBU size, sectors | `FBILogo.MAP` has 20 entries, and `FBILogo.EVO` has 20 navigation packs at exactly these sizes apart |

Across 83,110 entries:
- The first-reference size ran from 4 to 497 sectors and never exceeded the EVOBU size.
- The playback time ran from 96 to 232 units.
- The EVOBU size ran from 6 to 1863 sectors.

The time unit was observed only on 24000/1001 content with the header bytes above. Discs at another frame rate may use a different unit, and might signal it somewhere in the unknown header range.

## Library behaviour

- **`hddvd_vti_open`** reads the EVOB table. It rejects:
  - a table outside the file;
  - a record outside its table;
  - an empty, unprintable or path-like file name;
  - a duplicate EVOB number;
  - an end time before the start time;
  - an EVOB with no sectors.
- **`hddvd_tmap_open`** reads the entry table, which must be aligned, start after the counts, fit in the file and hold at least one entry. It rejects any entry that has a zero size, a zero playback time, or a first reference picture that is empty or larger than the EVOBU.
- **Entry times** are accumulated in stored units and converted once, so rounding never builds up.
- **`hddvd_tmap_find`** turns an offset from the EVOB start time into an entry, whose `first_sector` gives the position in the EVO file.
- **`hddvd_tmap_check_evob`** compares a map with its VTI record.
- **`hddvd-timemap`** prints a VTI's EVOBs and checks MAP files against them. On the supplied disc both maps match. All 130 MAP files open.

## What remains

- **IFO tables of Standard Content discs:** not decoded.
- **The VTI attribute table:** not decoded.
- **Angle and interleaved layouts:** none of the observed maps has them.
- **Other frame rates:** whether the time unit changes is unconfirmed.
