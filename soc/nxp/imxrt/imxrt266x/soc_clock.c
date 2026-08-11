/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * RT266x clock bring-up, adapted from the MCUXpresso SDK, where it is developed
 * and validated on hardware:
 *
 *   mcuxsdk/examples/_boards/mimxrt2660evk/common/clock/clock_config.c
 *   as of commit 996cb87daf7459ca2829ea46feca9655d166c2b1 (2026-08-07)
 *
 * Re-licensed Apache-2.0 (NXP owns both copies) and reformatted, so when
 * resyncing, diff behaviour rather than text and update the reference above.
 * Deviations: only the Over Drive Run operating point is carried;
 * AT_QUICKACCESS_SECTION_CODE becomes __itcm_section; the window runs on its own
 * DTCM stack because the board's stack is in the PSRAM being parked; interrupts
 * are masked here rather than by the caller.
 *
 * This is C and not devicetree because the boot chapter warns that "the SoC may
 * crash if the clocks are not handled properly" and prescribes parking affected
 * controllers on a fixed root first. Here that includes both XSPI instances --
 * the flash this code executes from and the PSRAM holding its data -- so the
 * hand-over runs from ITCM/DTCM with both parked, which devicetree cannot express.
 *
 *   soc_clock_init -> POWER_EnterHpRun --(callback)--> soc_clock_apply_od_run
 *     soc_clock_prepare: ConfigCGUAna, compute Sys-PLL images into DTCM, then
 *       soc_clock_handover_on_dtcm_stack (naked asm, stack switch)
 *         soc_clock_handover_to_sxosc  <-- ITCM window, both XSPI parked
 *       then InitAudioPll / InitVideoPll, which lock on the new reference
 *     then, from flash: InitCorePll, InitMainPll, and only after those two
 *       SYSCON_common, SYSCON_HPRUN, ConfigCGUDig_SS x6 domains -- in that
 *       order, because the SYSCON slices source the PLLs above.
 */

#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include "soc_clock.h"
#include <fsl_clock.h>
#include <fsl_iomuxc.h>
#include <fsl_power.h>
#include <fsl_modcon.h>

/*
 * This file addresses the hardware through the SDK's <PERIPHERAL>_BASE macros,
 * and so does the SDK on our behalf: CLOCK_SetRootClock() and the CLOCK_Init*()
 * analog bring-up re-derive their base inside fsl_clock.c. Those macros follow
 * __ARM_FEATURE_CMSE, so they resolve to the secure or the non-secure view by how
 * this file is compiled -- while devicetree describes a view of its own. The two
 * must agree, and one assertion is enough to prove it: the secure alias is a
 * uniform +0x10000000 across the whole peripheral aperture, so any single node
 * pins the view for all of them.
 *
 * Deriving the bases from devicetree instead was tried and dropped. With this
 * assertion in place the two are provably equal, so it bought nothing; and it was
 * less safe, because the assertion pins one node while a typo in another node's
 * reg would have silently redirected a register write. Devicetree still states
 * every address -- describing the SoC and reading the description are separate
 * decisions.
 */
BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(syscon_ccm)) == SYSCON__CCM_BASE,
	     "devicetree and SDK headers disagree on the secure/non-secure view");

/*******************************************************************************
 * Definitions
 *****************************************************************************
 */

/* Over Drive Run frequencies. The primary values live in devicetree -- the
 * nxp,imx-cguana-* nodes, with BUILD_ASSERTs below tying each PLL rate to its
 * reference and dividers -- so this is orientation, not the source of truth.
 *
 *   SXOSC crystal  24 MHz, the reference for every PLL
 *   FRO192M / FRO12M  the park and fallback sources used by the hand-over window
 *   Core PLL      792 MHz   -> NPU_ROOTCLK
 *   Main PLL VCO  2 GHz     -> DIVOUT1 1000 MHz -> PLL_PFDX -> MAIN_ROOTCLK (CPU)
 *                            -> DIVX 400 MHz -> PERI roots, bus roots
 *   Sys PLL VCO   2 GHz     -> DIV4 500 MHz, the XSPI hop target
 *                            -> DIV10 200 MHz -> SYSCON_PDMAIN_CLK
 *   Audio PLL      49.152 MHz, Video PLL 70.64 MHz
 */

/*******************************************************************************
 * Variables
 *****************************************************************************
 */

/*
 * Analog source configuration, derived from devicetree.
 *
 * Each of these mirrors one nxp,imx-cguana-* node, so a board changes its
 * crystal or its CPU frequency by editing devicetree rather than this file. The
 * mappings below are the only place the hardware's register encodings are
 * spelled out; devicetree says what it means, in Hz and divider values.
 */

#define SXOSC_NODE    DT_NODELABEL(sxosc)
#define FRO192M_NODE  DT_NODELABEL(fro192m)
#define FRO12M_NODE   DT_NODELABEL(fro12m)
#define CORE_PLL_NODE DT_NODELABEL(core_pll)
#define MAIN_PLL_NODE DT_NODELABEL(main_pll)
#define SYS_PLL_NODE  DT_NODELABEL(sys_pll)

/*
 * Steady-state clock roots this SoC bring-up programs, each from its
 * nxp,imx-ccm-rev3-root node. The node's nxp,root-id is the HAL root id -- its reg
 * is the slice's register block and must never be used as the id -- and clock-mux /
 * clock-div / clock-second-div are the HAL mux/div/sndDiv values directly (the
 * dt-bindings header is generated to match the HAL enum values), so the SoC
 * programs the same value the node declares -- no steady-state literal in C.
 * Of these roots only sysplldiv4_rootclk carries nxp,preconfigured, because the
 * controller's init loop runs from the flash it feeds; for the rest the
 * controller re-applies the same devicetree value later, which is idempotent.
 *
 * SOC_SET_ROOT_CLOCK() programs one root from its node; SOC_SET_ROOT_CLOCK_SD()
 * also carries clock-second-div for the roots that split into a divided and a
 * second-divided output (MAIN_ROOTCLK).
 */
#define SOC_SET_ROOT_CLOCK(node)                                                                   \
	do {                                                                                       \
		clock_root_config_t _cfg = {                                                       \
			.mux = DT_PROP(node, clock_mux),                                           \
			.div = DT_PROP(node, clock_div),                                           \
			.sndDiv = 1U,                                                              \
		};                                                                                 \
		CLOCK_SetRootClock((clock_root_t)DT_PROP(node, nxp_root_id), &_cfg);               \
	} while (0)

#define SOC_SET_ROOT_CLOCK_SD(node)                                                                \
	do {                                                                                       \
		clock_root_config_t _cfg = {                                                       \
			.mux = DT_PROP(node, clock_mux),                                           \
			.div = DT_PROP(node, clock_div),                                           \
			.sndDiv = DT_PROP(node, clock_second_div),                                 \
		};                                                                                 \
		CLOCK_SetRootClock((clock_root_t)DT_PROP(node, nxp_root_id), &_cfg);               \
	} while (0)

/* A PLL's reference is a clocks phandle, so its rate cannot disagree with it. */
#define SOC_PLL_REF_HZ(node) DT_PROP(DT_CLOCKS_CTLR(node), clock_frequency)

#define SOC_CGUANA_REF_FREQ(hz)                                                                    \
	((hz) == 19200000   ? kCLOCK_CguanaRefFreq19p2M                                            \
	 : (hz) == 24000000 ? kCLOCK_CguanaRefFreq24M                                              \
	 : (hz) == 32000000 ? kCLOCK_CguanaRefFreq32M                                              \
	 : (hz) == 40000000 ? kCLOCK_CguanaRefFreq40M                                              \
			    : -1)

#define SOC_SXOSC_MODE(node)                                                                       \
	(DT_ENUM_HAS_VALUE(node, nxp_mode, crystal)     ? kCLOCK_CguanaSxoscModeCrystal            \
	 : DT_ENUM_HAS_VALUE(node, nxp_mode, ac_slave)  ? kCLOCK_CguanaSxoscModeAcSlave            \
	 : DT_ENUM_HAS_VALUE(node, nxp_mode, dc_bypass) ? kCLOCK_CguanaSxoscModeDcBypass           \
							: kCLOCK_CguanaSxoscModeDcBypassNoDet)

BUILD_ASSERT(SOC_CGUANA_REF_FREQ(SOC_PLL_REF_HZ(CORE_PLL_NODE)) != -1,
	     "core PLL reference frequency is not one the hardware offers");
BUILD_ASSERT(SOC_CGUANA_REF_FREQ(SOC_PLL_REF_HZ(MAIN_PLL_NODE)) != -1,
	     "main PLL reference frequency is not one the hardware offers");
BUILD_ASSERT(SOC_CGUANA_REF_FREQ(SOC_PLL_REF_HZ(SYS_PLL_NODE)) != -1,
	     "sys PLL reference frequency is not one the hardware offers");

/*
 * The core PLL output is its reference multiplied by the loop divider, halved if
 * the post divider is on. Check the output devicetree states against that rather
 * than trusting it, so a mismatch is a build error and not a surprise on the
 * bench.
 */
BUILD_ASSERT(DT_PROP(CORE_PLL_NODE, clock_frequency) ==
		     (SOC_PLL_REF_HZ(CORE_PLL_NODE) * DT_PROP(CORE_PLL_NODE, nxp_loop_div)) /
			     (DT_PROP(CORE_PLL_NODE, nxp_post_div_by_2) ? 2 : 1),
	     "core PLL clock-frequency disagrees with its loop and post dividers");

static const clock_cguana_sxosc_config_t s_sxoscConfig = {
	.modeSel = SOC_SXOSC_MODE(SXOSC_NODE),
	.gmSel = DT_PROP(SXOSC_NODE, nxp_gm_sel),
	.xtal1CapTrim = DT_PROP_BY_IDX(SXOSC_NODE, nxp_cap_trim, 0),
	.xtal2CapTrim = DT_PROP_BY_IDX(SXOSC_NODE, nxp_cap_trim, 1),
	.detTrim = DT_PROP(SXOSC_NODE, nxp_det_trim),
	.clkDiv2En = DT_PROP(SXOSC_NODE, nxp_div2_enable),
};

