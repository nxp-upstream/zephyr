/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_imx_ccm_rev3

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/imx_ccm_rev3.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>

#include <fsl_clock.h>

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control_imx_ccm_rev3);

/*
 * The devicetree clock specifier packs a gate identifier and a clock-root
 * identifier into the single cell the shared NXP peripheral drivers can read
 * (see <zephyr/dt-bindings/clock/imx_ccm_rev3.h>). Gating and rate reporting
 * are therefore both answered from the specifier alone.
 *
 * Deliberately absent, and the reason this driver exists separately from
 * clock_control_mcux_ccm_rev2.c: there is no table mapping peripherals to clock
 * roots and no per-peripheral-driver conditional compilation. Both are what
 * make the rev2 driver grow with every new consumer.
 */

/*
 * Gate operations read-modify-write a shared CCM slice register, and several
 * peripherals' gates live in the same register. clock_control_on() may be
 * called from interrupt context, so serialize the read-modify-write. The rate
 * and status operations are reads and need no lock.
 */
static struct k_spinlock ccm_lock;

/* Clock roots this controller configures, in devicetree order. */
struct ccm_rev3_root {
	uint16_t id;
	uint8_t mux;
	uint8_t div;
	uint8_t second_div;
	bool shutdown;
	bool preconfigured;
};

/*
 * A root states the HAL's flat identifier in its own nxp,root-id property, so this
 * driver reads it rather than deriving it from reg. Deriving would put a per-SoC
 * numbering rule in code that several SoCs share.
 */
#define CCM_REV3_ROOT_ENTRY(node)                                                                  \
	{                                                                                          \
		.id = DT_PROP(node, nxp_root_id),                                                  \
		.mux = DT_PROP(node, clock_mux),                                                   \
		.div = DT_PROP(node, clock_div),                                                   \
		.second_div = DT_PROP(node, clock_second_div),                                     \
		.shutdown = DT_PROP(node, clock_shutdown),                                         \
		.preconfigured = DT_PROP(node, nxp_preconfigured),                                 \
	},

#define CCM_REV3_ROOT_ENTRY_IF_COMPAT(node)                                                        \
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, nxp_imx_ccm_rev3_root), \
		   (CCM_REV3_ROOT_ENTRY(node)))

/*
 * Terminator, so an instance with no clock-root children still has a non-empty
 * array. Named rather than written inline because a brace directly after the
 * FOREACH call reads as a compound literal, which clang-format then joins onto
 * the same line.
 */
#define CCM_REV3_ROOT_TERMINATOR {.id = IMX_CCM_ROOT_NONE},

/* Per-instance root list. */
struct ccm_rev3_config {
	const struct ccm_rev3_root *roots;
	size_t num_roots;
};

static bool ccm_rev3_decode(clock_control_subsys_t sub_system, uint32_t *gate, uint32_t *root)
{
	uintptr_t spec = (uintptr_t)sub_system;

	*gate = IMX_CCM_CLK_GATE(spec);
	*root = IMX_CCM_CLK_ROOT(spec);

	/*
	 * Reject an identifier this SoC cannot own before handing it to the HAL.
	 * The HAL is not unsafe with one -- its gate lookup range-checks every
	 * subsystem and writes nothing when none matches -- but it reports
	 * nothing either, so the caller would be told its clock was enabled when
	 * it was not. Fail here instead. Gaps inside the enumeration are not
	 * detectable from here, only out-of-range values.
	 */
	if (*gate != IMX_CCM_GATE_NONE && *gate >= (uint32_t)kCLOCK_IpInvalid) {
		return false;
	}

	return true;
}

static int ccm_rev3_on(const struct device *dev, clock_control_subsys_t sub_system)
{
	uint32_t gate, root;
	k_spinlock_key_t key;

	ARG_UNUSED(dev);

	if (!ccm_rev3_decode(sub_system, &gate, &root)) {
		return -EINVAL;
	}

	if (gate == IMX_CCM_GATE_NONE) {
		return 0;
	}

	key = k_spin_lock(&ccm_lock);
	CLOCK_EnableClock((clock_ip_name_t)gate);
	k_spin_unlock(&ccm_lock, key);

	return 0;
}

