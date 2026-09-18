# CT-W2 Minimal PING Protocol

This is the smallest Architecture-v3.2.2-compatible runtime protocol subset needed for R2 Final Control-Traffic / Service-Margin Acceptance. It is not the complete R3 lifecycle protocol.

## Transport

- USART2 through NUCLEO ST-LINK VCP
- 115200 baud
- 8 data bits, no parity, 1 stop bit
- RX: RXNE interrupt
- TX: DMA1 Stream 6 / Channel 4

## Byte order

All multibyte integer fields are little-endian.

## Frame

| Field | Size | CT-W2 value / rule |
|---|---:|---|
| Magic | 2 B | `A5 5A` |
| Version | 1 B | `01` |
| Type | 1 B | `01` PING, `81` PING_REPLY |
| Request ID | 2 B | unsigned, little-endian |
| Payload Length | 2 B | 0..64 |
| Payload | N B | bounded runtime payload |
| CRC-32 | 4 B | CRC-32/IEEE, little-endian |

CRC parameters:

- reflected polynomial: `0xEDB88320`
- init: `0xFFFFFFFF`
- xorout: `0xFFFFFFFF`
- check vector: ASCII `123456789` -> `0xCBF43926`
- coverage: all bytes from Magic through the final payload byte; the CRC field itself is excluded.

## PING request

PING is the only CT-W2 command. A request can contain 0..64 bytes. The hardware smoke uses all 64 bytes with a deterministic sequence-dependent pattern. The target validates the complete frame CRC and computes a CRC-32 over the complete PING payload, so the payload is real processed input rather than ignored padding.

## PING_REPLY payload

PING_REPLY has a fixed 12-byte payload:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | request-payload CRC-32 |
| 4 | 4 | W6 input counter observed when CommunicationTask processed the request |
| 8 | 1 | W6 phase |
| 9 | 1 | configured K |
| 10 | 1 | W6 mode (`0` NORMAL, `1` DROP) |
| 11 | 1 | request payload length |

The host smoke requires W6 phase `R2_W6_RUNNING` and a nonzero W6 input counter. Target RAM evidence independently records the same request in `g_r2_ct_result.trace[]`.

## Golden frames

PING, request ID `0x1234`, empty payload:

```text
A5 5A 01 01 34 12 00 00 85 62 FD 96
```

PING, request ID `0x0201`, payload bytes `00 01 ... 3F`:

```text
A5 5A 01 01 01 02 40 00
00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F
20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F
30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F
2F 1F 32 C9
```

## Parser / overload rules

- static allocation only;
- maximum CT runtime payload 64 bytes;
- finite Magic resynchronization;
- malformed length, bad CRC and unsupported version are counted and cannot alter W6 state;
- RX ring overflow and USART hardware errors latch CT infrastructure faults;
- at most 10 accepted PING requests in the preceding 1000 ms task-tick window; excess valid requests are rate-limited and counted;
- the fixed TX staging buffer cannot be reused until DMA completion confirms that USART2 no longer reads it.