#define SOC_FRO192M_OTWB(hz)                                                                       \
	((hz) == 96000000    ? kCLOCK_CguanaFro192mOtwb96M                                         \
	 : (hz) == 120000000 ? kCLOCK_CguanaFro192mOtwb120M                                        \
	 : (hz) == 144000000 ? kCLOCK_CguanaFro192mOtwb144M                                        \
	 : (hz) == 192000000 ? kCLOCK_CguanaFro192mOtwb192M                                        \
	 : (hz) == 240000000 ? kCLOCK_CguanaFro192mOtwb240M                                        \
	 : (hz) == 288000000 ? kCLOCK_CguanaFro192mOtwb288M                                        \
	 : (hz) == 384000000 ? kCLOCK_CguanaFro192mOtwb384M                                        \
	 : (hz) == 480000000 ? kCLOCK_CguanaFro192mOtwb480M                                        \
			     : -1)

#define SOC_FRO12M_OTWB(hz)                                                                        \
	((hz) == 8000000    ? kCLOCK_CguanaFro12mOtwb8M                                            \
	 : (hz) == 10000000 ? kCLOCK_CguanaFro12mOtwb10M                                           \
	 : (hz) == 12000000 ? kCLOCK_CguanaFro12mOtwb12M                                           \
	 : (hz) == 12288000 ? kCLOCK_CguanaFro12mOtwb12p288M                                       \
	 : (hz) == 16000000 ? kCLOCK_CguanaFro12mOtwb16M                                           \
	 : (hz) == 20000000 ? kCLOCK_CguanaFro12mOtwb20M                                           \
	 : (hz) == 24000000 ? kCLOCK_CguanaFro12mOtwb24M                                           \
	 : (hz) == 32000000 ? kCLOCK_CguanaFro12mOtwb32M                                           \
			    : -1)

BUILD_ASSERT(SOC_FRO192M_OTWB(DT_PROP(FRO192M_NODE, clock_frequency)) != -1,
	     "fro192m clock-frequency is not one of the bands the hardware offers");
BUILD_ASSERT(SOC_FRO12M_OTWB(DT_PROP(FRO12M_NODE, clock_frequency)) != -1,
	     "fro12m clock-frequency is not one of the bands the hardware offers");

static const clock_cguana_fro192m_config_t s_fro192mConfig = {
	.otwb = SOC_FRO192M_OTWB(DT_PROP(FRO192M_NODE, clock_frequency)),
};

static const clock_cguana_fro12m_config_t s_fro12mConfig = {
	.otwb = SOC_FRO12M_OTWB(DT_PROP(FRO12M_NODE, clock_frequency)),
};

static const clock_cguana_core_pll_config_t s_corePllConfig = {
	.vcoSelHf = DT_PROP(CORE_PLL_NODE, nxp_vco_high_band),
	.refFreq = SOC_CGUANA_REF_FREQ(SOC_PLL_REF_HZ(CORE_PLL_NODE)),
	.loopDivNint = DT_PROP(CORE_PLL_NODE, nxp_loop_div),
	.postDivBy2 = DT_PROP(CORE_PLL_NODE, nxp_post_div_by_2),
};

#define SOC_FRAC_PLL_VCO(hz)                                                                       \
	((hz) == 2000000000   ? kCLOCK_CguanaFracPllVco2000M                                       \
	 : (hz) == 1950000000 ? kCLOCK_CguanaFracPllVco1950M                                       \
	 : (hz) == 1900000000 ? kCLOCK_CguanaFracPllVco1900M                                       \
	 : (hz) == 1850000000 ? kCLOCK_CguanaFracPllVco1850M                                       \
			      : -1)

/* Is this integer divider listed in the node's nxp,int-divs? */
#define SOC_INT_DIV_MATCH(node, prop, idx, d) (DT_PROP_BY_IDX(node, prop, idx) == (d)) ||

#define SOC_HAS_INT_DIV(node, d)                                                                   \
	(DT_FOREACH_PROP_ELEM_VARGS(node, nxp_int_divs, SOC_INT_DIV_MATCH, d) false)

/* A selector of zero leaves that fractional output disabled. */
#define SOC_FRAC_DIV(node, idx)                                                                    \
	{                                                                                          \
		.en = DT_PROP_BY_IDX(node, nxp_frac_div_sels, idx) != 0,                           \
		.range = false,                                                                    \
		.sel = DT_PROP_BY_IDX(node, nxp_frac_div_sels, idx),                               \
	}

#define SOC_FRAC_PLL_CONFIG(node)                                                                  \
	{                                                                                          \
		.refFreq = SOC_CGUANA_REF_FREQ(SOC_PLL_REF_HZ(node)),                              \
		.lowFreq = SOC_FRAC_PLL_VCO(DT_PROP(node, nxp_vco_frequency)),                     \
		.div5En = SOC_HAS_INT_DIV(node, 5),                                                \
		.div8En = SOC_HAS_INT_DIV(node, 8),                                                \
		.div10En = SOC_HAS_INT_DIV(node, 10),                                              \
		.div20En = SOC_HAS_INT_DIV(node, 20),                                              \
		.fracDiv = {SOC_FRAC_DIV(node, 0), SOC_FRAC_DIV(node, 1), SOC_FRAC_DIV(node, 2)},  \
		.sscgEn = false,                                                                   \
		.sscg = NULL,                                                                      \
	}

BUILD_ASSERT(SOC_FRAC_PLL_VCO(DT_PROP(MAIN_PLL_NODE, nxp_vco_frequency)) != -1,
	     "main PLL nxp,vco-frequency is not one the hardware offers");
BUILD_ASSERT(SOC_FRAC_PLL_VCO(DT_PROP(SYS_PLL_NODE, nxp_vco_frequency)) != -1,
	     "sys PLL nxp,vco-frequency is not one the hardware offers");

/*
 * The main PLL's second fractional output is what reaches the CPU: it feeds
 * PLL_PFDX, then the CGU MAIN_ROOTCLK slice, then the compute subsystem's
 * cpu_clk. The selector counts quarter steps, so the output is
 * vco-frequency * 4 / selector. Check that against the CPU frequency devicetree
 * declares, so the two cannot drift apart -- this is the one arithmetic error
 * that would leave the core running at a rate nothing in the build describes.
 */
#define SOC_MAIN_PLL_DIVOUT1_HZ                                                                    \
	((DT_PROP(MAIN_PLL_NODE, nxp_vco_frequency) /                                              \
	  DT_PROP_BY_IDX(MAIN_PLL_NODE, nxp_frac_div_sels, 1)) *                                   \
	 4U)

BUILD_ASSERT(SOC_MAIN_PLL_DIVOUT1_HZ == DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency),
	     "cpu0 clock-frequency disagrees with the main PLL output that drives it");

static const clock_cguana_frac_pll_config_t s_mainPllConfig = SOC_FRAC_PLL_CONFIG(MAIN_PLL_NODE);
static const clock_cguana_frac_pll_config_t s_sysPllConfig = SOC_FRAC_PLL_CONFIG(SYS_PLL_NODE);

/*
 * The audio and video PLLs are re-locked after the reference hand-over, because
 * that hand-over moves the reference they were locked to, and leaving a PLL
 * locked to a reference that no longer exists is worse than configuring outputs
 * nobody reads. Their configurable parameters (CCO band, post-divide, and the
 * video fractional numerator) come from nxp,imx-cguana-avpll nodes; the fixed
 * start mode and reference stay C constants of the procedure.
 */
#define AVPLL_AUDIO_NODE DT_NODELABEL(avpll_audio)
#define AVPLL_VIDEO_NODE DT_NODELABEL(avpll_video)

/* Audio PLL: F_OUT = 49.152 MHz (48 kHz audio family). Per RM 117.4.4.1 audio-mode algorithm:
 *   F_AVPLL    = 16 x 49.152 = 786.432 MHz
 *   POSTDIV    = 16
 *   dBAND      = (786.432 - 722.5344) / 6 = 10.6496 MHz
 *   CCO band   = ROUND((786.432 - 722.5344) / dBAND) = 6  (Table 802 row 110b: 786.432 MHz)
 *   DNUM       = 0  (audio mode: no fractional offset; LOOPDIV auto-computed via lookup)
 */
static const clock_cguana_avpll_config_t s_audioPllConfig = {
	.refFreq = kCLOCK_CguanaRefFreq24M,
	.ccoBandSel = DT_PROP(AVPLL_AUDIO_NODE, nxp_cco_band_sel),
	.postDivRatio = DT_PROP(AVPLL_AUDIO_NODE, nxp_post_div_ratio),
	.dnum = DT_PROP(AVPLL_AUDIO_NODE, nxp_dnum),
	.sscgEn = false,
	.sscg = NULL,
};

/* Video PLL: F_OUT = 70.64 MHz (display panel reference). Per RM 117.4.5.1 video-mode algorithm:
 *   POSTDIV    = ROUND(759.808 / 70.64)        = 11
 *   F_AVPLL    = 70.64 x 11                    = 777.04 MHz
 *   CCO band   = ROUND((777.04 - 722.5344) / dBAND) = 5  (Table 802 row 101b: 775.7824 MHz)
 *   F_VCO_CAL  = 5 x 10.6496 + 722.5344        = 775.7824 MHz
 *   LOOPDIV    = floor(F_VCO_CAL / 24)         = 32  (auto-determined by PLL lookup table)
 *   D_NUM      = (777.04 / 24) - 32            = 113/300 ~= 0.376667
 *   DNUM[29:0] = ROUND(D_NUM x 2^30)            = 404,442,754 = 0x181B4E82
 * Sanity-check: 24 x (32 + 0x181B4E82 / 2^30) / 11 = 70.640000 MHz OK
 */
static const clock_cguana_avpll_config_t s_videoPllConfig = {
	.refFreq = kCLOCK_CguanaRefFreq24M,
	.ccoBandSel = DT_PROP(AVPLL_VIDEO_NODE, nxp_cco_band_sel),
	.postDivRatio = DT_PROP(AVPLL_VIDEO_NODE, nxp_post_div_ratio),
	/* dnum: (113/300) x 2^30 = 404,442,754 -> 70.64 MHz */
	.dnum = DT_PROP(AVPLL_VIDEO_NODE, nxp_dnum),
	.sscgEn = false,
	.sscg = NULL,
};

/*******************************************************************************
 ************************ BOARD_InitBootClocks function ************************
 *****************************************************************************
 */
/* Defined below; only soc_clock_init() enters it, through the power transition. */
static void soc_clock_apply_od_run(void);