static int ccm_rev3_off(const struct device *dev, clock_control_subsys_t sub_system)
{
	uint32_t gate, root;
	k_spinlock_key_t key;

	ARG_UNUSED(dev);

	if (!ccm_rev3_decode(sub_system, &gate, &root)) {
		return -EINVAL;
	}

	if (gate == IMX_CCM_GATE_NONE) {
		return 0;
	}

	key = k_spin_lock(&ccm_lock);
	CLOCK_DisableClock((clock_ip_name_t)gate);
	k_spin_unlock(&ccm_lock, key);

	return 0;
}

/*
 * Resolve the clock root a subsystem specifier refers to, in either of the two
 * cell forms a peripheral's clocks entry can use: an IMX_CCM_CLK(gate, root)
 * specifier, or a self-describing IMX_CCM_ROOT_CFG cell. A driver passes
 * clock_control whichever one its devicetree gave it, and the MIPI CSI-2 RX
 * node for instance declares its byte, UI and escape clocks as ROOT_CFG cells.
 * Decoding one of those as an IMX_CCM_CLK specifier yields a nonsense root.
 */
static bool ccm_rev3_resolve_root(clock_control_subsys_t sub_system, uint32_t *root)
{
	uintptr_t cell = (uintptr_t)sub_system;
	uint32_t gate;

	if (IMX_CCM_ROOT_CFG_IS(cell)) {
		*root = IMX_CCM_ROOT_CFG_ROOT(cell);
		return true;
	}

	return ccm_rev3_decode(sub_system, &gate, root);
}

static int ccm_rev3_get_rate(const struct device *dev, clock_control_subsys_t sub_system,
			     uint32_t *rate)
{
	uint32_t root;

	ARG_UNUSED(dev);

	if (rate == NULL) {
		return -EINVAL;
	}

	if (!ccm_rev3_resolve_root(sub_system, &root)) {
		return -EINVAL;
	}

	if (root == IMX_CCM_ROOT_NONE) {
		/*
		 * This peripheral has no dedicated clock root, so there is no
		 * rate to report. Say so rather than inventing one.
		 */
		return -ENOTSUP;
	}

	*rate = CLOCK_GetRootClockFreq((clock_root_t)root);

	return 0;
}

static enum clock_control_status ccm_rev3_get_status(const struct device *dev,
						     clock_control_subsys_t sub_system)
{
	uint32_t gate, root;

	ARG_UNUSED(dev);

	if (!ccm_rev3_decode(sub_system, &gate, &root)) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	if (gate == IMX_CCM_GATE_NONE) {
		/* Ungated peripherals are always clocked. */
		return CLOCK_CONTROL_STATUS_ON;
	}

	/*
	 * Reading a gate back needs the CCM instance and slice index that own
	 * it, and the HAL keeps that mapping private (locateClkGate() is static
	 * in fsl_clock.c) without exposing a query. Report unknown rather than
	 * guessing or duplicating the mapping here. nxp,imx-ccm-rev2 does not
	 * implement this operation at all.
	 */
	return CLOCK_CONTROL_STATUS_UNKNOWN;
}

