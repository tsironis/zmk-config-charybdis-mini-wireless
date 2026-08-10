# Research Report: ZMK & Charybdis Mini Wireless Updates

**Date:** 2026-05-10
**Scope:** ZMK firmware updates, Charybdis Mini Wireless ZMK configuration improvements, PMW3610 driver enhancements, BLE/power optimization
**Confidence:** High (based on official docs, GitHub repos, and community sources)

---

## Executive Summary

Your current configuration is functional but has several areas for improvement. The ZMK ecosystem has evolved significantly since your last updates, with the Zephyr 4.1 migration being the biggest change. Key opportunities include: adopting ZMK version pinning, migrating to the latest input processor system for trackball config, updating board naming conventions, and potentially switching to the 280Zo reference implementation's approach for input processors.

---

## 1. ZMK Firmware Major Updates

### Zephyr 4.1 Migration (Dec 2025)
ZMK's `main` branch now runs **Zephyr 4.1** (up from 3.5). This is a significant jump.

**Key changes:**
- **Hardware Model V2 (HWMv2):** Better multi-core SoC support (nRF5340)
- **Board naming:** All ZMK boards now require the `zmk` variant suffix
  - `nice_nano_v2` -> `nice_nano//zmk` (v2 is now the default revision)
  - Your `build.yaml` already uses `nice_nano//zmk` - **you're good here**
- **New drivers:** nPM1300 and others
- **LVGL 9.3.0:** Breaking API changes for displays
- **Cirque Pinnacle trackpad:** Migrated to upstream Zephyr (`dr-gpios` -> `data-ready-gpios`)

**Impact on your config:** Moderate. Your `build.yaml` board naming is already correct. The main impact is ensuring your PMW3610 driver fork is compatible with Zephyr 4.1.

### ZMK Version Pinning (June 2025)
ZMK now supports **semantic versioning** with pinned versions.

**Current state of your `west.yml`:**
```yaml
revision: main  # <-- tracks bleeding edge
```

**Recommendation:** Pin to a stable release for stability:
```yaml
revision: v0.3.0
```

> **Correction (2026-08-10):** `v0.3.0` (2025-08-01) is still the latest upstream tag. `v0.4` does
> not exist yet — it will be the first release carrying Zephyr 4.1. Note that `v0.3.0` predates the
> Zephyr 4.1 migration, so pinning to it means giving up Zephyr 4.1.

Also update `.github/workflows/build.yml` to match (replace `@main` with `@v0.4`).

### ZMK Studio (In Development)
- Runtime keymap editing over USB/BLE (like Via/Vial for QMK)
- Available at https://zmk.studio/ for Chrome/Edge
- Native apps for Linux, macOS, Windows
- Still incomplete - no firm release estimate
- Useful but not critical for your setup

### Upcoming: ZMK v0.4
- First version with Zephyr 4.1
- Will transition from deprecated `kscan` APIs to newer matrix input API
- New USB stack with improved high-speed support

---

## 2. Charybdis Mini Wireless ZMK Updates

### Reference Implementation: 280Zo/charybdis-wireless-mini-zmk-firmware
The most actively maintained Charybdis Mini ZMK config (latest release **v1.0.2**, April 2026).

**Key features your config is missing:**
1. **Input processor system for trackball:** Uses `charybdis_pointer.dtsi` with ZMK's modular input processor system for scroll/snipe modes
2. **Dongle support:** Bluetooth and USB dongle builds (including Prospector screen dongles)
3. ~~**Latest ZMK v0.4.1 stable release**~~ — incorrect, see correction above; no v0.4.x exists
4. **Cursor jump prevention patch** for PMW3610 on wake

**Input processor approach (recommended pattern):**
```dts
&trackpad_listener {
    input-processors = <&zip_xy_scaler 3 2>;
    
    scroller {
        layers = <SCROLL>;
        input-processors = <&zip_xy_to_scroll_mapper>;
        process-next;
    };
    
    sniper {
        layers = <SNIPE>;
        input-processors = <&zip_xy_scaler 1 4>;  // slower for precision
    };
};
```

This replaces driver-level scroll/snipe mode configuration with keymap-level input processors - more flexible and maintainable.

---

## 3. PMW3610 Trackball Driver Updates

### Driver Landscape
| Fork | Base | Status | Notes |
|------|------|--------|-------|
| **badjeff/zmk-pmw3610-driver** | Original | Active | Most widely used, latest features |
| **inorichi/zmk-pmw3610-driver** | ufan's | Active | Alternative implementation |
| **tsironis/zmk-pmw3610-driver** | efogdev | Your fork | PM/IRQ fixes |
| **280Zo uses** | badjeff | Reference | With cursor-jump-on-wake patch |