void soc_clock_init(void)
{
	POWER_EnterHpRun(soc_clock_apply_od_run);
}

/*******************************************************************************
 ************************ Configuration soc_clock_apply_od_run *******************
 *****************************************************************************
 */
/* Static helper prototypes -- see definitions after the entry functions. */
static void ConfigCGUAna(void);
/* CGUDig is decomposed as: one shared block that programs every CCM slice that is
 * mode-invariant (MODCON, PLL divider roots, PERI0-7, ETH, AUDIOBUS, COMMBUS,
 * WAKEBUS, CMPT, MAIN, MEDIA, AUDIO, COMM, WAKE domain slices) + one per-mode
 * block that programs the three CGU slices whose source/divider depend on the
 * operating point (CGU_MAIN_ROOTCLK slice 30, CGU_NPU_ROOTCLK slice 31,
 * CGU_MEDIABUS_ROOTCLK slice 32). This port carries only Over Drive Run, so
 * soc_clock_apply_od_run() is the single caller: shared block, then its own
 * per-mode block.
 */
static void ConfigCGUDig_SS(void);
static void ConfigCGUDig_SYSCON_common(void);
static void ConfigCGUDig_SYSCON_HPRUN(void);
static void ConfigCGUDig_CMPT(void);
static void ConfigCGUDig_MAIN(void);
static void ConfigCGUDig_MEDIA(void);
static void ConfigCGUDig_AUDIO(void);
static void ConfigCGUDig_COMM(void);
static void ConfigCGUDig_WAKE(void);

/* XSPI clock-switch step for the two per-controller window helpers below. Each helper is
 * called twice by soc_clock_handover_to_sxosc -- once to park the controller before
 * the PLL work, once to move it onto the re-locked Sys PLL afterwards.
 */
typedef enum _board_xspi_clock_step {
	/*!<
	 * Quiesce + disable the XSPI, park its clock on FRO192M (window entry,
	 * before any PLL is touched).
	 */
	kSocXspiParkOnFro192m = 0U,
	/*!<
	 * Move the XSPI clock to SysPLL DIV4 and re-enable it (window exit,
	 * after the SYS PLL has re-locked).
	 */
	kSocXspiRunOnSysPllDiv4,
} soc_xspi_park_step_t;

/*
 * The sys PLL register images the hand-over programs, in DTCM.
 *
 * Computed before the window opens, while the flash this code executes from is
 * still readable, and read inside it once neither external memory is available.
 */
static uint32_t __dtcm_noinit_section soc_clock_syspll_regs[4];

#define SOC_CLOCK_WINDOW_STACK_SIZE 512

static uint8_t __dtcm_noinit_section
	__aligned(8) soc_clock_window_stack[SOC_CLOCK_WINDOW_STACK_SIZE];

/* XSPI0 (NOR XIP boot flash) window handling: one ITCM-resident helper with two steps,
 * called twice by soc_clock_handover_to_sxosc. It must be ITCM-resident (__itcm_section)
 * because the CPU cannot fetch from the flash whose clock is being cut over.
 *
 *   kSocXspiParkOnFro192m (window step 2): ensure the MAIN-CCM gate is on, quiesce the
 *     controller (block new AHB prefetch, abort in-flight ones, then bounded-wait for idle
 *     -- MDIS'ing mid-access wedges XIP, because the NOR drops continuous-read and never
 *     recovers), disable it (MDIS), then park its clock on FRO192M via PERI_ROOTCLK0 ->
 *     BASE. Both sources are live at the hop, so the mux switch is glitchless. Only MUX is
 *     touched; DIV/SND_DIV keep their ROM values, since nothing fetches while parked.
 *
 *   kSocXspiRunOnSysPllDiv4 (window step 8): switch the final fclock to SYSPLLDIV4
 *     (500 MHz), setting MUX + DIV + SND_DIV together. The slice has two dividers, and the
 *     internal 2x clock MUST be twice SCK for the read datapath's DQS-pad loopback
 *     sampling -- SND_DIV=1 makes 2x == SCK and corrupts reads. div=2 -> 2x=250 MHz,
 *     sndDiv=2 -> SCK=125 MHz. Then restore prefetch and perform the window's single
 *     re-enable, so XIP resumes on the new clock with its final config in place.
 *
 * Exactly one disable (park) and one enable (run); MDIS retains all controller config.
 */
static void __itcm_section soc_clock_park_xspi0(soc_xspi_park_step_t step)
{
	uint32_t v;
	uint32_t i;

	if (step == kSocXspiParkOnFro192m) {
		/* Ensure the XSPI0 clock gate is ON (CGC_ROOT) before its clock roots are touched.
		 */
		MAIN__CCM->CGC_ROOT[(uint32_t)kCLOCK_MAIN_xspi0 - (uint32_t)kCLOCK_MAIN_START]
			.SLICE_CONTROL |= CCM_SLICE_CONTROL_LPCG_CFG_MASK;
		__DSB();
		__ISB();

		/* Quiesce: block new AHB read-prefetch, abort in-flight, bounded wait for idle. */
		MAIN__XSPI_0->SPTRCLR |= XSPI_SPTRCLR_PREFETCH_DIS_MASK;
		MAIN__XSPI_0->SPTRCLR |= XSPI_SPTRCLR_ABRT_CLR_MASK;
		__DSB();
		__ISB();
		for (i = 0U; ((MAIN__XSPI_0->SR & (XSPI_SR_BUSY_MASK | XSPI_SR_AHB_ACC_MASK |
						   XSPI_SR_IP_ACC_MASK)) != 0U) &&
			     (i < 1000000U);
		     i++) {
		}

		/* Now safe to disable; stays disabled until the kSocXspiRunOnSysPllDiv4 step. */
		MAIN__XSPI_0->MCR |= XSPI_MCR_MDIS_MASK;
		__DSB();
		__ISB();

		/* Park the clock path on FRO192M: PERI_ROOTCLK0 -> BASE, xspi0_fclk -> mux0. */
		v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_PERI_ROOTCLK0].SLICE_CONTROL;
		v &= ~CCM_SLICE_CONTROL_MUX_MASK;
		v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_PERI0_ClockRoot_BASE);
		SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_PERI_ROOTCLK0].SLICE_CONTROL = v;

		v = MAIN__CCM->CLOCK_ROOT[1].SLICE_CONTROL; /* xspi0_fclk */
		v &= ~CCM_SLICE_CONTROL_MUX_MASK;
		v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_XSPI0_ClockRoot_MAIN_PERI0_DIV2);
		MAIN__CCM->CLOCK_ROOT[1].SLICE_CONTROL = v;
		__DSB();
		__ISB();
		(void)MAIN__CCM->CLOCK_ROOT[1]
			.SLICE_CONTROL; /* CM85 posted-write read-back flush */
	} else {
		/* Final fclock: SYSPLLDIV4 (500 MHz), div=2 -> 2x=250 MHz, sndDiv=2 -> SCK=125 MHz.
		 */
		v = MAIN__CCM->CLOCK_ROOT[1].SLICE_CONTROL;
		v &= ~(CCM_SLICE_CONTROL_MUX_MASK | CCM_SLICE_CONTROL_DIV_MASK |
		       CCM_SLICE_CONTROL_SND_DIV_MASK);
		v |= CCM_SLICE_CONTROL_MUX(2U) | CCM_SLICE_CONTROL_DIV(2U - 1U) |
		     CCM_SLICE_CONTROL_SND_DIV(2U - 1U);
		MAIN__CCM->CLOCK_ROOT[1].SLICE_CONTROL = v;
		__DSB();
		__ISB();
		(void)MAIN__CCM->CLOCK_ROOT[1]
			.SLICE_CONTROL; /* CM85 posted-write read-back flush */

		/* Restore normal prefetch FIRST (still disabled), THEN the window's single
		 * re-enable.
		 */
		MAIN__XSPI_0->SPTRCLR &= ~XSPI_SPTRCLR_PREFETCH_DIS_MASK;
		MAIN__XSPI_0->MCR &= ~XSPI_MCR_MDIS_MASK;
		__DSB();
		__ISB();
	}
}

/* XSPI1 (DDR PSRAM) window handling -- mirror of soc_clock_park_xspi0, two steps:
 *
 *   kSocXspiParkOnFro192m (window step 2): gate on (esp. the xspi_nor build, where XSPI1
 *     is gated by default), quiesce, MDIS, then park PERI_ROOTCLK1 -> BASE and the
 *     xspi1_fclk slice on mux0 (MAIN_PERI1_DIV2 <- PERI_ROOTCLK1). MCR config (incl.
 *     X16_EN) is retained across MDIS.
 *
 *   kSocXspiRunOnSysPllDiv4 (window step 8): switch the final fclock to SYSPLLDIV4, copying
 *     ROM's dividers EXACTLY so the PSRAM frequency (and thus the ROM DLL calibration
 *     point) is preserved: SYSPLLDIV4 (500 MHz) == ROM's MAIN_PERI1_DIV2 (500 MHz),
 *     DIV(/1) -> 2x = 500 MHz, SND_DIV(/2) -> SCK = 250 MHz. DIV/SND_DIV are (value-1)
 *     encoded. The clock SOURCE still changes (Main PLL -> Sys PLL), so even at the same
 *     SCK the DDR read strobe the ROM DLL locked to is no longer aligned (single-beat /
 *     non-cacheable reads fail) -- therefore RE-LOCK the controller DLL, ONLY if XSPI1
 *     holds a live DDR PSRAM that ROM already brought up (runtime gate on MCR.X16_EN, not
 *     a build macro: a DDR DLL re-lock on a non-DDR XSPI1 would be wrong). DLLCR[0] is
 *     zeroed to drop the stale lock then re-armed to the validated working-state auto-DLL
 *     value (0xC260001C = DLLEN|FREQEN|REFCNTR2|RES6|CDL8|AUTO_UPD|SLV_EN); SMPR tap = 4.
 *     The SDK XSPI_UpdateDllValue is deliberately NOT used (below its auto-update
 *     threshold it drops FREQEN and never sets CDL8, giving a different DLLCR). No Global
 *     Reset / device re-init, so PSRAM contents (incl. psram_txt code) are preserved;
 *     bounded lock wait so a dead board cannot hang boot. Then restore prefetch, perform
 *     the single re-enable, and (live PSRAM only) pulse a serial soft-reset to settle the
 *     interface on the new clock.
 *
 * MUST be RAM-resident: for psram_txt (code executes from PSRAM) the CPU would otherwise
 * fetch over XSPI1 while its clock is cut over and while the DLL re-locks.
 */
