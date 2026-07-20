/*
 * SLEEP-DEBUG instrumentation (branch-only, not for production).
 *
 * Logs the nRF52 reset reason captured at boot plus an uptime heartbeat
 * every 30s. When the keyboard wedges, plug in USB-C and attach to the
 * CDC console: the first heartbeat discriminates between
 *   - fresh boot + OFF/VBUS reset reason  -> board was in SYSTEM_OFF and
 *     only USB woke it (key-matrix wake never fired), vs
 *   - hours of uptime                     -> board woke long ago and is
 *     wedged elsewhere (e.g. BLE reconnect).
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrfx.h>

LOG_MODULE_REGISTER(sleep_debug, LOG_LEVEL_INF);

static uint32_t boot_resetreas;

static void heartbeat_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat_cb);

static void heartbeat_cb(struct k_work *work) {
    ARG_UNUSED(work);

    int64_t up_ms = k_uptime_get();

    LOG_INF("heartbeat: uptime %lld.%03llds, boot RESETREAS=0x%08x%s%s%s%s%s%s",
            up_ms / 1000, up_ms % 1000, boot_resetreas,
            (boot_resetreas & POWER_RESETREAS_OFF_Msk) ? " OFF(gpio-sense-wake)" : "",
            (boot_resetreas & POWER_RESETREAS_VBUS_Msk) ? " VBUS(usb-wake)" : "",
            (boot_resetreas & POWER_RESETREAS_RESETPIN_Msk) ? " RESETPIN" : "",
            (boot_resetreas & POWER_RESETREAS_SREQ_Msk) ? " SREQ(soft-reset)" : "",
            (boot_resetreas & POWER_RESETREAS_DOG_Msk) ? " WATCHDOG" : "",
            (boot_resetreas & POWER_RESETREAS_LOCKUP_Msk) ? " LOCKUP(cpu-fault)" : "");

    k_work_schedule(&heartbeat_work, K_SECONDS(30));
}

static int sleep_debug_init(void) {
    boot_resetreas = NRF_POWER->RESETREAS;
    /* Write-1-to-clear so the next boot reports only its own cause. */
    NRF_POWER->RESETREAS = boot_resetreas;

    k_work_schedule(&heartbeat_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(sleep_debug_init, APPLICATION, 99);
