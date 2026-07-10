# Data Contract

This document defines the data boundary shared by acquisition, processing, storage, replay, UDP, and UI code.

## Raw Frame Unit

One `RawFrame` means:

- one selected digitizer channel, A or B;
- one external trigger emitted at the start of the up chirp;
- one complete captured laser period containing both the up and down chirps;
- one record before software chirp segmentation.

An Alazar DMA buffer may contain multiple raw frames. A scan line and a complete B-scan are higher-level groups of raw frames; they are not raw frames themselves.

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