static void __itcm_section soc_clock_park_xspi1(soc_xspi_park_step_t step)
{
	uint32_t v;
	uint32_t i;
	bool livePsram;

	if (step == kSocXspiParkOnFro192m) {
		/* Ensure the XSPI1 clock gate is ON (CGC_ROOT) before its clock roots are touched.
		 */
		MAIN__CCM->CGC_ROOT[(uint32_t)kCLOCK_MAIN_xspi1 - (uint32_t)kCLOCK_MAIN_START]
			.SLICE_CONTROL |= CCM_SLICE_CONTROL_LPCG_CFG_MASK;
		__DSB();
		__ISB();

		/* Quiesce: block new AHB read-prefetch, abort in-flight, bounded wait for idle. */
		MAIN__XSPI_1->SPTRCLR |= XSPI_SPTRCLR_PREFETCH_DIS_MASK;
		MAIN__XSPI_1->SPTRCLR |= XSPI_SPTRCLR_ABRT_CLR_MASK;
		__DSB();
		__ISB();
		for (i = 0U; ((MAIN__XSPI_1->SR & (XSPI_SR_BUSY_MASK | XSPI_SR_AHB_ACC_MASK |
						   XSPI_SR_IP_ACC_MASK)) != 0U) &&
			     (i < 1000000U);
		     i++) {
		}

		/* Now safe to disable; stays disabled until the kSocXspiRunOnSysPllDiv4 step. */
		MAIN__XSPI_1->MCR |= XSPI_MCR_MDIS_MASK;
		__DSB();
		__ISB();

		/* Park the clock path on FRO192M: PERI_ROOTCLK1 -> BASE, xspi1_fclk -> mux0. */
		v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_PERI_ROOTCLK1].SLICE_CONTROL;
		v &= ~CCM_SLICE_CONTROL_MUX_MASK;
		v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_PERI1_ClockRoot_BASE);
		SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_PERI_ROOTCLK1].SLICE_CONTROL = v;

		v = MAIN__CCM->CLOCK_ROOT[2].SLICE_CONTROL; /* xspi1_fclk */
		v &= ~CCM_SLICE_CONTROL_MUX_MASK;
		v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_XSPI1_ClockRoot_MAIN_PERI1_DIV2);
		MAIN__CCM->CLOCK_ROOT[2].SLICE_CONTROL = v;
		__DSB();
		__ISB();
		(void)MAIN__CCM->CLOCK_ROOT[2]
			.SLICE_CONTROL; /* CM85 posted-write read-back flush */
	} else {
		livePsram = ((MAIN__XSPI_1->MCR & XSPI_MCR_X16_EN_MASK) !=
			     0U); /* ROM brought up DDR PSRAM? */

		/* Final fclock: SYSPLLDIV4 (500 MHz), DIV(/1) -> 2x = 500 MHz, SND_DIV(/2) -> SCK =
		 * 250 MHz.
		 */
		v = MAIN__CCM->CLOCK_ROOT[2].SLICE_CONTROL;
		v &= ~(CCM_SLICE_CONTROL_MUX_MASK | CCM_SLICE_CONTROL_DIV_MASK |
		       CCM_SLICE_CONTROL_SND_DIV_MASK);
		v |= CCM_SLICE_CONTROL_MUX(2U) | CCM_SLICE_CONTROL_DIV(1U - 1U) |
		     CCM_SLICE_CONTROL_SND_DIV(2U - 1U);
		MAIN__CCM->CLOCK_ROOT[2].SLICE_CONTROL = v;
		__DSB();
		__ISB();
		(void)MAIN__CCM->CLOCK_ROOT[2]
			.SLICE_CONTROL; /* CM85 posted-write read-back flush */

		/* Still MDIS'd: re-lock the DDR read DLL for the new clock SOURCE (live PSRAM
		 * only).
		 */
		if (livePsram) {
			MAIN__XSPI_1->DLLCR[0] = 0U; /* drop the stale (Main-PLL-clock) lock */
			MAIN__XSPI_1->DLLCR[0] =
				0xC260001CU; /* re-arm auto-DLL for the Sys-PLL clock */
			for (i = 0U; ((MAIN__XSPI_1->DLLSR & XSPI_DLLSR_SLVA_LOCK_MASK) == 0U) &&
				     (i < 100000U);
			     i++) {
			}
			MAIN__XSPI_1->SMPR =
				0x04000000U; /* DLLFSMPFA tap = 4 (working-state value) */
		}

		/* Restore normal prefetch FIRST (still disabled), THEN the window's single
		 * re-enable. For live PSRAM, a serial soft-reset pulse settles the interface on the
		 * new clock.
		 */
		MAIN__XSPI_1->SPTRCLR &= ~XSPI_SPTRCLR_PREFETCH_DIS_MASK;
		MAIN__XSPI_1->MCR &= ~XSPI_MCR_MDIS_MASK;
		if (livePsram) {
			MAIN__XSPI_1->MCR |= XSPI_MCR_SWRSTSD_MASK;
			MAIN__XSPI_1->MCR &= ~XSPI_MCR_SWRSTSD_MASK;
		}
		__DSB();
		__ISB();
	}
}

/* Select SXOSC as the OSC_24M (L0) source (MODCON CLK24M_SEL.SEL: 0 = FRO24M reset
 * default, 1 = SXOSC). This re-references the running CMS PLLs onto the crystal; the
 * XSPI0 XIP flash rides one of those PLLs out of boot ROM, so the switch can jitter
 * the flash clock. Do it from RAM with a direct register write so the CPU is not
 * fetching from XSPI0 flash across the transient.
 *
 * Equivalent to MODCON_SetCFG(kModCon_MAIN_CLK24M_SEL, 0, 0x1); the target register is
 * MAIN__MODCON->IP[getModConOffset(kModCon_MAIN_CLK24M_SEL)].CFG[0], a compile-time
 * constant address (no flash .rodata dependency). SXOSC must already be running
 * (CLOCK_InitSxosc) before this is called.
 */
static void __itcm_section soc_clock_select_sxosc_ref(void)
{
	MAIN__MODCON->IP[getModConOffset((uint32_t)kModCon_MAIN_CLK24M_SEL)].CFG[0] = 0x1U;

	__DSB();
	__ISB();
	(void)MAIN__MODCON->IP[getModConOffset((uint32_t)kModCon_MAIN_CLK24M_SEL)].CFG[0];
}

/* Precompute the four SYSPLL control-register images from the SYSPLL config while flash
 * is still accessible, so soc_clock_handover_to_sxosc (which runs with XSPI0
 * flash torn down) needs no .rodata read. Mirrors CGUANA_ConfigFracPllRegs; SYSPLL and
 * MAINPLL share the PLLn register layout, so the MAINPLL field macros apply to both.
 * PLL1's start-mode field is not programmed: the device headers dropped
 * MAINPLL_PLL_STARTING_MODE and CGUANA_ConfigFracPllRegs no longer writes it.
 * Flash-resident (no __itcm_section) -- called before the RAM window.
 */
static void soc_clock_compute_sys_pll_regs(const clock_cguana_frac_pll_config_t *cfg,
					   uint32_t *pll1, uint32_t *pll2, uint32_t *pll3,
					   uint32_t *pll4)
{
	*pll1 = (cfg->div5En ? CGUANA_CGUA_MAINPLL_PLL1_REG_MAINPLL_DIV5_EN_MASK : 0U) |
		(cfg->div8En ? CGUANA_CGUA_MAINPLL_PLL1_REG_MAINPLL_DIV8_EN_MASK : 0U) |
		(cfg->div10En ? CGUANA_CGUA_MAINPLL_PLL1_REG_MAINPLL_DIV10_EN_MASK : 0U) |
		(cfg->div20En ? CGUANA_CGUA_MAINPLL_PLL1_REG_MAINPLL_DIV20_EN_MASK : 0U);

	*pll2 = (cfg->fracDiv[0].en ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC0_EN_MASK : 0U) |
		(cfg->fracDiv[0].range ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC0_RANGE_MASK
				       : 0U) |
		CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC0_SEL(cfg->fracDiv[0].sel) |
		(cfg->fracDiv[1].en ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC1_EN_MASK : 0U) |
		(cfg->fracDiv[1].range ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC1_RANGE_MASK
				       : 0U) |
		CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC1_SEL(cfg->fracDiv[1].sel) |
		(cfg->fracDiv[2].en ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC2_EN_MASK : 0U) |
		(cfg->fracDiv[2].range ? CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC2_RANGE_MASK
				       : 0U) |
		CGUANA_CGUA_MAINPLL_PLL2_REG_MAINPLL_DIVFRAC2_SEL(cfg->fracDiv[2].sel);

	*pll3 = CGUANA_CGUA_MAINPLL_PLL3_REG_MAINPLL_FREF_SET((uint32_t)cfg->refFreq) |
		CGUANA_CGUA_MAINPLL_PLL3_REG_MAINPLL_LOWFREQ(cfg->lowFreq);

	/* s_sysPllConfig.sscgEn is false -> PLL4 = 0. (SSCG path intentionally omitted; the
	 * RAM window must not depend on the flash-resident sscg sub-struct.)
	 */
	*pll4 = 0U;
}

