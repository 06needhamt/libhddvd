# AACS object envelope observations

This note documents the framing observed in Advanced Content ACA members.  It is
not an AACS decryption specification.  Unknown bytes deliberately remain unnamed.

## Confirmed by the reference archive

All 40 members begin with a 283-byte envelope.  Integers below are big-endian.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `AACS` signature |
| 4 | 1 | object type (`0x01`, `0x11`, or `0x12` observed) |
| 5 | 3 | flags (`0x000000` or `0x000100` observed) |
| 8 | 3 | payload length |
| 11 | 272 | type-specific header data |
| 283 | payload length | payload |
| following payload | remainder | optional trailer |

For clear type `0x12` XML objects, offset 11 contains a NUL-terminated object
name such as `main.xmf.AACS`; the payload begins with XML or a UTF-8 BOM followed
by XML.  These objects have a four-byte trailer in the reference archive.

Objects with flag `0x000100` contain opaque protected bytes.  Type `0x01` is used
for image resources and has no observed trailer.  Type `0x11` is used for HDi
scripts and has a variable trailer.  libhddvd does not interpret the protected
header, decrypt payloads, or interpret trailers; a host-supplied callback is
required to produce clear payload bytes.

## Parser rules

The parser requires the signature and complete 283-byte header, reads only the
24-bit payload length, verifies that the payload fits, and exposes all remaining
bytes as the trailer.  It extracts a name only from an unprotected header.  It
does not scan for XML or PNG signatures to find payload boundaries and does not
assign meanings to the remaining type-specific bytes.

Synthetic objects in `tests/test_object.c` are independently generated from this
layout and contain no bytes from the reference archive.
