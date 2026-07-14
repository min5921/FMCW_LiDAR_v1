# Data Contract

This document defines the data boundary shared by acquisition, processing, storage, replay, UDP, and UI code.

## Raw Frame Unit

One `RawFrame` means:

- one selected digitizer channel, A or B;
- one external trigger emitted at the start of the up chirp;
- one complete captured laser period containing both the up and down chirps;
- one record before software chirp segmentation.

An Alazar DMA buffer contains the records for one B-scan line. Each record is one A-scan `RawFrame`, and a configured number of completed B-scan lines forms one complete raster frame.

For the active scan contract, `records_per_buffer` is the A-scan count in one B-scan line. The operator sets B-scans per frame. The MCU waveform contains `records_per_buffer * B-scans_per_frame` points, while B-scan rate and period are measured from Alazar DMA buffer completion timestamps.

Runtime metadata also carries `dma_buffer_sequence`, zero-based `record_index_in_buffer`, and `records_in_buffer`. Live Time Domain and FFT snapshots update only when `record_index_in_buffer` matches the operator-selected A-scan. This display filter does not remove frames from processing, peak analysis, B-scan assembly, UDP frame assembly, or raw storage.

The v1 acquisition UI exposes only the `up_chirp_only` trigger mode. Legacy up/down trigger pairs may be imported by replay or migration tools, but are not an active hardware profile option.

After a digitizer adapter produces a frame, the acquisition controller publishes it as immutable shared ownership to bounded processing and storage queues. Consumers must not modify sample or metadata memory. UI rendering receives a downsampled snapshot rather than owning the acquisition buffer.

## Segment Convention

All sample ranges are half-open intervals: `[start_sample, end_sample_exclusive)`.

A valid segment must satisfy:

```text
0 <= start_sample < end_sample_exclusive <= record_length
```

Up and down ranges must not overlap. Guard samples are excluded before ranges are committed to a processing configuration. If segment lengths differ, the processing profile must explicitly select crop, zero-padding, or resampling; implicit resizing is not allowed.

## Identity And Revisions

Session-level strings and full configuration snapshots are written once in the session metadata. Per-frame metadata carries numeric revision references to avoid repeating large strings in the real-time path.

- `frame_id`: monotonic frame identifier within a session.
- `config_revision`: processing and device configuration applied to this frame.
- `optical_state.revision`: laser and EDFA state revision applied to this frame.
- `trigger.sequence`: hardware trigger sequence when available.

Configuration and optical-state history map each revision to the first and last affected frame IDs in the JSON metadata sidecar.

## Processed Frame Unit

One `ProcessedFrame` is the result of processing one full-period `RawFrame` at one scan position. It carries independent up/down FFT magnitude arrays, independently detected peak results, one distance/velocity measurement, and one XYZ point. A peak below threshold is invalid for that A-scan; no value is carried from a previous A-scan.

Scan-line and B-scan arrays are derived immutable snapshots. They are not embedded back into every processed frame.

## Timing And Scan Metadata

- `host_timestamp_ns` is the host monotonic timestamp captured when the frame becomes available.
- `trigger.timestamp_ns` is the hardware trigger timestamp when supported by the board or timing source.
- Trigger jitter and missed-trigger counters are recorded without changing frame identity.
- MCU scan indices and angles are attached through `scan_position`; `valid=false` means the position was unavailable, not zero degrees.

## Storage Rules

- Raw storage always preserves the complete pre-segmentation record.
- Binary files declare format version, sample format, byte order, channel, sample rate, and record length.
- JSON metadata stores the session descriptor, complete configuration snapshot, revision history, device versions, calibration identifiers, and stop reason.
- Queue overflow or raw-writer failure requests an acquisition stop and records the responsible queue and last accepted frame ID.
- Raw files use numbered parts named `<stem>.raw.0000.bin`, `<stem>.raw.0001.bin`, and so on.
- Replay opened at part `0000` automatically continues through compatible numbered parts.
- Processed binary and raw/processed JSON sidecars use the same session/config revision identity as the source raw frame.

## UDP Point Packet V1

Each complete raster frame is assembled from all scan positions and split into one or more UDP datagrams. Invalid measurements are omitted from the point array. Multi-byte fields and IEEE-754 floats use little-endian byte order.

| Offset | Type | Field |
|---:|---|---|
| 0 | char[4] | magic `FMCW` |
| 4 | uint16 | packet format version, currently 1 |
| 6 | uint16 | header bytes, currently 40 |
| 8 | uint64 | one-based raster frame ID |
| 16 | uint64 | source timestamp in ns |
| 24 | uint64 | config revision |
| 32 | uint16 | total segment count |
| 34 | uint16 | zero-based segment index |
| 36 | uint16 | points in this datagram |
| 38 | uint16 | point stride, currently 20 bytes |
| 40 | float[5] x N | x, y, z, intensity, velocity |

The maximum v1 datagram payload is 65,507 bytes, so `packet_point_count` is validated in the range 1..3273. The sender runs on its own worker thread and reports queue use, frames/packets sent, send FPS, dropped frames, and send errors.
