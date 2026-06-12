# Window Position Estimation — Design

**Date:** 2026-06-13
**Status:** Approved for spec review
**Branch:** `feature/window-position-estimation`

## Context

The controller drives a 433 MHz RC window motor by replaying captured `open`/`close`
codes. Two problems surfaced during commissioning:

1. **Unreliable travel.** A replayed command sometimes only nudges the window a
   little instead of completing its travel — the motor doesn't reliably latch
   onto a single marginal RF burst.
2. **Poor Apple Home fit.** The accessory is a `Service::Window` whose slider is
   snapped to 0/100 (`setRange(0,100,100)`) with no live movement, so the Home
   widget can't represent position and feels broken.

### Motor behavior (confirmed with the user)

- Only **OPEN** and **CLOSE** commands exist (no dedicated stop code).
- A single command **latches**: the window runs to its full end (open or closed)
  on its own. **Holding/repeating the same command does not interrupt it.**
- Sending the **opposite** command while moving **STOPS** the window where it is
  (it does *not* reverse).
- From a **positional stop** (resting anywhere), the **next** command decides the
  direction of travel.
- Full travel times are **asymmetric**: open = **23.5 s**, close = **27.0 s**.

These rules make true intermediate positioning possible: drive in a direction,
then send the opposite command as a "stop" when a timed estimate reaches the
target. Hitting a physical end requires no stop and self-corrects drift.

## Goals

- Estimate window position (0–100%) by timed dead-reckoning against calibrated
  travel durations, with active stop-at-target for intermediate positions.
- Make the Apple Home widget accurate: live animated `CurrentPosition`, correct
  `PositionState`, and a genuinely honored `TargetPosition`.
- Expose position + control + calibration in the web portal.
- Mirror position to MQTT / Home Assistant (a real positionable cover).
- Make travel reliable (fix issue 1).

## Non-goals

- Closed-loop control with real position feedback (no sensor exists).
- Sub-percent accuracy. The ends self-calibrate; intermediate stops carry the
  inherent timing error of an open-loop estimate.

## Architecture

### New module: `WindowPosition` (replaces `WindowSim`)

Single owner of window intent and state. Depends on `Codes` (look up the
`open`/`close` codes), `RFTransmitter` (send bursts), `Storage` (calibration +
last position). Notifies sinks (HomeKit, MQTT, portal) on change, throttled.

**State**

| Field | Meaning |
|---|---|
| `positionPct` | estimated position, float 0 (closed) … 100 (open) |
| `motion` | `Idle` \| `Opening` \| `Closing` |
| `targetPct` | desired position, or NONE |
| `motionStartMs`, `motionStartPct` | reference for the estimator |
| `lastSendMs` | for re-assertion throttling |
| `lastStopMs` | settle guard before a direction change |

**Config (NVS, calibratable)**

| Key | Default | Meaning |
|---|---|---|
| `openMs` | 23500 | full close→open travel time |
| `closeMs` | 27000 | full open→close travel time |
| `stopLeadMs` | 800 | fire the stop this early to compensate for RF burst + motor latency |

**Pure functions (unit-tested host-side)**

```
estimatePct(startPct, elapsedMs, motion, openMs, closeMs) -> pct      // clamped [0,100]
decideStep(pos, target, motion, leadPct, deadband, settleOk) -> Action
   // Action ∈ { None, SendOpen+Opening, SendClose+Closing,
   //            StopViaClose+Idle, StopViaOpen+Idle }
```

`leadPct` converts `stopLeadMs` into position units for the active direction
(`stopLeadMs / durationMs * 100`). `deadband` (~1.5%) prevents hunting.

**Behavior (`tick()` each loop, work throttled)**

1. Recompute `positionPct` from `motion` + elapsed.
2. Drive toward `targetPct`:
   - **Idle + target beyond deadband:** issue the directional burst toward the
     target (`target>pos` → OPEN/Opening, else CLOSE/Closing). This is the fresh
     command from rest — valid per the confirmed motor rules.
   - **Moving toward a full end (target 0 or 100):** no stop; let the motor reach
     its physical limit, then clamp + go Idle. This **recalibrates** position to
     exactly 0/100 and erases accumulated drift.
   - **Moving toward an intermediate target:** when `pos` reaches
     `target ∓ leadPct`, send the **opposite** command to stop; go Idle; set
     `positionPct ≈ target`.
   - **Retarget that needs the other direction** (e.g. Opening but `target<pos`):
     send stop now → Idle, record `lastStopMs`; after a ~1 s settle, the Idle
     branch issues the new directional command (stop and move are separate
     presses, matching the motor).
3. **Re-assertion (issue-1 fix):** while moving and still well short of the
   stop point, re-send the *same* direction every ~4 s (harmless — same
   direction never interrupts). Suspended once within the stop-lead window.
4. Notify sinks (HomeKit / MQTT / portal) at most every ~500 ms and on every
   state transition. Persist `positionPct` + motion-end to NVS when going Idle.