/* Glitchless crystal hand-over for the Core/Main/Sys (CMS) PLLs -- the whole hazardous
 * window. RAM-resident, direct register writes only: SDK driver code and its lookup tables
 * live in the XSPI0 flash that is torn down mid-window.
 *
 * The CMS PLL reference is CGUA_CTRL_REG.CMS_PLL_CKIN_SEL, not the MODCON CLK24M_SEL mux.
 * Flipping it while the ROM Main PLL is still running -- and feeding XIP flash and PSRAM --
 * glitches the PLL and it can fail to re-lock. So every CMS PLL is powered down first, the
 * reference switched with no running loop to lose, then the PLLs locked fresh on the crystal.
 *
 * Ordering constraint, do not reorder: both XSPI clock roots must be parked on FRO192M
 * before the core is parked, before the other bus roots are detached, and before any PLL is
 * powered down. XSPI0 (XIP NOR) and XSPI1 (PSRAM) can only be sourced from a Main/Sys-PLL
 * root, so perturbing their clock during the reference switch drops the device out of its
 * read state and XIP returns garbage on the way back to flash (seen as a hang with the PC
 * wandering in unprogrammed flash). Parking them on FRO192M -- an on-chip oscillator,
 * independent of every CMS PLL -- while the system is still fully clocked gives the flash
 * interface a clean transition. Doing it after the core/bus move corrupts XIP.
 *
 * SXOSC and FRO192M must already be up, brought up from flash before entry. Callers with
 * interrupts enabled must mask them around the call: both XSPI are disabled inside, so a
 * vector or handler fetch from flash/PSRAM mid-window would wedge XIP.
 *
 *   [0] Disable whichever L1 caches are on and turn the LLC off.
 *   [1] Force BASE_CLK -> FRO192M, the common park source for every BASE mux below.
 *   [2] Quiesce and disable (MDIS) both XSPI, park their roots on FRO192M. Both stay
 *       disabled until step 8; nothing touches XIP/PSRAM in between.
 *   [3] Park the CM85 core on FRO192M (MAIN_ROOTCLK -> BASE), which is also the XSPI bus
 *       clock, then detach the remaining CGU bus roots to FROs -- including
 *       SYSCON_PDMAIN_CLK, whose SYSPLL source would take the register bus down on re-entry.
 *   [4] Power down MAIN + CORE + SYS PLL (CGUANA FSM SW_OFF_REQ); wait for RDY to clear.
 *   [5] Switch the CMS and AV PLL reference to SXOSC, and OSC_24M with it.
 *   [6] Re-program and power up SYS PLL on the crystal reference; wait for FSM RDY.
 *   [7] Bring up SYSPLLDIV4_ROOTCLK (500 MHz), the shared XSPI hop target.
 *   [8] Hop both still-disabled XSPI fclocks onto SysPLL DIV4 and re-enable each once, so
 *       both are independent of PERI_ROOTCLK0/1 before the post-window code reprograms
 *       those. On return XIP flash and PSRAM are alive.
 *   [9] Restore the LLC policy and the L1 caches saved in step 0.
 *
 * Core PLL and Main PLL are deliberately left down; the flash-resident caller re-inits them
 * and the AV PLLs once XSPI rides Sys PLL.
 */
static void __itcm_section soc_clock_handover_to_sxosc(void)
{
	/* cmsFsm = the CGUANA FSM bit-set for the three Core/Main/Sys PLLs, powered down as one
	 * group for the reference switch so NONE of them can pass the reference-switch glitch to
	 * a consumer. XSPI0/XSPI1 do NOT ride a PLL during the switch -- they keep running on
	 * FRO192M (parked in step 2), which is why XIP/PSRAM survive the window.
	 */
	const uint32_t cmsFsm =
		CLOCK_CGUANA_FSM_MAINPLL | CLOCK_CGUANA_FSM_COREPLL | CLOCK_CGUANA_FSM_SYSPLL;
	uint32_t v;
	uint32_t i;
	uint32_t llcCtcr;
	bool dCacheOn;
	bool iCacheOn;

	/* [Step 0] Disable the L1 D/I caches around the window (only the ones currently on, so the
	 *    caller's cache policy comes back unchanged -- soc.c enables the i-cache before it
	 *    calls in here; SCB_Disable*Cache are CMSIS forced-inline, so
	 *    this code stays ITCM-resident), then turn the LLC (last-level cache) off. The LLC
	 *    caches the PSRAM aperture; a fill mid-clock-change could cache corrupt data. The
	 *    LLC write MUST run from ITCM (this function), NOT from the flash/PSRAM-resident
	 *    caller: in the psram_txt build the caller executes from the very PSRAM path the
	 *    write turns off, and the next fetch after LOOKUPEN|FILLEN clear bus-faults
	 *    (HW-confirmed release-build bootloop). From ITCM no LLC-path fetch happens while
	 *    it is off. Prior policy is restored in step 9.
	 */
	dCacheOn = ((SCB->CCR & SCB_CCR_DC_Msk) != 0U);
	iCacheOn = ((SCB->CCR & SCB_CCR_IC_Msk) != 0U);
	if (dCacheOn) {
		SCB_DisableDCache(); /* clean + invalidate + disable L1 D-cache */
	}
	if (iCacheOn) {
		SCB_DisableICache();
	}
	llcCtcr = CMPT__LLC->CCUCTCR;
	CMPT__LLC->CCUCTCR = llcCtcr & ~(LLC_CCUCTCR_LOOKUPEN_MASK | LLC_CCUCTCR_FILLEN_MASK);
	__DSB();
	__ISB();

	/* [Step 1] Force BASE_CLK -> FRO_192M first. BASE_CLK is the shared "BASE" mux input for
	 *    every root parked below (the XSPI PERI roots in step 2, the core + bus roots in
	 *    step 3), so it must point at the free-running FRO192M before any of them select
	 *    BASE. Read-back + DSB/ISB order the posted write before the dependent writes.
	 */
	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_BASE_CLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_BASE_ClockRoot_FRO_192M);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_BASE_CLK].SLICE_CONTROL = v;
	__DSB();
	__ISB();
	(void)SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_BASE_CLK].SLICE_CONTROL;

	/* [Step 2] Park BOTH flash controllers on FRO192M first --
	 *    before the core/bus roots (step 3) and before any PLL/reference perturbation
	 *    (steps 4-6) -- while the whole system is still fully clocked and stable, so XSPI0
	 *    (NOR/XIP) and XSPI1 (PSRAM) slide onto a non-PLL clock with no interface glitch
	 *    and stay alive for the entire window. Each helper quiesces (MDIS'ing mid-access
	 *    wedges XIP), disables, and parks its controller; both stay MDIS'd until step 8.
	 */
	soc_clock_park_xspi0(kSocXspiParkOnFro192m);
	soc_clock_park_xspi1(kSocXspiParkOnFro192m);

	/* [Step 3a] NOW park the CM85 core on FRO192M: MAIN_ROOTCLK (CGU root 30) mux -> BASE.
	 *    The core is currently clocked from the ROM Main PLL via MAIN_ROOTCLK; moving it to
	 *    the free-running FRO192M makes the CPU immune to the PLL power-down (step 4) and the
	 *    reference switch (step 5). Glitchless live mux (both sources running). This runs
	 *    from ITCM, so the CPU keeps executing across the hop.
	 */
	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_MAIN_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_CGU_MAIN_ClockRoot_BASE);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_MAIN_ROOTCLK].SLICE_CONTROL = v;
	__DSB();
	__ISB();
	(void)SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_MAIN_ROOTCLK].SLICE_CONTROL;

	/* [Step 3b] Detach the remaining CMS-PLL-fed CGU bus roots onto FRO192M so nothing still
	 *     rides MAIN/CORE/SYS PLL when they are powered down in step 4. MAIN_ROOTCLK is
	 *     already on BASE (step 3a). NPU / MEDIABUS / AUDIOBUS / COMMBUS take mux=BASE
	 *     (BASE_CLK was forced to FRO192M in step 1); WAKEBUS has no BASE mux option so takes
	 *     its direct FRO_192M mux instead. MUX field only -- div/sndDiv are left as ROM set
	 *     them; each root's final PLL source is re-applied post-window by ConfigCGUDig_*.
	 */
	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_NPU_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_NPU_ClockRoot_BASE);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_NPU_ROOTCLK].SLICE_CONTROL = v;

	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_MEDIABUS_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_MEDIABUS_ClockRoot_BASE);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_MEDIABUS_ROOTCLK].SLICE_CONTROL = v;

	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_AUDIOBUS_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_AUDIOBUS_ClockRoot_BASE);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_AUDIOBUS_ROOTCLK].SLICE_CONTROL = v;

	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_COMMBUS_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_COMMBUS_ClockRoot_BASE);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_COMMBUS_ROOTCLK].SLICE_CONTROL = v;

	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_WAKEBUS_ROOTCLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_WAKEBUS_ClockRoot_FRO_192M);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_WAKEBUS_ROOTCLK].SLICE_CONTROL = v;

	/* SYSCON_PDMAIN_CLK (the SYSCON power-domain bus clock -- the very bus these
	 * SYSCON__CCM / SYSCON__CGUANA register writes ride) sits at its ROM default on the
	 * FIRST entry, but the post-window CGUDig tree moves it onto SYSPLL_DIV10. On any
	 * LATER entry (runtime power-mode switch, stress re-run) powering the SYS PLL down
	 * with this root still on it kills the SYSCON register bus mid-window and the chip
	 * resets. Park it on the free-running FRO_48M (no BASE mux option on this slice);
	 * ConfigCGUDig_SYSCON_common() moves it back to SYSPLL_DIV10 after the window.
	 */
	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_SYSCON_PDMAIN_CLK].SLICE_CONTROL;
	v &= ~CCM_SLICE_CONTROL_MUX_MASK;
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_SYSCON_PDMAIN_ClockRoot_FRO_48M);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_SYSCON_PDMAIN_CLK].SLICE_CONTROL = v;
	__DSB();
	__ISB();
	(void)SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_SYSCON_PDMAIN_CLK].SLICE_CONTROL;

	/* [Step 4] Power down MAIN + CORE + SYS PLL together (CGUANA FSM SW_OFF_REQ), then wait
	 *    (bounded loop) for their RDY bits to clear so the SYS PLL re-enable in step 6 is
	 *    race-free -- asserting SW_ON while a SW_OFF is still in flight can wedge the FSM.
	 *    With all three CMS PLLs off, the reference switch (step 5) has no running PLL loop
	 *    to glitch. The bounded wait means a stuck FSM cannot hang boot forever.
	 */
	v = SYSCON__CGUANA->CGUAD_CTRL_REG;
	v &= ~CGUANA_CGUAD_CTRL_REG_CGUAD_FSM_SW_ON_REQ(cmsFsm);
	v |= CGUANA_CGUAD_CTRL_REG_CGUAD_FSM_SW_OFF_REQ(cmsFsm);
	SYSCON__CGUANA->CGUAD_CTRL_REG = v;
	__DSB();
	__ISB();
	for (i = 0U; ((SYSCON__CGUANA->CGUAD_CTRL_STS &
		       (cmsFsm & CGUANA_CGUAD_CTRL_STS_CGUAD_FSM_RDY_MASK)) != 0U) &&
		     (i < 1000000U);
	     i++) {
	}

	/* [Step 5] Switch the reference of the CMS PLLs and the AV (Audio/Video) PLLs from the
	 *    FRO192M-derived 24M (CGUA_CTRL_REG.CMS_PLL_CKIN_SEL = 0, the reset default -- an RC
	 *    clock that can run several % off, scaling EVERY PLL output with it) to the SXOSC
	 *    crystal, via CGUA_CTRL_REG.{CMS,AV}_PLL_CKIN_SEL. This is glitchless because every
	 *    CMS PLL is already off (step 4) -- there is no running PLL to lose lock. Also point
	 *    OSC_24M at SXOSC (MODCON CLK24M_SEL) from RAM.
	 *    CRITICAL: the SXOSC-side reference path into the PLLs is the CLKGEN "PLLCKIN" output,
	 *    which has its own enable, CGUA_CLKGEN_PLL_CKIN_EN (0 out of reset). Selecting SXOSC
	 *    (CKIN_SEL=1) without enabling PLLCKIN leaves the PLLs with NO reference -- the SYS
	 *    PLL FSM then never reaches RDY in step 6 and boot dies. Set EN together with the
	 *    SELs.
	 */
	v = SYSCON__CGUANA->CGUA_CTRL_REG;
	v |= CGUANA_CGUA_CTRL_REG_CGUA_CLKGEN_PLL_CKIN_EN(1U);
	v |= CGUANA_CGUA_CTRL_REG_CGUA_CLKGEN_CMS_PLL_CKIN_SEL(1U);
	v |= CGUANA_CGUA_CTRL_REG_CGUA_CLKGEN_AV_PLL_CKIN_SEL(1U);
	SYSCON__CGUANA->CGUA_CTRL_REG = v;
	__DSB();
	__ISB();
	soc_clock_select_sxosc_ref(); /* MODCON CLK24M_SEL -> SXOSC */

	/* [Step 6] Re-program the SYS PLL registers (images precomputed from flash before the
	 *    window, read from soc_clock_syspll_regs, which is in DTCM) and power it back up on the
	 * new SXOSC reference, then wait (bounded) for its FSM RDY. Only SYS PLL is brought back
	 * here -- it is the XSPI hop target (step 7/8). Core PLL and Main PLL stay OFF; the
	 * flash-resident caller re-inits them after the window once XSPI no longer needs them.
	 */
	SYSCON__CGUANA->CGUA_SYSPLL_PLL1_REG = soc_clock_syspll_regs[0];
	SYSCON__CGUANA->CGUA_SYSPLL_PLL2_REG = soc_clock_syspll_regs[1];
	SYSCON__CGUANA->CGUA_SYSPLL_PLL3_REG = soc_clock_syspll_regs[2];
	SYSCON__CGUANA->CGUA_SYSPLL_PLL4_REG = soc_clock_syspll_regs[3];
	v = SYSCON__CGUANA->CGUAD_CTRL_REG;
	v &= ~CGUANA_CGUAD_CTRL_REG_CGUAD_FSM_SW_OFF_REQ(CLOCK_CGUANA_FSM_SYSPLL);
	v |= CGUANA_CGUAD_CTRL_REG_CGUAD_FSM_SW_ON_REQ(CLOCK_CGUANA_FSM_SYSPLL);
	SYSCON__CGUANA->CGUAD_CTRL_REG = v;
	for (i = 0U;
	     ((SYSCON__CGUANA->CGUAD_CTRL_STS &
	       (CLOCK_CGUANA_FSM_SYSPLL & CGUANA_CGUAD_CTRL_STS_CGUAD_FSM_RDY_MASK)) == 0U) &&
	     (i < 1000000U);
	     i++) {
	}

	/* [Step 7] Bring up SYSPLLDIV4_ROOTCLK -> SysPLL DIV4 (500 MHz), div=1. BOTH XSPI0 and
	 *    XSPI1 hop onto this in step 8: XSPI1 at 250 MHz (matches ROM PSRAM), XSPI0 at
	 *    125 MHz.
	 */
	v = SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_SYSPLLDIV4_ROOTCLK].SLICE_CONTROL;
	v &= ~(CCM_SLICE_CONTROL_MUX_MASK | CCM_SLICE_CONTROL_DIV_MASK);
	v |= CCM_SLICE_CONTROL_MUX((uint32_t)kCLOCK_SYSPLLDIV4_ClockRoot_SYSPLL_DIV4) |
	     CCM_SLICE_CONTROL_DIV(1U - 1U);
	SYSCON__CCM->CLOCK_ROOT[kCLOCK_Root_CGU_SYSPLLDIV4_ROOTCLK].SLICE_CONTROL = v;
	__DSB();
	__ISB();

	/* [Step 8] Hop both (still-disabled since step 2) XSPI fclocks onto the freshly-locked
	 *    SYS PLL, so they become independent of PERI_ROOTCLK0/1 before the flash-resident
	 *    post-window code reprograms those PERI roots. Each helper switches the final fclock
	 *    and then performs the window's single re-ENABLE (switch -> [DLL re-lock] -> enable
	 *    -> restore prefetch). On return, XSPI0 XIP and XSPI1 PSRAM are both alive.
	 */
	soc_clock_park_xspi0(kSocXspiRunOnSysPllDiv4);
	soc_clock_park_xspi1(kSocXspiRunOnSysPllDiv4);

	/* [Step 9] Restore the LLC policy saved in step 0 -- still from ITCM, and only now that
	 *    XIP + PSRAM are alive on their final Sys-PLL clocks, so post-window fetches re-fill
	 *    the LLC fresh. PSRAM contents did not change while the LLC was off (both XSPI were
	 *    MDIS'd and the core ran from ITCM), so no invalidate is needed. Then re-enable the
	 *    L1 caches that were on; they re-fill fresh from the memories at their final clocks.
	 */
	CMPT__LLC->CCUCTCR = llcCtcr;
	__DSB();
	__ISB();
	if (iCacheOn) {
		SCB_EnableICache();
	}
	if (dCacheOn) {
		SCB_EnableDCache();
	}
}

