/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_resetcon_hwinfo

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <fsl_resetcon.h>

LOG_MODULE_REGISTER(hwinfo_nxp_resetcon, CONFIG_HWINFO_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one nxp,resetcon-hwinfo node must be enabled");

#define RESETCON_BASE ((RESETCON_Type *)DT_INST_REG_ADDR(0))

/* For a source that only qualifies another one reported alongside it. */
#define RESETCON_NO_RESET_CAUSE 0U

struct resetcon_cause_map {
	uint32_t source; /* One _resetcon_reset_source_flags bit */
	uint32_t cause;  /* RESET_* flags */
};

/* One entry per _resetcon_reset_source_flags bit, in RSTSTAT_STICKY bit order. */
static const struct resetcon_cause_map resetcon_cause_map[] = {
	{kRESETCON_SrcPorVbat, RESET_POR},
	{kRESETCON_SrcPmuPor, RESET_POR},
	{kRESETCON_SrcPinPor, RESET_POR},
	/* Set by the PMU voltage monitors. */
	{kRESETCON_SrcPmuCold, RESET_BROWNOUT},
	/* Wakeup from Deep Power Down. */
	{kRESETCON_SrcWuVbat, RESET_LOW_POWER_WAKE},
	/* Cold reset requested by the CSS security subsystem. */
	{kRESETCON_SrcSecCold, RESET_SECURITY},
	/*
	 * A warm reset escalated to a cold one also latches its own source bit, which
	 * carries the cause.
	 */
	{kRESETCON_SrcWarmEsca, RESETCON_NO_RESET_CAUSE},
	/* An on-chip module reported an error to RESETCON during a reset sequence. */
	{kRESETCON_SrcRstErr, RESET_HARDWARE},
	{kRESETCON_SrcPinReset, RESET_PIN},
	{kRESETCON_SrcResetSw, RESET_SOFTWARE},
	{kRESETCON_SrcDap, RESET_DEBUG},
	{kRESETCON_SrcJtag, RESET_DEBUG},
	{kRESETCON_SrcCpu0Sw, RESET_SOFTWARE},
	{kRESETCON_SrcCpu0Err, RESET_CPU_LOCKUP},
	{kRESETCON_SrcSwt0, RESET_WATCHDOG},
	{kRESETCON_SrcSwt1, RESET_WATCHDOG},
	{kRESETCON_SrcSecvio, RESET_SECURITY},
	/* Tamper detection (TDET), not an external reset pin. */
	{kRESETCON_SrcTamper, RESET_SECURITY},
	/* Requested by the same PMU monitors as kRESETCON_SrcPmuCold. */
	{kRESETCON_SrcPmuWarm, RESET_BROWNOUT},
	/* Wakeup from Power Down. */
	{kRESETCON_SrcWuPd, RESET_LOW_POWER_WAKE},
};

static uint32_t hwinfo_nxp_resetcon_xlate(uint32_t sources)
{
	uint32_t cause = 0U;

	ARRAY_FOR_EACH_PTR(resetcon_cause_map, entry) {
		if ((sources & entry->source) != 0U) {
			cause |= entry->cause;
		}
	}

	return cause;
}

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	const uint32_t sources = RESETCON_GetStickyResetStatus(RESETCON_BASE);

	*cause = hwinfo_nxp_resetcon_xlate(sources);

	LOG_DBG("sources = 0x%08x, cause = 0x%08x", sources, *cause);

	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	RESETCON_ClearStickyResetStatus(RESETCON_BASE, kRESETCON_SrcAll);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = hwinfo_nxp_resetcon_xlate(kRESETCON_SrcAll);

	return 0;
}
