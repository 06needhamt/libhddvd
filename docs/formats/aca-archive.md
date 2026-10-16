# ACA archive observations

`src/aca.c` reads the `HDDVDACA` container: a 32-byte header with a 16-bit
member count at offset 12, then one directory entry per member (offset, size,
CRC-32, flags, name length, name, 32 reserved bytes). Members must lie inside
the file, in ascending order, without overlapping, and with unique
case-insensitive names. This note records what externally supplied archives
showed about that layout. No archive bytes are kept in the repository.

## Two observed member layouts (October 2026, 17 archives from 5 discs)

**Listed envelope (16 archives).** Each directory entry covers the whole
stored object, including its `AACS` envelope header. The first member starts
exactly where the directory ends, and every entry's CRC-32 matches its bytes.

**Unlisted envelope (1 archive, 328 members).** Each directory entry covers
only the clear payload (PNG, XML, RIFF and UTF-16 text were seen). An
unlisted block immediately before each payload holds that member's `AACS`
envelope header, so the first member starts after the directory instead of at
its end. In 277 of these blocks the big-endian 32-bit value at block offset 7
equals the entry's size. The archive also has 4336 unlisted bytes after the
last member. None of the entry CRCs matched standard or common CRC-32 variants
over the payload, the header plus payload, or nearby windows, so what these
CRCs cover is unknown.

## Reader behavior

* The first member may start at or after the end of the directory, but never
  inside it. Bytes before the first member, between members, and after the
  last member are not interpreted.
* Members are read by their directory entry in both layouts. CRC verification
  reports the unlisted-envelope archive's members as mismatches, because the
  coverage of its CRC is not established; this is reported, not hidden.
