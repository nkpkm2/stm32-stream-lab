# R2-W6 Mandatory K Matrix Evidence

## Status

- Work package: R2-W6
- Firmware milestone: `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`
- Mandatory hardware matrix: **8 / 8 PASS**
- Native W1-W6 suite: **116 / 116 PASS**
- Committed-state programmed-byte regression: **12 / 12 PASS** (8 W6 cells + W5/W4/W3/R1)
- R2 overall: **IN PROGRESS**
- `r2-pass`: **NOT CREATED**

## Scope

W6 validates the mandatory K = 1 / 2 / 4 / 8 matrix using the same BufferPool, dynamic DBM, queue ownership, and controlled-drop mechanisms established by W1-W5.
Each K is exercised in NORMAL and DROP mode for 96 DMA transfer-complete input events.
DROP mode uses a Processing hold of K+5 block periods to exhaust the K FREE tokens and force controlled capacity drops.

The W6 timing gates are:

- nominal completion -> rebind/drop decision <= 57600 cycles (0.25 TB)
- nominal completion -> ISR end <= 80640 cycles (0.35 TB)
- final protected window <= 3600 cycles

## Hardware matrix

| Cell | Admit | Drop | Recoveries | Max drop streak | Max decision | Max IRQ end | Max window | ELF SHA256 | Trace SHA256 |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| K=1 NORMAL | 96 | 0 | 0 | 0 | 6752 | 11765 | 1075 | `311835D2C505F239C0791F0C144548B24CC5E7BD5E177C5DB2FAF6FA8DF62A93` | `0A02ADF3465F913F3B16ACBE574EF962DB39E294BF1EAC7915B644EA44776CCD` |
| K=1 DROP | 14 | 82 | 13 | 6 | 6808 | 11833 | 1099 | `0998BEC54FDCFBA81A5E995DB892A3CEF0B1E6C0C910622CB5B3E7311FB429CE` | `ABB4414C175478190235026A7B4BB23E38723B9762DFB02E130CD5E3DB8D24B9` |
| K=2 NORMAL | 96 | 0 | 0 | 0 | 6800 | 11836 | 1075 | `1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7` | `03C735C8368F4025D965B7C46555377758E45CC68D3378F55F94D4E373B81070` |
| K=2 DROP | 15 | 81 | 13 | 7 | 6593 | 11586 | 1099 | `B9D7222ADF3E4BEAD78F53E0F5FACC8C43691E1899E36DC72DCE0155BC3C1FA1` | `606A9995DEB8B30B7B827DCFFD450B51A99CBE6C3D2149C0765A10D503E2C857` |
| K=4 NORMAL | 96 | 0 | 0 | 0 | 6856 | 11947 | 1075 | `68599AA9820108398F80538286070E54DA3A53EC6968FBDBFD64B2E4C3FF32BF` | `0704CDB78C6A9B37E2F9CC11DFB7E319E0AE5D7E5B5F4C854BB237EB0FE134C1` |
| K=4 DROP | 14 | 82 | 10 | 8 | 6880 | 11737 | 1099 | `C732A527A7B49BCAC109C3BCF5E6E402723A457574D447C616FA3D7B7C1E9134` | `216628C0B066C645E53A64A199E325665D12B40470A8E35FA8DE9A754677251D` |
| K=8 NORMAL | 96 | 0 | 0 | 0 | 6937 | 12150 | 1076 | `E102911CC6A2C81D65ED03BD63B1558018D59BB507436DD7D2E7B67D69F11B18` | `011D5953755288F595FD7FA415CF6D7D52D5AEF5EABF0685D2D342039D40D6A4` |
| K=8 DROP | 15 | 81 | 7 | 12 | 6632 | 11838 | 1075 | `B687A7D83E51533622AE77AABFC97E23FF0131964C6B7A2AD041AB48BB756581` | `8EB3A3281918AB4A984C7C549AD3C7604D9ECDB1F0051F6BCFD812990B60D555` |

## Mandatory invariants verified

- NORMAL: every input event is admitted; the completed DMA-owned buffer is published READY, processed, released FREE, and later reused.
- ADMIT: only the inactive MxAR is rebound; the active DMA target is unchanged; mapping epoch advances exactly once.
- DROP: M0AR/M1AR are unchanged; mapping epoch does not advance; no READY descriptor is published; the dropped buffer does not enter Processing.
- DMA TE/DME/FE = 0 and ADC OVR = 0 in all eight cells.
- BufferPool, DMA-slot, queue, notification, and token-ledger violations = 0.
- Admitted samples and canaries validate with zero errors.
- Final stable ownership is K FREE + 2 DMA_OWNED, READY=0, PROCESSING=0.
- Post-stop quiet-window counters do not advance.

## Reproducibility

Before commit, the current source reproduced all eight hardware-tested MCU programmed images byte-for-byte.
After firmware milestone commit, committed-state regression reproduced all eight W6 programmed images and the W5/W4/W3/R1 known-good programmed images: 12 / 12 PASS.

The whole ELF containers were not byte-identical across rebuild directories, but MCU programmed bytes were identical. No stronger claim is made.

## Evidence layout

- `hardware-matrix.csv` - machine-readable 8-cell result table.
- `cells/` - exact hardware-tested ELF/MAP identity, flash/reset logs, RAM inspection, raw trace, CSV trace, and validator artifacts for each cell.
- `regression/precommit-reproducibility/` - source-provenance and eight-cell programmed-image reproduction evidence.
- `regression/committed-state/` - committed-state Native/W6/lower-layer programmed-byte regression evidence.
- `../archive/` - architecture baselines, original downloaded tooling/packages, historical hardware anchors, inventory packages, and complete tempa manifest.
- `FAILURES_AND_DEVIATIONS.md` - host-side tooling deviations and superseded scripts; these are not represented as firmware failures.

## Boundary

W6 closes the mandatory K matrix. It does **not** create `r2-pass` and does not by itself close R2.
Architecture v3.2.2 still requires the final worst-permitted-control-traffic / service-margin stress acceptance before R2 can become a final PASS candidate.