static int ccm_rev3_configure(const struct device *dev, clock_control_subsys_t sub_system,
			      void *data)
{
	uintptr_t cell = (uintptr_t)sub_system;
	clock_root_config_t root_cfg;
	uint32_t root, mux, clk_div, snd_div;

	ARG_UNUSED(dev);

	/*
	 * The root to program travels, by value, as the subsystem argument -- a
	 * packed IMX_CCM_ROOT_CFG cell, the same channel other Zephyr clock
	 * bindings use to pass a per-peripheral clock-config value. It is
	 * self-describing (a fixed tag in its top nibble), so this driver needs
	 * no peripheral-to-root table and no data pointer.
	 */
	ARG_UNUSED(data);

	if (!IMX_CCM_ROOT_CFG_IS(cell)) {
		/*
		 * Not a root-config cell: a legacy IMX_CCM_CLK(gate, root)
		 * specifier that the shared drivers pass to configure()
		 * unconditionally before .on. There is no root to program from
		 * it -- its root is either programmed at init from a ccm root
		 * child, or fixed by SoC bring-up. Report success so those
		 * callers proceed.
		 */
		return 0;
	}

	root = IMX_CCM_ROOT_CFG_ROOT(cell);
	mux = IMX_CCM_ROOT_CFG_MUX(cell);
	clk_div = IMX_CCM_ROOT_CFG_DIV(cell);
	snd_div = IMX_CCM_ROOT_CFG_SND_DIV(cell);

	/*
	 * The HAL programs a divider as value - 1, so a 0 here would reach the
	 * register as an all-ones field: the slowest divider rather than the
	 * fastest, which presents as a peripheral running far below its
	 * configured rate rather than as an error.
	 */
	if (clk_div == 0U || snd_div == 0U) {
		return -EINVAL;
	}

	root_cfg = (clock_root_config_t){
		.clockShutdown = false,
		.mux = (uint8_t)mux,
		.div = (uint8_t)clk_div,
		.sndDiv = (uint8_t)snd_div,
	};

	CLOCK_SetRootClock((clock_root_t)root, &root_cfg);

	return 0;
}

/*
 * Divider field bounds. CCM_SLICE_CONTROL_DIV is 8 bits wide and encodes
 * div - 1, so the usable range is 1..256.
 */
#define CCM_REV3_DIV_MIN 1U
#define CCM_REV3_DIV_MAX 256U

static int ccm_rev3_set_rate(const struct device *dev, clock_control_subsys_t sub_system,
			     clock_control_subsys_rate_t rate)
{
	uint32_t requested = (uint32_t)(uintptr_t)rate;
	uint32_t root, mux, div, src_freq, cur_freq, cur_div, snd_div;
	clock_name_t src;
	clock_root_config_t root_cfg;
	k_spinlock_key_t key;

	ARG_UNUSED(dev);

	if (!ccm_rev3_resolve_root(sub_system, &root)) {
		return -EINVAL;
	}

	if (root == IMX_CCM_ROOT_NONE) {
		/* No dedicated root, so there is no rate to set. */
		return -ENOTSUP;
	}

	if (requested == 0U) {
		return -EINVAL;
	}

	/*
	 * Only the first divider is adjusted. The mux keeps whatever source the
	 * static devicetree root configuration (or the SoC bring-up) selected,
	 * and the second divider is read back and written through unchanged.
	 *
	 * Re-muxing would change the source for every consumer of this root,
	 * which a per-peripheral rate request has no business doing: the source
	 * a board picked is a deliberate choice. And a root whose second
	 * divider is in use (CGU MAIN_ROOTCLK, for one) would have its ratio
	 * silently altered if sndDiv were forced to 1 here.
	 */
	mux = CLOCK_GetRootClockMux((clock_root_t)root);
	src = CLOCK_GetRootClockSource((clock_root_t)root, mux);
	if (src == kCLOCK_SRC_Invalid) {
		return -EIO;
	}

	src_freq = CLOCK_GetClockSrcFreq(src);
	cur_div = CLOCK_GetRootClockDiv((clock_root_t)root);
	cur_freq = CLOCK_GetRootClockFreq((clock_root_t)root);
	if (src_freq == 0U || cur_div == 0U || cur_freq == 0U) {
		return -EIO;
	}

	/*
	 * The second divider has to be recovered rather than read:
	 * CLOCK_GetRootClockSndDiv() is static in fsl_clock.c and not exported.
	 * CLOCK_GetRootClockFreq() computes (src_freq / div) / sndDiv from the
	 * same registers, so dividing the source frequency by the reported
	 * output and the first divider leaves exactly the second divider.
	 *
	 * A zero result would mean the reported output exceeds src_freq / div,
	 * i.e. the three readings disagree.
	 */
	snd_div = src_freq / cur_div / cur_freq;
	if (snd_div == 0U) {
		return -EIO;
	}

	/*
	 * The root output is (src_freq / div) / snd_div, so the first divider
	 * has to cover the requested rate with the second one already applied.
	 *
	 * Rounded up, so the resulting rate lands at or below the request:
	 * callers ask for a rate their peripheral must not exceed (the CSI-2
	 * escape clock has a documented upper bound), which makes overshooting
	 * the unsafe direction to err in.
	 */
	div = DIV_ROUND_UP(src_freq, requested * snd_div);

	if (div < CCM_REV3_DIV_MIN || div > CCM_REV3_DIV_MAX) {
		/*
		 * Not reachable from this source by division alone: either
		 * above what the source supplies, or so far below it that the
		 * divider field cannot express the ratio. Changing the mux is
		 * the board's decision, so report the request as unsatisfiable
		 * rather than programming a rate nobody asked for.
		 */
		return -ENOTSUP;
	}

	root_cfg = (clock_root_config_t){
		.clockShutdown = false,
		.mux = (uint8_t)mux,
		.div = (uint8_t)div,
		.sndDiv = (uint8_t)snd_div,
	};

	key = k_spin_lock(&ccm_lock);
	CLOCK_SetRootClock((clock_root_t)root, &root_cfg);
	k_spin_unlock(&ccm_lock, key);

	return 0;
}

