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
| 0xd0 | menu PGC unit table | below; one root menu in 8 of 10 sets |
| 0xd4 | time map table | 0 (absent) in one title set |
| 0xe0 | cell address table | |
| 0xe4 | VOBU address map | |

Tables except the VOBU map start with a 2-byte count, 2 reserved bytes and the 4-byte offset of their last byte.

**Chapters:** a 4-byte offset per title, then 4-byte `(PGC, program)` entries; a title's chapters run to the next title's offset. All 1-based.

**PGC table:** search pointers are **12 bytes** (DVD: 8): category (top bit: entry PGC; second byte: title number), 4 unknown bytes, then the PGC's offset. A PGC has 2-byte program and cell counts at 0 and 2, its playback time at 4, and at **0xa8** four 2-byte offsets: commands, program map, cell playback, cell positions. 0xb0 to 0x130 holds two identical 16-entry palettes. Fields between 8 and 0xa8 differ only between title sets with and without audio, so they are not decoded.

- **Commands:** pre, post and cell command counts (2 bytes each), the 4-byte offset of the table's last byte, then 8-byte commands (see below). The end offset is 10 + 8n − 1 in all 101 command tables, and the program map follows immediately. The first decoder (October 2026) took the header as 8 bytes, which shifted every command by 2 bytes; the 10-byte header is checked now.
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

### Navigation commands

The 785 commands in the 11 IFOs use DVD-Video's 8-byte encoding, as libdvdnav documents it. The command type is in the top 3 bits of byte 0, the comparison in bits 6 to 4 of byte 1, and the link or jump subset in its low 4 bits. `hddvd-vtsinfo` prints them in that syntax and checks every target it can. Every target is valid in context:
- link targets are within the PGC's cells and programs, or the title's chapters;
- jump titles and chapters exist in the title set;
- each `Goto` line is within its command list.

| Form | Count | Example |
|---|---:|---|
| conditional `LinkPTTN` (compare a register with a value) | 308 | `20 b5 00 00 00 03 00 09`: if (GPRM0 != 3) LinkPTTN 9 |
| set a register (type 3: `=`, `-=`) | 109 | `71 00 00 3f 00 03 00 00`: GPRM63 = 3 |
| conditional `LinkCN` | 42 | `20 a7 00 03 00 01 00 02`: if (GPRM3 == 1) LinkCN 2 |
| `JumpVTS_PTT` (32 conditional on two registers) | 34 | `30 55 00 01 00 04 0a 3f`: if (GPRM10 > GPRM63) JumpVTS_PTT 4:1 |
| `Nop` | 24 | |
| `JumpTT`, `JumpVTS_TT` | 24 | `30 02 00 00 00 17 00 00`: JumpTT 23 |
| `SetHL_BTNN` (button = value / 1024), some conditional | 19 | `56 00 00 00 04 00 00 00`: SetHL_BTNN 1 |
| `LinkPGN`, `LinkPGCN`, `LinkPTTN` | 11 | |
| conditional `Goto` | 2 | `00 a1 00 02 00 01 00 05`: if (GPRM2 == 1) Goto 5 |
| `CallSS` (jump subset 8) | 212 | `30 08 00 03 00 b0 00 00`: CallSS VMG PGC 3 |

- **Registers:** commands use general registers 0 to 3, 10, 11 and 63. GPRM63 is a loop counter: it is set to 3, decremented, and tested with `if (GPRM63 > 0) LinkCN 1`. DVD-Video has only 16 general registers, so HD DVD has at least 64.
- **`JumpSS` and `CallSS`** (jump subsets 6 and 8) take their domain from bits 5 and 4 of byte 5: 0 first play, 1 a VMG menu and 2 a title set menu (menu id in the low nibble), 3 a VMG PGC (number in bytes 2 and 3, 15 bits). `CallSS` keeps the cell to resume at in byte 4. Bit 7 of byte 5 is set in all but one of them and is unexplained.
  - The first decoder read the domain from the low bits, which made 210 `CallSS` commands ask for title set menus that do not exist. Read this way, 211 `CallSS` and 6 `JumpSS` name VMG PGCs 2 to 10, all within the VMG's 10 menu PGCs. The last one calls the title set's root menu, menu id 3, and that set has one.
  - All 212 `CallSS` resume at cell 0.

### Menus and first play

**Menu PGC unit table** (title set 0xd0, VMG 0xc8): the usual count and end offset, then one 8-byte pointer per language unit: language code (2 bytes), an extension byte, existence flags (`0x80` in all 9 tables), and the unit's offset. Each unit is a PGC table with the 12-byte search pointers above.
- **Categories:** the high nibble of the second byte is DVD's menu id. The VMG's entry PGC is `82 20`, the title menu (2). Each title set's is `82 30`, the root menu (3). The other 9 VMG PGCs are `02 00`.
- **Shape:** menu PGCs hold commands only, with no programs or cells. That fits the missing menu EVOs (0xc0 is 0).
- **Contents:** each title set root menu either jumps to a title (`JumpTT`) or to a VMG PGC (`JumpSS`). The VMG's 10 PGCs keep the game's state in general registers: sets, compares, `Goto`, `LinkPGCN` and `JumpTT`.

