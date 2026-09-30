# libhddvd 0.4.0

Experimental open-source HD DVD inspection library.

## Implemented

- HD DVD directory discovery (standard and advanced content)
- XPL XML inspection
- Native `HDDVDACA` reader (no libzip dependency)
- ACA directory listing, bounds validation, CRC-32 verification, member reads and safe extraction
- XMF manifest and XMU markup inspection
- EVO program-stream probing
- Conservative IFO/MAP probes (binary semantic decoding is still incomplete)

## ACA tools

```sh
hddvd-acainfo archived.aca
hddvd-acaextract archived.aca extracted
hddvd-menuinfo archived.aca
```

The ACA implementation was validated against a genuine HD DVD archive with 40 members. The sample itself is not distributed with this source tree. Regression tests construct a small synthetic archive using the observed container structure.

## Build

Requires libxml2.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Status

This remains experimental software. ACA parsing is now based on observed media and strongly validated by offsets and CRCs. Full IFO/MAP semantics and an HDi JavaScript/rendering runtime are future work.
