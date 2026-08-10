# Design Document: Charybdis Mini Wireless ZMK Improvements

**Date:** 2026-05-10
**Status:** Proposed - Awaiting Approval
**Based on:** research_zmk_charybdis_updates_2026-05-10.md

---

## Current State Analysis

### What You Have
- **ZMK**: Tracking `main` (unpinned)
- **PMW3610 Driver**: `tsironis/zmk-pmw3610-driver` (efogdev-based fork with PM/IRQ fixes and anti-warp)
- **Board**: `nice_nano//zmk` (already migrated to new naming)
- **Input Processors**: Basic implementation in `charybdis_right.overlay` (snipe + scroll modes)
- **BLE Config**: Experimental features enabled, 0dBm TX power, aggressive connection interval (9)
- **Power**: Deep sleep at 15min, no idle timeout configured
- **Driver compat**: `pixart,pmw3610-alt` (custom compatible to avoid Zephyr conflict)

### What's Working Well
- Board naming is already HWMv2-compatible (`nice_nano//zmk`)
- Battery level proxy/fetching correctly configured for split
- Input processors already in use for scroll/snipe layers
- Anti-warp and ignore-after-rest features active in driver
- Power state downshift chain (RUN -> REST1 -> REST2 -> REST3) well-configured

---

## Proposed Changes

### Change 1: Pin ZMK Version

**Risk:** Low | **Impact:** High (stability)

**Current:**
```yaml
# config/west.yml
- name: zmk
  remote: zmkfirmware
  revision: main
```

**Proposed:**
```yaml
- name: zmk
  remote: zmkfirmware
  revision: v0.4
```

**Also update** `.github/workflows/build.yml`:
```yaml
# Current
uses: zmkfirmware/zmk/.github/workflows/build-user-config.yml@main
# Proposed
uses: zmkfirmware/zmk/.github/workflows/build-user-config.yml@v0.4
```

**Rationale:** Currently tracking `main` means every push could break your build. Pinning to a release gives you control over when to upgrade. The `v0.4` tag will point to the latest patch within that minor version.

**Decision needed:** Do you want to pin now, or wait until v0.4 is officially released? We could also pin to a specific commit hash for maximum stability.

---

### Change 2: Add Idle Timeout (Pre-Deep-Sleep Power Saving)

**Risk:** Low | **Impact:** Medium (battery life)

**Current:** No idle timeout - keyboard jumps from active to deep sleep after 15min.

**Proposed addition to `config/charybdis.conf`:**
```
# Idle mode after 30s inactivity (reduces power before deep sleep)
CONFIG_ZMK_IDLE_TIMEOUT=30000
```

**Rationale:** Idle mode is a lighter power-saving state that activates before deep sleep. It reduces power consumption during brief pauses without the full wake-up cost of deep sleep. The 280Zo reference config uses this same value.

---

### Change 3: Increase Bluetooth TX Power

**Risk:** Low | **Impact:** Medium (connection reliability)

**Current:**
```
CONFIG_BT_CTLR_TX_PWR_0=y   # 0 dBm
```

**Proposed:**
```
CONFIG_BT_CTLR_TX_PWR_PLUS_8=y   # +8 dBm
```

**Rationale:** The 280Zo reference config uses +8dBm. Higher TX power improves connection reliability and range, especially in noisy RF environments. Trades slightly more power consumption for significantly better BLE stability. This is especially valuable for split keyboards where the peripheral half communicates wirelessly.

**Decision needed:** Are you experiencing any BLE connection drops? If yes, this is a clear win. If BLE is stable at 0dBm, you could keep it to save battery.

---

### Change 4: Improve Input Processor Configuration

**Risk:** Low | **Impact:** Medium (trackball feel)

**Current (`charybdis_right.overlay`):**
```dts
trackball_listener {
    compatible = "zmk,input-listener";
    device = <&trackball>;
    
    snipe_mode {
        layers = <7>;
        input-processors = <&zip_xy_scaler 1 3>;
        process-next;
    };
    
    scroll_mode {
        layers = <6>;
        input-processors = <&zip_xy_scaler 1 8>,
                           <&zip_xy_to_scroll_mapper>;
        process-next;
    };
};
```

**Proposed:**
```dts
#include <dt-bindings/zmk/input_transform.h>

trackball_listener {
    compatible = "zmk,input-listener";
    device = <&trackball>;

    /* Base processor: default cursor speed for all layers */
    input-processors = <&zip_xy_scaler 5 5>;

    /* SCROLL mode: Convert trackball to scroll wheel */
    scroll_mode {
        layers = <6>;
        input-processors = <
            &zip_xy_scaler 1 15
            &zip_xy_to_scroll_mapper
            &zip_scroll_transform (INPUT_TRANSFORM_Y_INVERT)
        >;
    };

    /* SNIPE mode: Reduced speed for precision work */
    snipe_mode {
        layers = <7>;
        input-processors = <&zip_xy_scaler 2 6>;
    };
};
```

