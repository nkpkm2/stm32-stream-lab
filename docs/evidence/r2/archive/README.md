# R2 Provenance Archive

This directory preserves R2 provenance without duplicating every expanded temporary tree as normal Git files.

## Policy

- Every top-level file present in `Downloads\tempa` at archival time is copied verbatim into `tooling/` and committed.
- `tempa-full-manifest.csv` records every recursively present tempa file by relative path, size, timestamp and SHA256.
- Expanded installer/handoff directories are not duplicated file-by-file when their source ZIP/snapshot is already preserved; their complete contents remain represented in the recursive manifest.
- Architecture v3.2.2 is the authoritative baseline. v3.2.1 copies are retained only as historical/superseded records.
- Historical hardware anchors preserve exact ELF/MAP/summary identities used for R1/W3/W4/W5 programmed-byte regressions.
- Superseded or failed host scripts are retained and classified; they are not treated as positive hardware evidence.
