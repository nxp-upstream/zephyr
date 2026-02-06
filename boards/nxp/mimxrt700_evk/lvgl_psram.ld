#include <zephyr/linker/sections.h>

/* Place LVGL display buffers into PSRAM2 linker region */
SECTION_DATA_PROLOGUE(.noinit.lvgl, (NOLOAD),)
{
  /* Draw buffer(s) */
  KEEP(*(.lvgl_buf))
  KEEP(*(.lvgl_buf.*))

  KEEP(*(.lvgl_heap))
  KEEP(*(.lvgl_heap.*))
}
GROUP_LINK_IN(PSRAM2)