**Public API**

```
begin();                 // load config + last position, show LED
tick();                  // called from loop()
goTo(uint8_t pct);       // set target; tick() drives the motor
openFull();  closeFull(); // goTo(100) / goTo(0)
stop();                  // halt at current position now
noteExternalCommand(dir);// sniffer replayed open/close directly — track without re-sending RF
position(); motionState(); target();  // getters for sinks
setCalibration(openMs, closeMs, stopLeadMs);
```

### RF transmission reliability

- Start motion with a robust sync-framed burst (the existing framed code; repeat
  count tuned up so a single press reliably latches).
- Re-assert the same direction every ~4 s during travel toward an end.
- Send a solid burst for the stop command; suspend re-assertion first so the
  stop isn't starved in the transmit queue.

### Apple Home (HomeSpan)

- Keep `Service::Window`; change `TargetPosition` range to `setRange(0,100,1)`.
- `TargetPosition::update()` → `WindowPosition::goTo(target)`.
- `CurrentPosition` ← `positionPct` (throttled); `PositionState` ←
  Opening=1 (increasing) / Closing=0 (decreasing) / Idle=2 (stopped).
- On reaching Idle, set `TargetPosition` = `CurrentPosition` so the slider settles.
- `HomeKit::notifyState(bool)` becomes `HomeKit::notifyPosition(uint8_t pct, motion)`.

### Web portal

New **Window** tab/section:

- Animated 0–100% position bar + state label (Opening / Closing / Open / Closed /
  Partly open NN%).
- **Target slider** → POST go-to-% on release (debounced).
- **Open**, **Close**, **Stop** buttons.
- **Calibration** form: open seconds, close seconds, stop-lead ms → save.

API (new):

| Method/Path | Body | Action |
|---|---|---|
| `GET /api/window` | — | `{pos, motion, target, openMs, closeMs, stopLeadMs}` |
| `POST /api/window` | `{action:"goto", value:NN}` / `{action:"open"\|"close"\|"stop"}` | drive |
| `POST /api/window/calibrate` | `{openMs, closeMs, stopLeadMs}` | persist config |

Live updates: new WebSocket message `{type:"window", pos, motion, target}`,
broadcast on change (throttled).

### MQTT / Home Assistant

Extend the existing cover discovery into a positionable cover:

- Add `pos_t` (position state topic) publishing `0..100`, `pos_open=100`,
  `pos_clsd=0`.
- Add `set_pos_t` (set-position command topic) → `WindowPosition::goTo(pct)`.
- Keep `cmd_t` with `OPEN`/`CLOSE`/`STOP` → `openFull`/`closeFull`/`stop`.
- Publish position on change (throttled), retained.

### Storage

Add getters/setters for `openMs`, `closeMs`, `stopLeadMs` (with defaults) and
persistence of `lastPositionPct`.

### Integration & cleanup

- Remove `WindowSim`; `WindowPosition` takes over the LED (green=open, red=closed,
  **amber=resting partway**, blue=moving).
- All intent paths funnel through `WindowPosition`: HomeKit `update()`, MQTT
  `cmd/cover` + `set_position`, portal `/api/window`.
- Decouple state inference from `RFTransmitter::loop` (it no longer pokes window
  state). Instead, `Portal`'s `/api/replay` calls `noteExternalCommand` when the
  replayed code is named `open`/`close`, so a manual sniffer replay still updates
  the model — without the model double-sending its own bursts.
- `main.cpp`: `WindowPosition::begin()` in `setup()`, `WindowPosition::tick()` in
  `loop()`.

## Edge cases & error handling

- `goTo` within deadband of current position → no-op.
- `goTo` while moving the same direction further → keep moving, move the stop point.
- `goTo` requiring the opposite direction → stop, settle ~1 s, then re-issue.
- `open`/`close` code missing in `Codes` → log and ignore the command.
- Reboot mid-travel → resume Idle at the persisted position (best effort); the
  next full open/close recalibrates to a physical end.
- Transmit queue: re-assertion throttled to ~4 s; suspend it before a stop so the
  stop burst isn't delayed.

## Testing

- **Host-side unit tests** (PlatformIO `native` env) for the pure functions:
  - `estimatePct`: open-from-0, partial, asymmetric durations, clamping at both ends.
  - `decideStep`: go-to-% up and down from rest, full-end run-to-limit (no stop),
    intermediate stop at `target − leadPct`, mid-travel retarget requiring the
    opposite direction (stop → settle → move), deadband no-op.
- **On-device verification** with the user: animation tracks reality, full
  open/close reliability (issue 1), intermediate stop accuracy → tune
  `stopLeadMs`, reversal/retarget, calibration form, HomeKit + HA position.

## Open questions / future

- `stopLeadMs` default (800 ms) is a starting guess; finalized by on-device
  measurement.
- If intermediate stop accuracy proves poor, a future option is measuring per-
  direction latency separately rather than a single lead constant.