**First play** (VMG): the 4-byte value at **0x84** is the byte offset of the first-play PGC within the VMG's first sectors, which end at the byte that 0x80 names. On Dragon's Lair the header ends at 1479, the PGC is at 0x486, and its single command is `JumpTT 25`.

Across the 11 IFOs there are 1,094 commands, 309 of them in menus and first play. Besides the forms above, they include:
- 14 conditional sets: `74 c0 15 15 00 27 00 27` is if (GPRM21 >= 39) GPRM21 -= 39. The condition compares the register in byte 2 with bytes 6 and 7, or with the register in byte 7.
- one `SetSTN`, which sets the subpicture stream.

`hddvd-vtsinfo` confirms every command except one, `59 00 00 00 c1 00 00 00`. That is type 2 sub-operation 9, which DVD-Video does not define.

### Command execution

`hddvd_vm_run` executes these forms with libdvdnav's semantics: 64 general registers and DVD's 32 system registers. The set operations are `=`, swap, `+=`, `-=`, `*=`, `/=`, `%=`, rnd, `&=`, `|=` and `^=`.
- **Saturation:** sums and products stop at 0xffff, and differences at 0.
- **Division by zero:** division and remainder by 0 give 0xffff.
- **rnd:** gives 1 to the value; 1 when the value is 0.

A run ends at the first link, jump, `Exit` or `Break`, at the end of the list, or at a form the VM does not run. It reports what to do next and leaves the next step to the host: which PGC, menu or title to load, and when to run its post or cell commands. The 65,536-command limit catches `Goto` loops.

`hddvd-vtsinfo` runs the VMG's first play. On Dragon's Lair it jumps to title 25.

### Stream attributes (title set)

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0x214 | 4 | video attributes | `2b 08 c0 00` in all 10 sets; bits 7–6 of the first byte are the coding (below) |
| 0x218 | 2 | number of audio streams | 1 in 9 sets; 0 in `HV006`, whose EVO has no audio |
| 0x21a | 8 × 8 | audio records | the subpicture count follows exactly 8 records later |
| 0x25a | 2 | number of subpicture streams | 1 in 4 sets; 0 in `HV006`, whose EVO has none |
| 0x25c | 6 each | subpicture records | one seen; 6 bytes as in DVD-Video, unconfirmed |

- **Audio records** (also used in the VTI, below): byte 0 is `0x1c` in all 10 records seen. Byte 1 is `0x04` when bytes 4–5 hold a language code ("en") and `0x00` when they are zero. Byte 2 is `0xd4`, `0xc4` or `0xd0`. No EVO with audio is available, so the coding and channel bits are not decoded.
- **Subpicture records:** the language code is at bytes 2–3 (`21 08 65 6e 00 00`).
- **Video coding:** 0 is MPEG-2 (`2b…` here; stream `0xe0` in `HV006T01.EVO`). 1 is VC-1 (`6b…` in the VTI; extended stream `0xfd` with id `0x55` in `FBILogo.EVO`). Other values are not decoded.

The library returns the coding, the languages and every record as stored. `hddvd-vtsinfo` checks the coding and both counts against the EVO's streams, when one is given.

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
| 0x80 | end of the header (byte offset; 1479) |
| 0x84 | first-play PGC (byte offset within the header) |
| 0xc4 | title table |
| 0xc8 | menu PGC unit table (above) |
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

The **attribute table**, at the sector named at 0xb8 (sector 1), holds two 1024-byte records. It uses the same end-address and offset layout, but with the count at offset 0. Each record holds:

| Offset | Size | Field | Evidence |
|---:|---:|---|---|
| 0 | 2 | unknown | `0x1000` for `FBILogo.EVO`, 0 for the feature |
| 2 | 4 | video attributes, as in title sets | `6b 10 c0 00` and `6b 00 c0 00`: VC-1, and `FBILogo.EVO` is VC-1 |
| 0x0e | 2 | number of audio streams | 0 for `FBILogo.EVO`, whose EVO has only video; 1 for the feature |
| 0x10 | 8 each | audio records, as in title sets | `1c 00 c4 00 00 00 00 00`: no language code |

In both EVOB records, the EVOB number equals the attribute record number, so this one file cannot tell the two fields apart. The library requires every EVOB's attribute number to name a record.

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
- **Menus and first play:** both IFOs read their menu PGC unit tables (`hddvd_vts_get_menu_*`, `hddvd_vmg_get_menu_*`), and the VMG reads its first-play PGC (`hddvd_vmg_get_first_play*`). Each pointer and PGC must lie within its table, and the first play within the header. A zero pointer means there is none. Menu cells are checked against their own PGC only.
- **`hddvd_vm`** runs command lists as described under "Command execution".

## What remains

- **Standard Content:**
  - the cell command flags, bit 7 of the `JumpSS`/`CallSS` domain byte, and `SetSystem` sub-operation 9;
  - menu EVOs and menu cells (none on the observed disc), and `JumpSS` to title set menus of another set (the target set is not decoded);
  - audio coding and channel bits, and the rest of the video attributes (only one combination seen);
  - frame rates other than 30000/1001;
  - multi-angle and interleaved cells.
- **The VTI attribute table:** bytes other than the video attributes and audio records.
- **Angle and interleaved layouts:** none of the observed maps has them.
- **Other frame rates:** whether the time-map unit or the playlist clock changes at time bases other than 60fps is unconfirmed.
