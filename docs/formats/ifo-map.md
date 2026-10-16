# IFO and MAP header observations

These facts come from 141 files on five externally supplied discs (October
2026): 11 IFO files from one Standard Content disc and 130 MAP files from four
Advanced Content discs. No disc bytes are kept in the repository. All integers
are big-endian. Every file was a whole number of 2048-byte sectors.

## IFO

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 12 | identifier | `HVDVD-VMG100` (1 file, `HV000I01.IFO`) or `STANDARD-VTS` (10 files) |
| 0x0c | 4 | unknown | much larger than the IFO in title files; in DVD-Video this position holds the end sector of the whole title set, which cannot be checked without the EVO files |
| 0x1c | 4 | end sector of the IFO | equals the sector count minus one in 11 of 11 files, as in DVD-Video |

`hddvd_ifo_read_header` accepts the two identifiers above and requires the end
sector to match the size. Other fields, including the sector pointers seen
from 0xc4, are not interpreted yet.

## MAP (time map)

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 12 | identifier | `HDDVD_TMAP00` in 130 of 130 files |
| 0x0c | 4 | end sector of the MAP | equals the sector count minus one in 130 of 130 files |
| 0x10 | 0xf0 | unknown | identical in all 130 files, apart from the VTI name below |
| 0x72 | NUL-terminated | VTI file name | `HVA00001.VTI` in all 130 files |

Per-file data starts after offset 0x100. `hddvd_map_read_header` checks the
identifier, sector framing, end sector, and a printable VTI name of fewer than
64 characters; `hddvd_map_probe` sets `structure_validated` when they hold.

## What remains

Decoding the time-map entries and IFO tables needs evidence of what they point
to. The supplied files include neither the VTI files the MAPs name nor any EVO
files, so title times cannot yet be mapped to EVO byte ranges.