**Key differences from current:**
1. **Added base `input-processors`**: `<&zip_xy_scaler 5 5>` gives a consistent base speed multiplier on all layers. Currently you have no base scaler, relying solely on driver CPI.
2. **Scroll Y-invert**: Added `&zip_scroll_transform (INPUT_TRANSFORM_Y_INVERT)` - natural scroll direction. Without this, scroll direction may feel inverted.
3. **Adjusted scroll speed**: `1 15` instead of `1 8` for smoother scrolling (less sensitive)
4. **Adjusted snipe speed**: `2 6` (~33%) instead of `1 3` (~33%) - same ratio but may feel smoother with the base scaler
5. **Removed `process-next`** from scroll/snipe - these are exclusive modes, no need to chain to base processors

**Decision needed:** The base scaler and scroll speed values are subjective. Do you want to start with the 280Zo reference values and tune from there, or keep your current feel?

---

### Change 5: Extract Trackball Config to Dedicated File

**Risk:** None | **Impact:** Low (maintainability)

**Current:** Trackball input listener config lives inline in `charybdis_right.overlay`

**Proposed:** Extract to `boards/shields/charybdis/charybdis_pointer.dtsi` and include it:

```dts
// In charybdis_right.overlay, replace the trackball_listener block with:
#include "charybdis_pointer.dtsi"
```

**Rationale:** Follows the 280Zo reference pattern. Separates hardware wiring (overlay) from behavior tuning (pointer dtsi). Makes it easier to adjust trackball feel without touching pin definitions.

---

### Change 6: Add Mouse Emulation Config

**Risk:** Low | **Impact:** Low (feature completeness)

**Proposed addition to `config/charybdis.conf`:**
```
CONFIG_ZMK_MOUSE=y
```

**Rationale:** Explicitly enables mouse emulation behaviors. While your keymap uses `&mkp` (mouse key press) bindings in combos and layers, having this explicitly enabled ensures mouse button behaviors work correctly across ZMK updates.

---

## Changes NOT Recommended

### Switching PMW3610 Driver (from tsironis to badjeff/280Zo)
**Why not:** Your tsironis fork (efogdev-based) has specific PM/IRQ fixes, anti-warp, and ignore-after-rest features that are tailored to your hardware. The config names differ (`CONFIG_PMW3610_*` vs `CONFIG_PMW3610_ALT_*`), and the compatible string differs (`pixart,pmw3610` vs `pixart,pmw3610-alt`). Switching drivers would require:
- Renaming all Kconfig options
- Updating compatible string
- Testing that PM/IRQ fixes work or are no longer needed
- Risk of regression on wake-warp and tracking issues

This should only be considered if you're experiencing specific trackball issues that the badjeff driver solves and yours doesn't.

### Changing Connection Interval
**Why not:** Your `CONFIG_BT_PERIPHERAL_PREF_MAX_INT=9` (11.25ms) is aggressive but good for low-latency trackball use. Increasing it would save minimal battery but add noticeable latency to cursor movement.

### Adding Dongle Support
**Why not:** Requires additional hardware (dongle board) and significant config restructuring. Not a config-level change.

---

## Implementation Order

| Step | Change | Files Modified | Risk |
|------|--------|----------------|------|
| 1 | Add idle timeout | `config/charybdis.conf` | Low |
| 2 | Add mouse emulation config | `config/charybdis.conf` | Low |
| 3 | Extract pointer dtsi | New: `boards/shields/charybdis/charybdis_pointer.dtsi`, Edit: `charybdis_right.overlay` | None |
| 4 | Improve input processors | `boards/shields/charybdis/charybdis_pointer.dtsi` | Low |
| 5 | Increase BLE TX power | `config/charybdis.conf` | Low |
| 6 | Pin ZMK version | `config/west.yml`, `.github/workflows/build.yml` | Low |

Steps 1-2 can be done together. Step 3-4 together. Steps 5-6 each independent.

---

## Files Changed Summary

| File | Action | Description |
|------|--------|-------------|
| `config/charybdis.conf` | Edit | Add idle timeout, mouse config, TX power |
| `config/west.yml` | Edit | Pin ZMK version |
| `.github/workflows/build.yml` | Edit | Pin workflow version |
| `boards/shields/charybdis/charybdis_pointer.dtsi` | **New** | Extracted trackball input processor config |
| `boards/shields/charybdis/charybdis_right.overlay` | Edit | Include pointer dtsi, remove inline listener |