/*
 * A stack for the hand-over window, in DTCM.
 *
 * The window parks both XSPI controllers, and on this board zephyr,sram is the
 * XSPI1 PSRAM -- so the stack the rest of the system runs on is one of the
 * memories being parked. Pushing to it mid-window would fault. The SDK solves
 * the same problem by placing its stack in DTCM outright; Zephyr's stacks follow
 * zephyr,sram, so instead the window borrows this one for the critical section
 * and hands the original back afterwards.
 *
 * 512 bytes is ample: the window calls at most three levels deep and spills only
 * the register images it was handed.
 */

/*
 * Run the hand-over on the DTCM stack above.
 *
 * Naked, because the compiler must not emit a prologue that touches the outgoing
 * stack after it has been swapped. The body saves the caller's stack pointer in a
 * callee-saved register, switches to the top of the DTCM stack, calls the window,
 * then restores. r0..r3 carry the four register images straight through to the
 * callee, so nothing has to be spilled across the switch.
 */
static void soc_clock_handover_on_dtcm_stack(void)
{
	/*
	 * One asm block, so nothing the compiler generates can land between the
	 * stack switch and the call.
	 *
	 * The old stack pointer goes to r12 rather than being pushed, and r12 and
	 * the return address are saved on the DTCM stack -- so from the switch
	 * onwards nothing reads or writes PSRAM. Pushing them on the way in would
	 * have written to PSRAM immediately before the window turns the caches off
	 * and parks XSPI1, which is the access this exists to avoid. r12 is
	 * call-clobbered, so borrowing it costs nothing.
	 *
	 * The window takes no arguments -- it reads soc_clock_syspll_regs, which
	 * is in DTCM -- so there is nothing to marshal across the switch.
	 */
	__asm__ volatile("mov  r12, sp\n"
			 "mov  sp, %0\n"
			 "push {r12, lr}\n"
			 "blx  %1\n"
			 "pop  {r12, lr}\n"
			 "mov  sp, r12\n"
			 :
			 : "r"(&soc_clock_window_stack[SOC_CLOCK_WINDOW_STACK_SIZE]),
			   "r"(soc_clock_handover_to_sxosc)
			 : "r12", "lr", "memory");
}

/* Shared, mode-invariant prologue for the boot-clock setup: the hazard-ordered flow around
 * the RAM window. Takes no parameters -- everything here is identical across the run modes,
 * so it uses the file-scope s_*Config globals directly, and the per-mode tail (Core PLL,
 * Main PLL, the CGUDig tree) is done by the caller afterwards.
 *
 *   1) Pre-window, from flash: bring up the FROs and SXOSC (ConfigCGUAna), since the window
 *      parks on FRO192M and switches the PLL reference to SXOSC. Precompute the SYSPLL
 *      register images here too, because the window cannot read flash.
 *   2) The window: soc_clock_handover_to_sxosc. It does no interrupt masking of its
 *      own, so this function masks around the call -- the requirement travels with
 *      the window rather than with whoever calls this.
 *   3) Post-window, from flash: re-init the Audio/Video PLLs, whose reference the window
 *      switched to SXOSC. Core PLL and Main PLL stay deferred to the caller; XSPI rides Sys
 *      PLL by then, so bringing them up disturbs nothing.
 */
static void soc_clock_prepare(void)
{
	unsigned int key;

	/*
	 * The SDK sets the LPUART0 functional-clock root to the crystal here so
	 * its examples have a console before the PLLs exist. Zephyr's console
	 * comes up long after this hook, and the root belongs to the
	 * nxp,imx-ccm-rev3-root node in devicetree, so it is not set here.
	 */

	/* Step 1 (pre-window, flash): FROs + SXOSC. CLOCK_InitSxosc's FSM RDY wait ensures
	 * the crystal is stable before the window references it. This does not disturb the
	 * running ROM Main PLL that XSPI0/XSPI1 ride.
	 */
	ConfigCGUAna();

	/* Compute the SYSPLL register images while flash is up (the window is flash-free). */
	soc_clock_compute_sys_pll_regs(&s_sysPllConfig, &soc_clock_syspll_regs[0],
				       &soc_clock_syspll_regs[1], &soc_clock_syspll_regs[2],
				       &soc_clock_syspll_regs[3]);

	/* Step 2 (RAM window): glitchless crystal hand-over.
	 *
	 * Both XSPI controllers are disabled inside the window, so any instruction,
	 * vector or literal fetch that lands in external memory while it is open
	 * wedges XIP. The SDK leaves interrupt masking to the caller; do it here so
	 * the requirement travels with the window rather than with whoever calls
	 * this. The window functions themselves are __itcm_section, and the data they
	 * touch is on the stack or in .data.
	 */
	key = irq_lock();
	soc_clock_handover_on_dtcm_stack();
	irq_unlock(key);

	/* Step 3 (post-window, flash): AV PLLs lock fresh on the SXOSC reference. */
	CLOCK_InitAudioPll(&s_audioPllConfig);
	CLOCK_InitVideoPll(&s_videoPllConfig);
}

