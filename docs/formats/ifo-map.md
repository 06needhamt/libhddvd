# IFO, VTI and MAP observations

These facts come from externally supplied discs (October 2026):
- 11 IFO files from one Standard Content disc (Dragon's Lair), and one complete title EVO (`HV006T01.EVO`, 14,370 sectors) for one of them.
- 130 MAP files from five Advanced Content discs.
- The `HVA00001.VTI` and one EVO file (`FBILogo.EVO`, 3373 sectors) from one of those discs.

No disc bytes are kept in the repository. All integers are big-endian, and every file was a whole number of 2048-byte sectors.

## IFO

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 12 | identifier | `HVDVD-VMG100` (1 file, `HV000I01.IFO`), `STANDARD-VTS` (10 files) or `ADVANCED-VTS` (the VTI) |
| 0x0c | 4 | last sector of the whole title set (title sets) or of the VMG | for all 10 title sets, it equals 2 × the IFO's sectors + the title EVO's sectors − 1 (IFO, EVO, backup IFO), as in DVD-Video |
| 0x1c | 4 | end sector of the IFO | equals the sector count minus one in all 12 files, as in DVD-Video |

`hddvd_ifo_read_header` accepts the three identifiers above and requires the end sector to match the size.

## Standard Content tables

The VMG (`HV000I01.IFO`) and title sets (`HVnnnI01.IFO`) follow DVD-Video's layout with HD DVD sizes. Every record below was checked across all 11 IFOs. One title set was also checked against its complete EVO.

### Title set (`STANDARD-VTS`)

Sector pointers in the first sector:

| Offset | Table | Notes |
|---:|---|---|
| 0x80 | end of this header | `0x7ff` (DVD: `0x3ff`) |
| 0xc0 | menu EVO | 0 in all 10 |
| 0xc4 | title EVO | equals the IFO's sector count in all 10, so the EVO follows the IFO |
| 0xc8 | chapter (PTT) search table | |
| 0xcc | PGC table | |
| 0xd0 | menu PGC unit table | not decoded |
| 0xd4 | time map table | 0 (absent) in one title set |
| 0xe0 | cell address table | |
| 0xe4 | VOBU address map | |

Tables except the VOBU map start with a 2-byte count, 2 reserved bytes and the 4-byte offset of their last byte.

**Chapters:** a 4-byte offset per title, then 4-byte `(PGC, program)` entries; a title's chapters run to the next title's offset. All 1-based.

**PGC table:** search pointers are **12 bytes** (DVD: 8): category (top bit: entry PGC; second byte: title number), 4 unknown bytes, then the PGC's offset. A PGC has 2-byte program and cell counts at 0 and 2, its playback time at 4, and at **0xa8** four 2-byte offsets: commands, program map, cell playback, cell positions. 0xb0 to 0x130 holds two identical 16-entry palettes. Fields between 8 and 0xa8 differ only between title sets with and without audio, so they are not decoded.

- **Commands:** pre, post and cell command counts (2 bytes each), 2 more bytes, then 8-byte commands. Their semantics are not decoded.
- **Program map:** 2-byte first cell of each program, strictly increasing from 1.
- **Cell playback:** **28 bytes** (DVD: 24): category, playback time, first VOBU, first ILVU end, last VOBU and last sector, then 4 bytes: a flags byte (0x10 to 0x70 seen, meaning unknown) and the 1-based cell command run after the cell, or two zero bytes. In all 692 cells, the command number never exceeded the PGC's cell-command count.
- **Cell position:** EVOB id (2 bytes), a reserved byte, cell id.

**Playback times** are BCD `hh mm ss ff`. The frame byte's top two bits are always `10`, and frames count 30 to a second at 30000/1001; the seconds byte's top bit is always set (meaning unknown). Every PGC's time equals the sum of its cells'. In `HV006T01.EVO`:
- **EVOB 2:** 107 pictures with 54 repeated fields are 134 frames, stored as 4 s 14 f.
- **EVOB 1:** 167.5 frames are stored as 5 s 18 f. The 3:2 pulldown half frame is rounded up.

**Cell address table:** 12-byte entries: EVOB id, a reserved byte, **cell id at byte 3** (DVD: byte 2), first and last sector. Entries are contiguous from sector 0. The last one ends at the title EVO's last sector (14,369 for `HV006T01.EVO`). Every cell lies inside the entry with its EVOB and cell ids.

**VOBU address map:** the offset of its last byte first, then a 4-byte start sector per VOBU. In `HV006T01.EVO` the 20 entries are exactly its 20 navigation packs. Every cell starts and ends on a VOBU and ends where the next VOBU starts.

**Time maps:** one per PGC, or none. Each has a unit byte, a reserved byte, a 2-byte count, and 4-byte entries:
- **Unit:** 1 (second), or 0 for an empty map.
- **Entry k:** names the VOBU playing at (k + 1) units from the PGC's start. In `HV006T01.EVO`, VOBU 13 starts at 30 frames = 1.001 s, so 1.000 s is still in VOBU 12, which is what entry 1 names. All 9 entries agree.
- **Top bit:** set exactly when the next entry is in another cell, or there is none (4,049 of 4,049 entries).
- **Entry counts:** short PGCs have one entry per whole second. Three long multi-cell PGCs have two fewer, which their missing EVOs cannot explain, so counts are not checked.

### Navigation packs (Standard Content)

A title EVO's navigation pack holds a private-stream-2 packet whose first byte is 0. Its data follows DVD's PCI general information, offsets from that byte:

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 1 | 4 | the pack's own sector in the EVO | all 20 packs |
| 0x0d | 4 | VOBU start time, 90 kHz | equals the VOBU's first video PTS in all 20 |
| 0x11 | 4 | VOBU end time | the next VOBU's start |
| 0x15 | 4 | end time of the EVOB's last VOBU | set only in each EVOB's last VOBU |
| 0x19 | 4 | elapsed time in the cell, BCD as above | `80 95` (0 s 15 f) in the second VOBU |

Advanced Content EVOs (`FBILogo.EVO`) have zeros at these offsets.

### VMG (`HVDVD-VMG100`)

| Offset | Table |
|---:|---|
| 0x0c | last sector of the VMG (35) |
| 0xc4 | title table |
| 0xc8 | menu PGC unit table (not decoded) |
| 0xd0 | title set attribute table: a 2-byte title set count (10) and per-set records, not decoded (identical in all 10 sets) |

**Title table:** **16-byte** entries (DVD: 12): a playback-type byte (not decoded), angle count, 2 unknown bytes, chapter count (2 bytes), 2 unknown bytes, title set and title number within it (2 bytes each), and the title set's first sector on the disc. All 60 titles' chapter counts equal their title sets' chapter tables. Title set 1 starts at sector 36, after the VMG. Each later set starts 17 sectors after the previous set's last sector (a 16-sector gap at all 9 boundaries), which is unexplained.

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

## Title time

Playlists give clip times as `HH:MM:SS:FF` in the TitleSet's `timeBase`.
- **Observed rate:** all five playlists use `timeBase="60fps"`.
- **What a frame means:** each frame is 1001/60000 s, not 1/60 s.
  - On both 300 discs, every one of the 86 primary clips is exactly as long as its time map at 60000/1001: the difference is 0 ticks in every case.
  - Read as a true 60 frames per second, every clip would come out shorter than its map, by 540 to 328,642 ticks.
  - South Park's 12-second `FBILogo` clip covers its 12.012-second map exactly.
- **Other discs:** on the remaining discs the maps run longer than their clips, which is consistent with clips that use only part of their map. No clip at 60000/1001 exceeds its map.
- **Other time bases:** never observed, so `hddvd_xpl_time_to_ticks` rejects them.

A clip plays its map from `clipTimeBegin` (default 0). A title time `t` inside the clip's `[titleTimeBegin, titleTimeEnd)` is therefore at `clipTimeBegin + (t - titleTimeBegin)` from the EVOB start time. The EVO presentation time is that offset plus the VTI's start time.

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
- **`hddvd_title_map_open`** opens, for one playlist title, every primary audio/video clip's time map and the VTI in the same directory. It requires each clip to fit inside its map.
- **`hddvd_title_map_locate`** turns a title time into the EVO URI, presentation time, EVOBU and sector to read from.
- **`hddvd-locate`** runs the lookup on a disc tree. On the supplied disc, its EVOBU start times equal those in `FBILogo.EVO`'s navigation packs.

- **Standard Content:** `hddvd_vts_open` reads a title set and `hddvd_vmg_open` the title table, enforcing the reference and range rules above. `hddvd-vtsinfo` prints either one, checks PGC times and discontinuity flags, and, given the title EVO, checks it against the navigation packs:
  - VOBU map entries are the navigation packs, each naming its own sector;
  - cell times and in-cell elapsed times match the packs' times;
  - every time-map entry names the VOBU playing at its time.

  All 11 Dragon's Lair IFOs open and pass, and `HV006I01.IFO` passes against `HV006T01.EVO`.

## What remains

- **Standard Content:**
  - command semantics;
  - menu PGCs and menu EVOs;
  - stream attributes (one disc's are all identical);
  - frame rates other than 30000/1001;
  - multi-angle and interleaved cells.
- **The VTI attribute table:** not decoded.
- **Angle and interleaved layouts:** none of the observed maps has them.
- **Other frame rates:** whether the time-map unit or the playlist clock changes at time bases other than 60fps is unconfirmed.
