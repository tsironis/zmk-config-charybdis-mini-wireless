# Sleep-debug experiment log

Running record of every `build/sleep-debug/` variant: what it changed, and what it did on hardware.

**Outcomes for experiments 1–14 were never written down and are now lost.** Reconstructed columns
below come from commit messages; the Outcome column is unknown except where noted. Fill it in as
experiments are re-run.

## Fixed context

- Symptom: central half does not wake from deep sleep; requires reset.
- Zephyr is `v4.1.0+zmk-fixes` @ `9df4b12b` in **all** builds — includes the 2026-06-24 revert of
  the `CONFIG_PM` slot-gating bug, so upstream [#3207](https://github.com/zmkfirmware/zmk/issues/3207)
  is ruled out. See `sleep-debug_upstream-ruled-out_2026-08-10.md`.
- ZMK fork `tsironis/zmk@charybdis-sleep-debug` is upstream `ead91ca` (2026-05-04) + `4dd0362`
  ("fix(split): use our managed queue for input report to avoid deadlock") + instrumentation.

## Experiments

| # | Dir | Config SHA | Fork SHA | What varied | Outcome |
|---|-----|-----------|----------|-------------|---------|
| 1 | `1-baseline` | `5b057fe` | — | 60 s sleep timeout for fast repro | unknown |
| 1b | `1b-baseline-120s` | `a2765d5` | — | 120 s timeout so sensor is in REST3 at sleep entry | unknown |
| 2 | `2-no-trackball` | `2ce5396` | — | PMW3610 disabled entirely | **unknown — decisive, re-test** |
| 3 | `3-stock-conf` | `98f795b` | — | BLE/workqueue tuning block removed | unknown |
| 4 | `4-stock-zmk` | `9e64abd` | — | stock upstream ZMK main (no fork) | unknown |
| 5 | `5-instrumented` | `bae5e27` | — | USB CDC logging via `zmk-usb-logging` snippet | unknown |
| 6 | `6-breadcrumbs` | `8fc1aec` | `5a5ec64` | noinit breadcrumbs across sleep entry | unknown |
| 7 | `7-breadcrumbs-v2` | `8fc1aec` | `4a8ca8c` | re-log previous-boot breadcrumb every 30 s | unknown |
| 8 | `8-wdt-v3` | `b54df4c` | `e0ae0a8` | watchdog auto-recovery + deadlock evidence | unknown |
| 9 | `9-wdt-v4` | `b54df4c` | `77c186b` | decouple noinit validity checks per-struct | unknown |
| 10 | `10-wdt-v5` | `b54df4c` | `946e9d4` | dump raw noinit magic unconditionally | unknown |
| 11 | `11-gpregret-v6` | `b54df4c` | `d1d450` | GPREGRET as reset-proof sleep-entry stage marker | unknown |
| 12 | `12-culprit-v7` | `b54df4c` | `fc84cc9` | identify culprit thread via GPREGRET2 | unknown |
| 13 | `13-culprit-v8` | `b54df4c` | `a53d0d7` | write GPREGRET2 first in the WDT callback | unknown |
| 14 | `14-sentinel-v9` | `b54df4c` | `ae9bf15` | 0xDD/0xEE sentinel: retention-lost vs callback-never-fired | unknown |
| 15 | `15-driver-pm-bisect` | `1fc25a1` | `ae9bf15` | PMW3610 driver pinned to `0df27a9` — **full** revert of `b447408` | **CONFOUNDED — froze during use, evidence lost to power-cycle** |
| 15b | `15b-driver-pm-only` | `a68032f` | `ae9bf15` | driver pinned to `dcff8f6` — PM registration + PM-path IRQ removed, rate-limiting kept | pending |

Experiment 15 built as CI run `31367312168`; pins verified in the log (zephyr `9df4b12b`,
driver `0df27a9`). Artifacts in `build/sleep-debug/15-driver-pm-bisect/firmware-1fc25a1/`.
The reverted driver compiles — the file-scope `PM_DEVICE_DT_INST_DEFINE(n, …)` with unbound `n`
is accepted, which is consistent with it having shipped that way for months.

## Experiment 15 — the current hypothesis

Driver pinned to `tsironis/zmk-pmw3610-driver@0df27a9` = `b8ecaea` with `b447408` reverted.
Keeps the `pixart,pmw3610-alt` compatible rename so `charybdis_right.overlay:57` still binds.

`b447408` (2026-04-14) did three things on the sleep path:

1. Moved `PM_DEVICE_DT_INST_DEFINE` inside `PMW3610_DEFINE` and passed `PM_DEVICE_DT_INST_GET(n)`
   to `DEVICE_DT_INST_DEFINE`. **Before this, the PM handler was dead** — `PM_DEVICE_DT_INST_DEFINE(n, …)`
   sat at file scope with an unbound `n`, and the device was registered with a `NULL` pm_device.
   After it, the driver participates in Zephyr device PM for the first time, so ZMK's deep-sleep
   device-suspend walk now calls into it.
2. Dropped the `enable_pm_support && rst_gpio.port` early-return in `pmw3610_pm_action`, so SUSPEND
   always runs `pmw3610_set_interrupt(dev, false)` (GPIO-only, no SPI).
3. Added `pmw3610_set_interrupt(devs[i], false)` before `pmw3610_shutdown()` in `on_activity_state()`.
   `pmw3610_shutdown()` does an **SPI write** (`pmw3610_write_reg`) on the sleep-entry path.

Item 1 is the prime suspect: it is the only change that makes this driver visible to device PM, and
that is exactly the machinery upstream #3207 turned out to hinge on. Item 3 is the secondary
suspect — an SPI transaction racing the deep-sleep transition can block on the SPIM semaphore.

**Read:** clean sleep/wake ⇒ our PM code is the cause, bisect items 1/2/3 individually.
Still deadlocks ⇒ driver exonerated; next axis is split BLE / central role, and experiment 2
becomes the one to re-run.

## Experiment 15 post-mortem (2026-08-11)

Froze **during active use**, not during sleep. Console after reboot showed a cold boot with no
evidence: `RESETREAS=0x00000000` (no bits — power-on reset, not `RESETPIN` or `DOG`),
`sleep_bc=0x00000000 wdt_ev=0x00000000` (noinit zeroed), `boot #1`, `GPREGRET=0x00` (not the
`0xDD` sentinel). The board was power-cycled, which is the one reset type that clears GPREGRET
and noinit RAM.

Two things still follow:

1. **The revert was too broad.** `b447408` bundled four changes; `0df27a9` reverted all of them,
   including "restore report interval rate-limiting for BLE HID queue management".
   `charybdis_right.conf:18` sets `CONFIG_PMW3610_REPORT_INTERVAL_MIN=12`, but experiment 15 has
   no code reading it — so the trackball reported at full interrupt rate into the BLE HID queue.
   That is the flooding path `charybdis.conf:8-12` blames for
   `bt_l2cap_create_pdu_timeout(K_FOREVER)`. The freeze is plausibly self-inflicted; the run
   tells us nothing about sleep. Experiment 15b narrows the change to PM only.
2. **The watchdog did not fire.** Recovery required manual intervention. If the fork's WDT is
   armed and fed from the system workqueue as `charybdis.conf:44-46` describes, the system WQ was
   still running, so this wedge is not system-WQ starvation — it points at the BLE TX / L2CAP
   path. *Unverified:* that the WDT is actually armed at boot. Confirm before leaning on this.

**On freeze, press the reset button once — do not pull power.** GPREGRET and noinit RAM survive a
pin reset; neither survives a POR. Avoid double-tapping (that enters the bootloader).

## Protocol

1. Push branch, `gh run watch`, download artifacts into `build/sleep-debug/<n>-<name>/`.
2. Confirm zephyr SHA held: `gh run view <id> --log | grep "need revision v4.1.0"`.
3. Flash both halves, attach USB CDC console to central, idle past `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT`,
   attempt wake.
4. Pass = central wakes, heartbeat resumes, no watchdog reset. Fail = read RESETREAS +
   GPREGRET/GPREGRET2 sentinel to separate watchdog-fired from retention-lost.
5. Overnight battery delta: ~1%/day normal, 10–15%/day is the stuck-awake signature.
6. **Write the outcome into the table above before starting the next experiment.**