/* Over Drive Run FBB (HpRun): CM85=1000 (via PLL_PFDX), NPU=792 (from COREPLL_OUT).
 * CorePLL @ 792 MHz, MainPLL DIVOUT1 @ 1000 MHz -- the default analog config.
 */
static void soc_clock_apply_od_run(void)
{
	soc_clock_prepare();
	/* Per-mode tail -- config globals used directly. Both PLLs must be locked before
	 * ConfigCGUDig_SYSCON_common() (it sources PLL_PFDX <- MAINPLL_DIVOUT1 and
	 * COMMPFDX <- COREPLL_OUT).
	 */
	CLOCK_InitCorePll(&s_corePllConfig); /* 792 MHz */
	CLOCK_InitMainPll(&s_mainPllConfig); /* DIVOUT1 = 1000 MHz */
	ConfigCGUDig_SYSCON_common();        /* CGU slices identical across modes */
	ConfigCGUDig_SYSCON_HPRUN();         /* CGU slices 30/31/32 for HpRun */
	ConfigCGUDig_SS();                   /* CMPT / MAIN / MEDIA / AUDIO / COMM / WAKE */
}

/*******************************************************************************
 * Static helpers
 *****************************************************************************
 */

/* Bring up the PRE-WINDOW analog blocks: FRO12M, FRO192M and SXOSC. These are
 * mode-invariant, so this function takes no parameters and reads the file-scope
 * s_*Config globals directly.
 *
 * Everything else moved out of here relative to the original bring-up:
 *   - The CMS/AV PLL reference switch (formerly CLOCK_SetCmsPllRefSource /
 *     CLOCK_SetAvPllRefSource here, on the live ROM Main PLL) is done inside the RAM
 *     window with all CMS PLLs powered down -- see soc_clock_handover_to_sxosc.
 *   - Sys PLL is re-programmed and re-locked inside the RAM window (it must lock on the
 *     new SXOSC reference before the XSPI hop).
 *   - Audio/Video PLLs are initialised post-window by soc_clock_prepare (their
 *     reference is switched inside the window, so they must lock after it).
 *   - Core PLL / Main PLL differ per operating point and are initialised by
 *     soc_clock_apply_od_run() after soc_clock_prepare() returns.
 */
static void ConfigCGUAna(void)
{
	/* LPOSC_12M, LPOSC_1M and LPOSC_32K stay at reset; nothing here uses them. */

	/* FRO 12 MHz -- early safe fallback; brought up first so the rest of the analog
	 * bring-up always has a known-good clock available.
	 */
	CLOCK_InitFro12M(&s_fro12mConfig);

	/* FRO 192 MHz -- the core/bus/XSPI park clock for the RAM window. */
	CLOCK_InitFro192M(&s_fro192mConfig);

	/* SXOSC 24 MHz crystal -- the new PLL reference; the FSM RDY wait inside
	 * CLOCK_InitSxosc ensures it is stable before the RAM window references it.
	 */
	CLOCK_InitSxosc(&s_sxoscConfig);
}

/* The six subsystem-domain helpers, all mode-invariant. The per-mode CGU slices
 * (30 MAIN_ROOTCLK, 31 NPU_ROOTCLK, 32 MEDIABUS_ROOTCLK) are programmed
 * separately by ConfigCGUDig_SYSCON_HPRUN() -- see soc_clock_apply_od_run().
 *
 * The CMPT / MAIN / MEDIA domain slices track their upstream CGU roots via
 * mux=MAIN/NPU/MEDIABUS with div!=1 (e.g. CMPT.main_clk_divided = MAIN/3), so
 * they follow whatever the per-mode CGU root is set to -- no per-mode variant of
 * CMPT/MAIN/MEDIA is needed.
 *
 * Each helper below keeps one mutable rootCfg and reuses it across its
 * CLOCK_SetRootClock() calls, with sndDiv = 1 set once as a baseline:
 * CLOCK_SetRootClock writes div and sndDiv using a (value-1) encoding, so a
 * zero-initialised sndDiv would write an all-ones SND_DIV field and corrupt the
 * secondary divider on any root that uses it.
 */
static void ConfigCGUDig_SS(void)
{
	ConfigCGUDig_MEDIA();
	ConfigCGUDig_AUDIO();
	ConfigCGUDig_CMPT();
	ConfigCGUDig_MAIN();
	ConfigCGUDig_COMM();
	ConfigCGUDig_WAKE();
}

/*
 * The 49 SYSCON_CCM slices, in slice-index order. Slices with a single possible
 * source (BASE, LOW, the SAI master clocks, SXOSC and the LPOSC fan-out) are set
 * here because there is nothing to choose; the PLL-distribution slices come from
 * their nxp,imx-ccm-rev3-root nodes, which is where their values live. Slices no
 * consumer in this baseline needs are left at reset.
 */
static void ConfigCGUDig_SYSCON_common(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	/*
	 * MODCON clock-tree muxes. The six Mx /2 selects feed XSPI0/XSPI1 (MAIN) and
	 * USDHC0/USDHC1 (COMM); default them to pass-through so consumers see the
	 * full root frequency. OSC_24M is not routed here -- the RAM window already
	 * switched it to SXOSC with the core parked.
	 */
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_MAINPFDX_DIV2, false);
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_MAIN_PERI0_DIV2, false);
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_MAIN_PERI1_DIV2, false);
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_COMMPFDX_DIV2, false);
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_COMM_PERI1_DIV2, false);
	CLOCK_SetClockSrcDiv2(kCLOCK_SRC_COMM_PERI2_DIV2, false);

	rootCfg.mux = kCLOCK_BASE_ClockRoot_FRO_192M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_BASE_CLK, &rootCfg);

	rootCfg.mux = kCLOCK_LOW_ClockRoot_FRO_24M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_LOW_CLK, &rootCfg);

	/* PLL-distribution and PFD layer: steady values from devicetree. */
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(mainpll_divx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(syspll_divx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(pll_pfdx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(media_pfdx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(mainpfdx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(commpfdx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(maindivx_rootclk));

	rootCfg.mux = kCLOCK_SAIMCLK_ClockRoot_SAI1_MCLK;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_SAIMCLK_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_SAIMCLK0_ClockRoot_SAI0_MCLK;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_SAIMCLK0_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_SAIMCLK1_ClockRoot_SAI1_MCLK;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_SAIMCLK1_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_SAIMCLK2_ClockRoot_SAI2_MCLK;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_SAIMCLK2_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_SXOSC_ClockRoot_OSC_24M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_SXOSC_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_LP12M_CORE_ClockRoot_LPOSC_12M_CORE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_LP12M_CORE_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_LP1M_CORE_ClockRoot_LPOSC_1M_CORE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_LP1M_CORE_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_ULP32K_ClockRoot_LPOSC32K;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_ULP32K_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_FRO192M_ClockRoot_FRO_192M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_FRO192M_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_FRO96M_ClockRoot_FRO_96M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_FRO96M_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_FRO48M_ClockRoot_FRO_48M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_FRO48M_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_FRO24M_ClockRoot_FRO_24M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_FRO24M_ROOTCLK, &rootCfg);

	/* SYSPLL/MAINPLL distribution roots on the core/bus/XSPI path: from devicetree. */
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(sysplldiv4_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(sysplldiv5_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(sysplldivx_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(mainplldivx_rootclk));

	rootCfg.mux = kCLOCK_MAINPLLDIV8_ClockRoot_MAINPLL_DIV8;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_MAINPLLDIV8_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_MAINPLLDIV10_ClockRoot_MAINPLL_DIV10;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_MAINPLLDIV10_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_MAINPLLDIV20_ClockRoot_MAINPLL_DIV20;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_MAINPLLDIV20_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_AUDIOPLL_ClockRoot_AUDIOPLL_DIVOUT;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_AUDIOPLL_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_VIDEOPLL_ClockRoot_VIDEOPLL_DIVOUT;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_VIDEOPLL_ROOTCLK, &rootCfg);

	/* CGU slices 30 (MAIN_ROOTCLK / CM85), 31 (NPU_ROOTCLK), 32 (MEDIABUS_ROOTCLK)
	 * are programmed by ConfigCGUDig_SYSCON_HPRUN() -- they are the per-mode
	 * slices, and only HP RUN is carried here.
	 */

	/* Bus roots (33-36): steady values from devicetree. */
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(audiobus_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(commbus_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(wakebus_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(syscon_pdmain_rootclk));

	rootCfg.mux = kCLOCK_PERI0_ClockRoot_MAINPLL_DIVOUT0;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK0, &rootCfg);

	rootCfg.mux = kCLOCK_PERI1_ClockRoot_MAINPLL_DIVOUT1;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK1, &rootCfg);

	rootCfg.mux = kCLOCK_PERI2_ClockRoot_MAINPLL_DIVOUT2;
	rootCfg.div = 2U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK2, &rootCfg);

	/* PERI3 feeds the console LPUART; steady value from devicetree. */
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(peri3_rootclk));

	rootCfg.mux = kCLOCK_PERI4_ClockRoot_MAINPLL_DIVX;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK4, &rootCfg);

	rootCfg.mux = kCLOCK_PERI5_ClockRoot_MAINPLL_DIVX;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK5, &rootCfg);

	rootCfg.mux = kCLOCK_PERI6_ClockRoot_BASE;
	rootCfg.div = 8U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK6, &rootCfg);

	rootCfg.mux = kCLOCK_PERI7_ClockRoot_FRO_192M;
	rootCfg.div = 5U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_PERI_ROOTCLK7, &rootCfg);

	rootCfg.mux = kCLOCK_AUDIO_ClockRoot_LOW;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_AUDIO_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_VIDEO_ClockRoot_BASE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_VIDEO_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_USB1_ClockRoot_FRO_48M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_USB1_ROOTCLK, &rootCfg);

	rootCfg.mux = kCLOCK_ETH_ClockRoot_SYSPLL_DIV20;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CGU_ETH_ROOTCLK, &rootCfg);
}

