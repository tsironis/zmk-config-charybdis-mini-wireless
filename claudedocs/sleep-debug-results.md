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
| 15b | `15b-driver-pm-only` | `a68032f` | `ae9bf15` | driver pinned to `dcff8f6` — PM registration + PM-path IRQ removed, rate-limiting kept | froze during use; evidence lost to pin reset |
| 15c | `15c-live-capture` | `f7bf916` | `ae9bf15` | 15b + `&out` bindings, tethered live capture on BLE | **decisive — see post-mortem below** |

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

## Experiment 15c — live capture (2026-08-13)

Tethered central on USB with HID forced to BLE via `&out OUT_BLE`, `<dbg>` logging to file for
~2 h 23 m. Froze during typing. This run answered more than 6–15 combined.

### Retention is dead on this board — post-mortem instrumentation cannot work

A boot with `RESETREAS=0x00010002` (`OFF | DOG`) — i.e. the watchdog reset the SoC *itself*, the
exact self-initiated reset the breadcrumb design was waiting for — still reported
`sleep_bc=0x00000000`, `wdt_ev=0x00000000`, `GPREGRET=0x00` instead of the `0xDD` sentinel.
So it is not "the WDT callback never ran": **nothing survives a reset here**, presumably the
nice!nano bootloader clearing RAM and GPREGRET. Experiments 6–15 were structurally incapable of
reporting. Only live capture works.

RESETREAS itself *is* trustworthy — 15b produced a clean single-bit `RESETPIN`, proving the
write-1-to-clear in `sleep_debug_init` works.

### The wake-from-sleep hang is real, and the watchdog rescues it

`OFF | DOG` on two consecutive boots. Init order: `activity_init` arms the WDT at
`APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY` (90); `sleep_debug_init` clears RESETREAS at 99.
For both bits to survive together, that boot never reached 99. So: **GPIO wake from System OFF →
init hangs → watchdog fires 30 s later and recovers.** Nordic confirms System OFF stops the WDT,
so the watchdog cannot be *causing* the sleep failure — only rescuing it. This is the original bug
and it remains open.

### The freeze-during-use is NOT a firmware fault

Evidence, all from the 152k-line capture:

- `pmw3610: PM:` shows only `0↔1` (ACTIVE↔IDLE). **Never `→ 2`.** The sleep path was not involved.
- `zmk_ble_active_profile_conn()` (`app/src/ble.c:341-353`) logs at WRN on both NULL paths, and
  both strings contain "profile" — the grep found none. **`conn` was never NULL**, so the
  `hog.c:419` silent-drop path was never taken.
- No `Error notifying` → `bt_gatt_notify_cb` returned 0 for every report.
- No `queue full` / `Failed to queue`; events 12–17 ms apart, so the msgq drained.
- No disconnect logged; endpoint stayed `BLE:0` from 00:09:32 onward.
- ~200 consecutive mouse reports over 3 s with zero buffer errors → ACL TX buffers were still
  recycling → **the peer was ACKing at the link layer.**

Conclusion: the firmware delivered every HID report over a healthy, acknowledging connection and
macOS stopped acting on them. Host-side HID stack wedge, downstream of everything the
`charybdis.conf:8-12` workqueue/buffer tuning addresses. Not a ZMK deadlock.

Open confirmations: `grep -c "Not sending"` should be 0; next freeze, toggle Bluetooth off/on on
the Mac without touching the keyboard; reproduce against a second host.

### Consequence for the branch

The freeze and the sleep hang are **two different bugs**. The driver PM bisect (15/15b) was aimed
at the freeze and is inconclusive for it, since the freeze is host-side. The sleep hang is still
unexplained and is the one worth pursuing — via live capture across a sleep cycle, not breadcrumbs.

## Protocol

1. Push branch, `gh run watch`, download artifacts into `build/sleep-debug/<n>-<name>/`.
2. Confirm zephyr SHA held: `gh run view <id> --log | grep "need revision v4.1.0"`.
3. Flash both halves, attach USB CDC console to central, idle past `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT`,
   attempt wake.
4. Pass = central wakes, heartbeat resumes, no watchdog reset. Fail = read RESETREAS +
   GPREGRET/GPREGRET2 sentinel to separate watchdog-fired from retention-lost.
5. Overnight battery delta: ~1%/day normal, 10–15%/day is the stuck-awake signature.
6. **Write the outcome into the table above before starting the next experiment.**
