.. _cpu_load_metric:

CPU Load
########

The CPU Load metric returns the CPU load as a percentage for the current CPU,
since the last time it was called.

Applications can also read the same accounting interval as a CPU load window.
The window API returns the recent non-idle cycle count, interval duration, load
percentage, source, and confidence value. A successful window read advances the
per-CPU baseline used by both ``cpu_load_window_get()`` and
``cpu_load_metric_get()``. The first read for a CPU seeds the baseline and does
not report a complete interval yet.

For an example of the CPU Load metric refer to :zephyr:code-sample:`cpu_freq_on_demand` sample.