/* Per-mode CGU slices for HpRun (Over Drive Run FBB) -- Jira table column 1:
 *
 *  Slice 30  MAIN_ROOTCLK      PLL_PFDX          -> 1000 MHz  (CM85)
 *  Slice 31  NPU_ROOTCLK       COREPLL_OUT       -> 792 MHz   (NPU; spec-nominal 800)
 *  Slice 32  MEDIABUS_ROOTCLK  PLL_PFDX / 3      -> 333 MHz   (MEDIA)
 *
 * Downstream: CMPT.cpu_clk = MAIN_ROOTCLK/1 = 1000 (CM85 CPU clock);
 * CMPT.cmpt_clk = MAIN_ROOTCLK/1 with sndDiv=1 -> also 1000; MAIN.main_clk_divided
 * = MAIN_ROOTCLK/3 -> 333 MHz (MAIN bus / CMPT bus target).
 */
static void ConfigCGUDig_SYSCON_HPRUN(void)
{
	/* The three per-mode slices come from devicetree:
	 *   root 30 MAIN     -- two dividers: div -> CPU_ROOTCLK (CM85 core), sndDiv ->
	 *                       MAIN_ROOTCLK (MAIN bus). HpRun: CM85 = 1000, MAIN = 333 MHz,
	 *                       so clock-div=<1> / clock-second-div=<3>. Uses the *_SD macro.
	 *   root 31 NPU      -- COREPLL_OUT (= 792 MHz per s_corePllConfig).
	 *   root 32 MEDIABUS -- PLL_PFDX (=1000 MHz) / 3 = 333.33 MHz.
	 */
	SOC_SET_ROOT_CLOCK_SD(DT_NODELABEL(main_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(npu_rootclk));
	SOC_SET_ROOT_CLOCK(DT_NODELABEL(mediabus_rootclk));
}

/*
 * CMPT domain clock roots (5 slices on CMPT_CCM). Slice 3 is listed for
 * completeness but is not set here -- see the note in the body.
 *
 *  Slice  Root Name       Mux  Source   Div  Yield
 *  -----  -------------  ---  ------  ---  ----------
 *     0   cmpt_clk         0  MAIN      1  1000 MHz
 *     1   cpu_clk          0  CPU       1  1000 MHz
 *     2   npu_clk          0  NPU       1  792 MHz
 *     3   systick_clk0     1  SXOSC     1  24 MHz   (from devicetree)
 *     4   systick_clk1     1  SXOSC     1  24 MHz
 */
static void ConfigCGUDig_CMPT(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	rootCfg.mux = kCLOCK_CMPT_ClockRoot_MAIN;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CMPT_cmpt_clk, &rootCfg);

	rootCfg.mux = kCLOCK_CPU_ClockRoot_CPU;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CMPT_cpu_clk, &rootCfg);

	rootCfg.mux = kCLOCK_NPU_ClockRoot_NPU;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CMPT_npu_clk, &rootCfg);

	/*
	 * systick_clk0 (SXOSC = 24 MHz) is the secure SysTick reference and is
	 * programmed from its nxp,imx-ccm-rev3-root devicetree node, which is
	 * also where CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC has to agree with it.
	 * systick_clk1 below is the non-secure reference; this image runs
	 * secure, so nothing consumes it and it stays here.
	 */

	rootCfg.mux = kCLOCK_SYSTICK1_ClockRoot_SXOSC;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_CMPT_systick_clk1, &rootCfg);
}

/*
 * MAIN domain (37 slices on MAIN_CCM). Only the two roots that no single
 * device owns are set here: fro192m (FRO192M) and ulp32k (ULP32K).
 *
 * Of the per-peripheral leaf roots, the LPUART ones are carried as an inline
 * "source" clocks entry on each lpuart node and programmed by that driver at
 * init; the XSPI ones are declared the same way but written by the hand-over
 * window above. The rest (lpspi, lpi2c, i3c, flexcan, flexio, adc, lpit, qtpm,
 * sinc, tpiu, cssi, otp, clkout) are described nowhere yet and keep their reset
 * values -- add a "source" entry, or a MAIN_CCM root child, when a driver needs
 * a rate other than that.
 */
static void ConfigCGUDig_MAIN(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	/*
	 * Only the shared upstream/distribution roots that no single device owns
	 * are configured in this file. Every per-peripheral functional-clock root
	 * is described by its device's nxp,imx-ccm-rev3-root devicetree entry and
	 * programmed by that device's driver at init, so devicetree is the single
	 * source of truth and a board or application overlay can retarget them.
	 */

	rootCfg.mux = kCLOCK_MAIN_MAIN_FRO192M_ClockRoot_FRO192M;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_MAIN_fro192m, &rootCfg);

	rootCfg.mux = kCLOCK_MAIN_MAIN_ULP32K_ClockRoot_ULP32K;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_MAIN_ulp32k, &rootCfg);
}

/*
 * MEDIA domain (10 slices on MEDIA_CCM). Only the two domain-base roots are
 * set here: media_clk (MEDIABUS) and mediapll_clk (MAINPLLDIV10). The
 * per-peripheral leaf roots (mipicsi, mipidsi, reformat, dcpixel, csi) are not
 * described anywhere yet and keep their reset values.
 */
static void ConfigCGUDig_MEDIA(void)
{
	POWER_SetDomainRunMode(kPOWER_DomainMedia, kPDCON_EventNoneOrActive);
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	rootCfg.mux = kCLOCK_MEDIA_ClockRoot_MEDIABUS;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_MEDIA_media_clk, &rootCfg);

	rootCfg.mux = kCLOCK_MEDIAPLL_ClockRoot_MAINPLLDIV10;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_MEDIA_mediapll_clk, &rootCfg);
}

/*
 * AUDIO domain (11 slices on AUDIO_CCM). Only the domain-base root is set
 * here: audio_clk (AUDIOBUS). The per-peripheral leaf roots (dmic, sai,
 * spdif, asrc) are not described anywhere yet and keep their reset values.
 */
static void ConfigCGUDig_AUDIO(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	rootCfg.mux = kCLOCK_AUDIO_CLK_ClockRoot_AUDIOBUS;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_AUDIO_audio_clk, &rootCfg);
}

/*
 * COMM domain (17 slices on COMM_CCM). Only the two domain-base roots are
 * set here: comm_clk (COMMBUS) and comm_ulp32k (ULP32K). The per-peripheral
 * leaf roots (usdhc, xspir, usb, eth, xeno, dll) are not described anywhere yet
 * and keep their reset values.
 */
static void ConfigCGUDig_COMM(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	rootCfg.mux = kCLOCK_COMM_ClockRoot_COMMBUS;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_COMM_comm_clk, &rootCfg);

	rootCfg.mux = kCLOCK_COMM_ULP32K_ClockRoot_ULP32K;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_COMM_comm_ulp32k, &rootCfg);
}

/*
 * WAKE domain (27 slices on WAKE_CCM), the always-on peripheral roots. Only the
 * six domain-base roots are set here: wake_clk (WAKEBUS), wake_sxosc (SXOSC),
 * wake_lp1m (LP1M_WAKE), wake_lp12m (LP12M_WAKE), wake_ulp32k (ULP32K),
 * wake_lpclk (LP1M_WAKE).
 *
 * Of the per-peripheral leaf roots, the two WAKE LPUARTs carry an inline
 * "source" clocks entry on their nodes, programmed by that driver at init; the
 * rest (lpspi, lpi2c, i3c, qtpm, lptmr, swt, ewm, acmp, dmic) are described
 * nowhere yet and keep their reset values.
 *
 * TBD (silicon / RM): several roots in this helper select a LP1M_WAKE,
 * LP12M_WAKE, or LP2M_WAKE source via mux. The corresponding CGU SS slice
 * entries (LP1M_WAKE_ROOTCLK, LP12M_WAKE_ROOTCLK, LP2M_WAKE_ROOTCLK in the
 * CLK_SLICES list) are marked `#feedthrough` -- i.e. silicon
 * implements the WAKE-domain LP signal but does NOT expose a software-
 * configurable slice for it. Per RM 117.4.7 the FRO_12M block outputs
 * CLK_FRO12M_WAKE (level-shifter version under VDD_0V8) and CLK_FRO1M
 * (12 MHz / 12), which we assume map to LP12M_WAKE and LP1M_WAKE
 * respectively. LP2M_WAKE has no obvious counterpart in RM 117.4.7 and
 * may be sourced from a separate always-on 2 MHz oscillator outside the
 * CGUANA scope (likely under VBAT/PMU).
 *
 * Please confirm with the IC team / RM clock tree diagram that:
 *   1. LP1M_WAKE, LP12M_WAKE, LP2M_WAKE signals are actually routed to
 *      the WAKE_CCM domain and live at boot time.
 *   2. The domain-base roots this helper sources from them (wake_lp1m,
 *      wake_lp12m, wake_lpclk) come up with a usable clock.
 *   3. If any of LP1M_WAKE / LP12M_WAKE / LP2M_WAKE need separate
 *      software bring-up (e.g. a PMU register), it must happen before
 *      this helper runs -- add that init to ConfigCGUAna().
 */
static void ConfigCGUDig_WAKE(void)
{
	clock_root_config_t rootCfg = {0};

	rootCfg.sndDiv = 1U;

	rootCfg.mux = kCLOCK_WAKE_ClockRoot_WAKEBUS;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_clk, &rootCfg);

	rootCfg.mux = kCLOCK_WAKE_SXOSC_ClockRoot_SXOSC;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_sxosc, &rootCfg);

	rootCfg.mux = kCLOCK_WAKE_LP1M_ClockRoot_LP1M_WAKE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_lp1m, &rootCfg);

	rootCfg.mux = kCLOCK_WAKE_LP12M_ClockRoot_LP12M_WAKE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_lp12m, &rootCfg);

	rootCfg.mux = kCLOCK_WAKE_ULP32K_ClockRoot_ULP32K;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_ulp32k, &rootCfg);

	rootCfg.mux = kCLOCK_WAKE_LPCLK_ClockRoot_LP1M_WAKE;
	rootCfg.div = 1U;
	CLOCK_SetRootClock(kCLOCK_Root_WAKE_wake_lpclk, &rootCfg);

	SystemCoreClockUpdate();
}