static DEVICE_API(clock_control, ccm_rev3_api) = {
	.on = ccm_rev3_on,
	.off = ccm_rev3_off,
	.get_rate = ccm_rev3_get_rate,
	.set_rate = ccm_rev3_set_rate,
	.get_status = ccm_rev3_get_status,
	.configure = ccm_rev3_configure,
};

static int ccm_rev3_init(const struct device *dev)
{
	const struct ccm_rev3_config *config = dev->config;

	/*
	 * Program this instance's clock roots, in devicetree order. Ordering
	 * within an instance is the devicetree author's responsibility: a root
	 * whose selected source is produced by another root has to come after
	 * it. Ordering *across* instances is not expressible here at all --
	 * device initialization order at equal priority follows the devicetree
	 * dependency ordinal, not the order the nodes are written -- so roots
	 * that need a fixed order relative to each other belong in one instance.
	 */
	for (size_t i = 0; i < config->num_roots; i++) {
		const struct ccm_rev3_root *root = &config->roots[i];
		clock_root_config_t cfg;

		if (root->id == IMX_CCM_ROOT_NONE) {
			continue;
		}

		/*
		 * A root marked nxp,preconfigured is programmed by the SoC
		 * bring-up earlier in boot, not here. Its steady-state mux/div is
		 * declared on the node (so its rate is reportable and a single
		 * devicetree value the SoC reads back), but re-programming it from
		 * this loop would duplicate the SoC's work, and for a root feeding
		 * the running CPU or a live bus would re-write a clock the core is
		 * running from. Skip it.
		 */
		if (root->preconfigured) {
			continue;
		}

		cfg = (clock_root_config_t){
			.clockShutdown = root->shutdown,
			.mux = root->mux,
			.div = root->div,
			.sndDiv = root->second_div,
		};

		CLOCK_SetRootClock((clock_root_t)root->id, &cfg);
	}

	LOG_DBG("%s: configured %zu clock root(s)", dev->name, config->num_roots);

	return 0;
}

#define CCM_REV3_INSTANCE(n)                                                                       \
	static const struct ccm_rev3_root ccm_rev3_roots_##n[] = {                                 \
		DT_INST_FOREACH_CHILD_STATUS_OKAY(n, CCM_REV3_ROOT_ENTRY_IF_COMPAT)                \
			CCM_REV3_ROOT_TERMINATOR};                                                 \
                                                                                                   \
	static const struct ccm_rev3_config ccm_rev3_config_##n = {                                \
		.roots = ccm_rev3_roots_##n,                                                       \
		.num_roots = ARRAY_SIZE(ccm_rev3_roots_##n) - 1,                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, ccm_rev3_init, NULL, NULL, &ccm_rev3_config_##n, PRE_KERNEL_1,    \
			      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &ccm_rev3_api);

/*
 * One device per CCM instance. The terminator entry above keeps each array
 * non-zero-length on an instance that declares no clock roots, and num_roots
 * excludes it.
 */
DT_INST_FOREACH_STATUS_OKAY(CCM_REV3_INSTANCE)
