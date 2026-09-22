# Supported AlazarTech Models

This application intentionally supports only the ATS-SDK 25.1.0 models that meet
both requirements below:

1. 12-bit ADC sample format.
2. An SDK C `NPT_Scan` example using `AUX_IN_TRIGGER_ENABLE`.

The resulting model list is:

| Model | SDK kind | External trigger range | FIFO-only flag | Record rule |
|---|---:|---|---|---|
| ATS9120 | 32 | TTL | enabled | min 256, multiple of 32 |
| ATS9130 | 34 | TTL | enabled | min 256, multiple of 32 |
| ATS9350 | 14 | 5 V | disabled | min 256, multiple of 32 |
| ATS9351 | 18 | 5 V | disabled | min 256, multiple of 32 |
| ATS9352 | 35 | 5 V | disabled | min 256, multiple of 32 |
| ATS9353 | 44 | 5 V | disabled | min 256, multiple of 32 |
| ATS9360 | 25 | TTL | enabled | min 256, multiple of 128 |
| ATS9362 | 58 | TTL | disabled | min 256, multiple of 128 |
| ATS9364 | 53 | TTL | disabled | min 256, multiple of 128 |
| ATS9371 | 33 | TTL | enabled | min 256, multiple of 128 |
| ATS9373 | 29 | TTL | enabled | min 256, multiple of 128 |

Common acquisition contract:

- System ID and Board ID are fixed to `1 / 1`.
- One channel is acquired at a time: channel A or channel B.
- Samples use the native 12-bit unsigned offset-binary value left-aligned in a
  16-bit DMA word.
- Analog input coupling is DC and impedance is 50 ohm.
- `TRIG IN` uses SDK level code `150`; the selected model determines `ETR_TTL`
  or `ETR_5V`.
- `AUX IN` uses `AUX_IN_TRIGGER_ENABLE` with positive slope as the B-scan gate.
- One UP trigger captures one full UP/DOWN chirp record.
- `ADMA_FIFO_ONLY_STREAMING` follows the model's SDK `NPT_Scan` example rather
  than a user-facing switch.

## Sampling Rates

- ATS9120: 10, 20, 50, 100, 200, 500 kS/s; 1, 2, 5, 10, 20 MS/s.
- ATS9130: 10, 20, 50, 100, 200, 500 kS/s; 1, 2, 5, 10, 25, 50 MS/s.
- ATS9350/51/52/53: 1, 2, 5, 10, 20, 50, 100, 200, 500 kS/s;
  1, 2, 5, 10, 20, 50, 100, 125, 250, 500 MS/s.
- ATS9360: 1, 2, 5, 10, 20, 50, 100, 200, 500 kS/s;
  1, 2, 5, 10, 20, 50, 100, 200, 500, 800 MS/s;
  1, 1.2, 1.5, 1.8 GS/s.
- ATS9362: 1, 2, 5, 10, 20, 50, 100, 200, 500, 750 MS/s.
- ATS9364: 1, 2, 5, 10, 20, 50, 100, 200, 500, 800 MS/s; 1 GS/s.
- ATS9371: 1, 2, 5, 10, 20, 50, 100, 200, 500 kS/s;
  1, 2, 5, 10, 20, 50, 100, 200, 500, 800 MS/s; 1 GS/s.
- ATS9373: ATS9371 rates plus 1.2, 1.5, 2, 2.4, 3, 3.6, and 4 GS/s.

## Input And Timing Constraints

- ATS9120/9130 input ranges: +/-40, 50, 80, 100, 200, 400, 500,
  800 mV; +/-1, 2, 4 V.
- ATS9350/9352 input ranges: +/-40, 100, 200, 400 mV; +/-1, 2, 4 V.
- ATS9351/9353/9360/9362/9364/9371/9373 input range: +/-400 mV.
- ATS9120/9130/9350/9351/9352/9353: pre-trigger alignment 32,
  maximum NPT pre-trigger 4080, single-channel trigger-delay alignment 8.
- ATS9360/9362/9364/9371/9373: pre-trigger alignment 128,
  maximum NPT pre-trigger 8176, single-channel trigger-delay alignment 16.

## UI And Connection Behavior

The existing `Board model` combo box shows only model numbers. Changing it
repopulates sampling rate, input range, record alignment, pre-trigger, trigger
delay, trigger range, and AutoDMA setup from the shared capability catalog.
There is no separate board diagnostic page.

At Connect, `AlazarGetBoardKind()` must match the selected model. Unknown models
and selection/hardware mismatches stop before capture and return an operator
error. Supporting another board therefore requires an explicit catalog entry
and acceptance evidence; a nominally compatible bit depth is not enough.
