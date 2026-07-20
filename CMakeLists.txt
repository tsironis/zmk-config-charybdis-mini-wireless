# SLEEP-DEBUG branch only: build the instrumentation source.
zephyr_library_named(zmk_config_sleep_debug)
zephyr_library_sources(src/sleep_debug.c)
