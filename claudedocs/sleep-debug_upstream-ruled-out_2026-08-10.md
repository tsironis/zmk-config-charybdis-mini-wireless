# Sleep deadlock: upstream Zephyr PM bug ruled out

**Date:** 2026-08-10
**Branch:** `sleep-debug-instrumented`
**Status:** negative result — do not re-derive

---

## Summary

Upstream ZMK issue [#3207](https://github.com/zmkfirmware/zmk/issues/3207) describes symptoms
nearly identical to ours: on a split keyboard, the **central half fails to wake from deep sleep**
and drains battery (10–15%/day vs ~1%/day), present on `main` but not on v0.3.

**It is not our bug.** The upstream fix was already present in every build in `build/sleep-debug/`.

## The upstream bug and its fix

Root cause was a Zephyr commit (`pm: only define slots if CONFIG_PM is enabled`,
`2ebb8fad`) that gated `Z_PM_DEVICE_DEFINE_SLOT` behind `CONFIG_PM`. ZMK does not set
`CONFIG_PM`, so devices suspended and were never resumed — the board "always locks out".

Pete Johanson reverted it on **2026-06-24**:
[`1d1eb5d`](https://github.com/zmkfirmware/zephyr/commit/1d1eb5d9645bbfbe437413ddba2b779e5c7b60b3)
on the `v4.1.0+zmk-fixes` branch.

## Why our builds already have it

Both `zmkfirmware/zmk` and our fork `tsironis/zmk@charybdis-sleep-debug` declare zephyr in
`app/west.yml` as `revision: v4.1.0+zmk-fixes` — a **branch**, not a pinned SHA. The ZMK
reusable CI workflow restores an `actions/cache` of the west modules, but `west update` still
re-fetches and hard-resets branch-tracked projects on every run, so the cache does not pin it.

Verified in the CI logs of the earliest and latest July runs:

```
$ gh run view 29732305578 --log | grep -E "zephyr: fetching|HEAD is now at 9df4b12b"
--- zephyr: fetching, need revision v4.1.0+zmk-fixes
HEAD is now at 9df4b12b drivers: display: ls0xx: fix releasing SPI bus too soon
```

Same for run `30552328744` (2026-07-30, the `14-sentinel-v9` build). `9df4b12b` is dated
2026-06-28 — two commits past the revert.

**Therefore experiments 1–14 all ran against patched Zephyr.** The remaining deadlock is ours.

## What this leaves

The largest axis never varied across experiments 1–14 is our own PMW3610 fork's
power-management path. `tsironis/zmk-pmw3610-driver` diverges from badjeff upstream there:

| SHA | Date | Message |
|-----|------|---------|
| `b8ecaea` | 2026-04-14 17:13 | Rename compatible to `pixart,pmw3610-alt` to avoid Zephyr conflict |
| `b447408` | 2026-04-14 09:09 | **Fix wake warp: disable IRQ on sleep, wire PM handler, enable anti-warp** |
| `42e0a85` | 2026-04-09 | chore: readme |
| `564cc7f` | 2025-08-31 | feat: PM support |

`b447408` disables the trackball IRQ in `on_activity_state()` before entering SLEEP, removing the
IRQ pin as a System OFF wake source, and moves `PM_DEVICE_DT_INST_DEFINE` inside the
`PMW3610_DEFINE` macro. That is squarely on the sleep-entry path we have been instrumenting.

Experiment 2 (`2-no-trackball`, commit `2ce5396`) disabled the sensor **entirely**, which conflates
"our driver PM code runs" with "sensor exists at all". No experiment has run with the trackball
present but our PM changes absent.

Also unadopted: badjeff's `2025-12-31` fix "pmw3610_set_performance() not being called before
sleep". Our efogdev-derived fork predates it and has no obvious equivalent.

### Wrinkle for the bisect

`boards/shields/charybdis/charybdis_right.overlay:57` binds `compatible = "pixart,pmw3610-alt"`,
and that compatible string only exists as of `b8ecaea` — *after* `b447408`. So pinning
`config/west.yml` back to `42e0a85` would break the DT binding, and reverting the overlay to
`pixart,pmw3610` risks colliding with Zephyr 4.1's in-tree PMW3610 driver (the exact reason for
the rename). The clean isolation is a driver-fork branch = `b8ecaea` with `b447408` reverted,
pinned from `config/west.yml`.

## Also worth correcting

The latest upstream ZMK tag is still **v0.3.0** (2025-08-01). There is no v0.4 or v0.4.1 —
v0.4 will be the first release carrying Zephyr 4.1 and is unreleased as of 2026-08-10.
`research_zmk_charybdis_updates_2026-05-10.md` claimed "ZMK v0.4.1 stable"; that was wrong.

## Sources

- [ZMK issue #3207](https://github.com/zmkfirmware/zmk/issues/3207)
- [Zephyr revert 1d1eb5d](https://github.com/zmkfirmware/zephyr/commit/1d1eb5d9645bbfbe437413ddba2b779e5c7b60b3)
- [ZMK releases](https://github.com/zmkfirmware/zmk/releases)
- [badjeff/zmk-pmw3610-driver](https://github.com/badjeff/zmk-pmw3610-driver)