### Recent Driver Improvements (badjeff)
1. **Scroll/snipe/auto-layer moved to keymap:** No longer needed in driver; configured via ZMK input listeners with layer-based overrides
2. **CPI moved to devicetree:** `CONFIG_PMW3610_CPI` now in `.dts/.overlay`, enabling per-sensor configs
3. **Separated sampling/reporting rates:** Reports accumulated XY displacement between interrupts - **lossless and predictable cursor traction**
4. **Shared SPI bus support:** Deprecated manual chip-select; uses Zephyr's `spi_transceive_dt()` for compatibility with displays and other SPI devices
5. **Power saving defaults:** Shorter downshift times applied automatically
6. **Deep sleep optimization:** 5.5x reduced battery consumption on trackball board during deep sleep

### Recommendation
Consider evaluating **badjeff's latest driver** against your tsironis fork. The key improvements (lossless tracking, shared SPI, power savings) may be worth adopting. Check if your PM/IRQ fixes have been upstreamed or if badjeff's driver has equivalent fixes.

---

## 4. BLE & Power Optimization

### Your Current BLE Config
```
CONFIG_BT_CTLR_TX_PWR_0=y              # Default TX power (0 dBm)
CONFIG_BT_PERIPHERAL_PREF_MAX_INT=9     # Fast connection interval
CONFIG_ZMK_BLE_EXPERIMENTAL_FEATURES=y  # Enables CONN + SEC
```

### Findings

**CONFIG_ZMK_BLE_EXPERIMENTAL_FEATURES:**
- Still active/not deprecated as of current docs
- Aggregate config enabling both `CONFIG_ZMK_BLE_EXPERIMENTAL_CONN` (disables 2M PHY for stability) and `CONFIG_ZMK_BLE_EXPERIMENTAL_SEC` (passkey entry)
- **Keep it** - provides connection stability benefits

**Connection Interval (`CONFIG_BT_PERIPHERAL_PREF_MAX_INT=9`):**
- Value of 9 = 11.25ms intervals - very aggressive
- Good for low latency but increases power consumption
- Central side uses more power than peripheral due to radio wake patterns
- Split communication adds ~3.75ms average latency (7.5ms worst case) regardless

**Power Management:**
- Your `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT=900000` (15 min) is reasonable
- Consider adding `CONFIG_ZMK_IDLE_TIMEOUT` for idle mode (before deep sleep)
- External power control (`CONFIG_ZMK_EXT_POWER`) can disable VCC to peripherals

**Industry benchmark:** Keychron Q Ultra (ZMK-powered) achieved 8,000 Hz polling over 2.4GHz dongle with 660 hours battery life at CES 2026. This uses a dongle approach, not BLE split.

### Battery Monitoring
Your config already has both proxy and fetching enabled - this is correct:
```
CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_PROXY=y
CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
```

---

## 5. Actionable Recommendations

### High Priority
1. **Pin ZMK version** in `west.yml` to avoid unexpected breakage (use `v0.4` when released, or a specific commit hash)
2. **Adopt input processor system** for scroll/snipe layers instead of driver-level config - more flexible, follows current ZMK best practices
3. **Evaluate badjeff's latest PMW3610 driver** vs your fork - lossless tracking and deep sleep improvements are significant

### Medium Priority
4. **Add idle timeout** before deep sleep: `CONFIG_ZMK_IDLE_TIMEOUT=30000` (30s to idle mode)
5. **Consider dongle build** for desktop use - eliminates BLE latency entirely
6. **Review your PMW3610 CPI config** - move to devicetree if still in Kconfig
7. **Test ZMK Studio** for runtime keymap editing - could simplify your workflow

### Low Priority
8. **Tune connection interval** - `CONFIG_BT_PERIPHERAL_PREF_MAX_INT=9` is aggressive; if battery life matters more, consider 12-15
9. **Monitor Zephyr 4.3 update** - will bring new USB stack and kscan -> matrix input migration
10. **Explore `process-next` flag** in input processors for combining scroll + snipe layer behaviors

---

## Sources

- [ZMK Zephyr 4.1 Update Blog Post](https://zmk.dev/blog/2025/12/09/zephyr-4-1)
- [ZMK Version Pinning Blog Post](https://zmk.dev/blog/2025/06/20/pinned-zmk)
- [ZMK Pointing Device Docs](https://zmk.dev/docs/features/pointing)
- [ZMK Input Processor Usage](https://zmk.dev/docs/keymaps/input-processors/usage)
- [ZMK Bluetooth Configuration](https://zmk.dev/docs/config/bluetooth)
- [ZMK Studio](https://zmk.dev/docs/features/studio)
- [ZMK Split Keyboards Docs](https://zmk.dev/docs/features/split-keyboards)
- [280Zo Charybdis Wireless Mini ZMK Firmware](https://github.com/280Zo/charybdis-wireless-mini-zmk-firmware)
- [badjeff PMW3610 Driver](https://github.com/badjeff/zmk-pmw3610-driver)
- [inorichi PMW3610 Driver](https://github.com/inorichi/zmk-pmw3610-driver)
- [ZMK Releases](https://github.com/zmkfirmware/zmk/releases)
- [ZMK Power Management (DeepWiki)](https://deepwiki.com/zmkfirmware/zmk/5.2-power-management)
