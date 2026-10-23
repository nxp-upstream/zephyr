/*
 * SPDX-FileCopyrightText: Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <fsl_clock.h>
#include <fsl_pdcon.h>
#include <fsl_power.h>

#include "stay_awake.h"

/* PDCON numbers its domains from 0 to 5, as power_domain_t does. */
#define PDCON_DOMAIN_COUNT 6U

struct pdcon_domain {
	const struct device *dev;
	power_domain_t id;
};

enum {
	NPU_DOMAIN,
	COMM_DOMAIN,
	MEDIA_DOMAIN,
	TESTED_DOMAIN_COUNT,
};

#define PDCON_DOMAIN(node_label, domain_id)                                                        \
	{                                                                                          \
		.dev = DEVICE_DT_GET(DT_NODELABEL(node_label)),                                    \
		.id = (domain_id),                                                                 \
	}

static const struct pdcon_domain domains[TESTED_DOMAIN_COUNT] = {
	[NPU_DOMAIN] = PDCON_DOMAIN(pd_npu, kPOWER_DomainNpu),
	[COMM_DOMAIN] = PDCON_DOMAIN(pd_comm, kPOWER_DomainComm),
	[MEDIA_DOMAIN] = PDCON_DOMAIN(pd_media, kPOWER_DomainMedia),
};

/* A root soc_clock_init() programs in the CCM inside a domain. */
struct domain_root {
	const char *name;
	clock_root_t root;
	uint32_t boot_mux;
	CCM_Type *ccm;
	/* clock_root_t of the first root of that CCM */
	clock_root_t ccm_first_root;
};

/* The divider soc_clock_init() gives every domain-base root. */
#define DOMAIN_ROOT_BOOT_DIV 1U

/* domain is the HAL's name of the subsystem: COMM for COMM__CCM and kCLOCK_Root_COMM_*. */
#define DOMAIN_ROOT(domain, root_name, mux)                                                        \
	{                                                                                          \
		.name = STRINGIFY(root_name),                                                      \
		.root = kCLOCK_Root_##domain##_##root_name,                                        \
		.boot_mux = (mux),                                                                 \
		.ccm = domain##__CCM,                                                              \
		.ccm_first_root = kCLOCK_Root_##domain##_START,                                    \
	}

/* soc_clock.c, ConfigCGUDig_COMM() */
static const struct domain_root comm_roots[] = {
	DOMAIN_ROOT(COMM, comm_clk, kCLOCK_COMM_ClockRoot_COMMBUS),
	DOMAIN_ROOT(COMM, comm_ulp32k, kCLOCK_COMM_ULP32K_ClockRoot_ULP32K),
};

/* soc_clock.c, ConfigCGUDig_MEDIA() */
static const struct domain_root media_roots[] = {
	DOMAIN_ROOT(MEDIA, media_clk, kCLOCK_MEDIA_ClockRoot_MEDIABUS),
	DOMAIN_ROOT(MEDIA, mediapll_clk, kCLOCK_MEDIAPLL_ClockRoot_MAINPLLDIV10),
};

struct domain_boot_state {
	int ret;
	enum pm_device_state device_state;
	pdcon_event_t pdcon_state;
};

/* Read by the suite setup, before a test switches a domain and hides the boot state. */
static struct domain_boot_state boot_states[TESTED_DOMAIN_COUNT];

static bool domain_held[TESTED_DOMAIN_COUNT];

static pdcon_event_t pdcon_state(power_domain_t id)
{
	return PDCON_GetDomainState(SYSCON__PDCON, (uint8_t)id);
}

static void pdcon_states_read(pdcon_event_t states[PDCON_DOMAIN_COUNT])
{
	for (uint32_t id = 0U; id < PDCON_DOMAIN_COUNT; id++) {
		states[id] = pdcon_state((power_domain_t)id);
	}
}

static void hold_domain(size_t index)
{
	const struct pdcon_domain *domain = &domains[index];

	zassert_ok(pm_device_runtime_get(domain->dev), "Cannot power %s on", domain->dev->name);
	domain_held[index] = true;
}

static void release_domain(size_t index)
{
	const struct pdcon_domain *domain = &domains[index];
	int ret = pm_device_runtime_put(domain->dev);

	domain_held[index] = false;
	zassert_ok(ret, "Cannot power %s off", domain->dev->name);
}

static void assert_device_state(const struct pdcon_domain *domain, enum pm_device_state expected)
{
	enum pm_device_state state;

	zassert_ok(pm_device_state_get(domain->dev, &state), "No PM state for %s",
		   domain->dev->name);
	zassert_equal(state, expected, "%s is %s, expected %s", domain->dev->name,
		      pm_device_state_str(state), pm_device_state_str(expected));
}

static void assert_domain_on(const struct pdcon_domain *domain)
{
	assert_device_state(domain, PM_DEVICE_STATE_ACTIVE);
	zassert_equal(pdcon_state(domain->id), kPDCON_EventNoneOrActive,
		      "PDCON reports %s powered off", domain->dev->name);
}

static void assert_domain_off(const struct pdcon_domain *domain)
{
	assert_device_state(domain, PM_DEVICE_STATE_SUSPENDED);
	zassert_equal(pdcon_state(domain->id), kPDCON_EventPowerOff, "PDCON reports %s powered",
		      domain->dev->name);
}

static void assert_only_domain_switched(const pdcon_event_t before[PDCON_DOMAIN_COUNT],
					const pdcon_event_t after[PDCON_DOMAIN_COUNT],
					const struct pdcon_domain *switched)
{
	for (uint32_t id = 0U; id < PDCON_DOMAIN_COUNT; id++) {
		if (id == (uint32_t)switched->id) {
			continue;
		}

		zassert_equal(after[id], before[id],
			      "Switching %s moved PDCON domain %u from state %d to %d",
			      switched->dev->name, id, before[id], after[id]);
	}
}

static bool root_is_running(const struct domain_root *root)
{
	uint32_t status = root->ccm->CLOCK_ROOT[root->root - root->ccm_first_root].STATUS0;

	return (status & CCM_STATUS0_OFF_MASK) == 0U;
}

static void assert_roots_at_boot_setting(const struct domain_root *roots, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const struct domain_root *root = &roots[i];

		zassert_equal(CLOCK_GetRootClockMux(root->root), root->boot_mux,
			      "%s mux is %u, soc_clock_init() sets %u", root->name,
			      CLOCK_GetRootClockMux(root->root), root->boot_mux);
		zassert_equal(CLOCK_GetRootClockDiv(root->root), DOMAIN_ROOT_BOOT_DIV,
			      "%s divider is %u, soc_clock_init() sets %u", root->name,
			      CLOCK_GetRootClockDiv(root->root), DOMAIN_ROOT_BOOT_DIV);
		zassert_true(root_is_running(root), "%s is shut down", root->name);
	}
}

/* The CCM of the domain is only accessible while the domain is held. */
static void assert_resume_restores_roots(size_t index, const struct domain_root *roots,
					 size_t count)
{
	hold_domain(index);
	assert_roots_at_boot_setting(roots, count);
	release_domain(index);
}

ZTEST(power_domains, test_unused_domains_are_off_after_boot)
{
	ARRAY_FOR_EACH(domains, i) {
		const char *name = domains[i].dev->name;

		zassert_ok(boot_states[i].ret, "No PM state for %s", name);
		zassert_equal(boot_states[i].device_state, PM_DEVICE_STATE_SUSPENDED,
			      "%s is %s after boot", name,
			      pm_device_state_str(boot_states[i].device_state));
		zassert_equal(boot_states[i].pdcon_state, kPDCON_EventPowerOff,
			      "PDCON reports %s powered after boot", name);
	}
}

ZTEST(power_domains, test_runtime_get_and_put_switch_the_domain)
{
	ARRAY_FOR_EACH(domains, i) {
		hold_domain(i);
		assert_domain_on(&domains[i]);

		release_domain(i);
		assert_domain_off(&domains[i]);
	}
}

/* The domains are powered on one at a time, each next to the ones already on. */
ZTEST(power_domains, test_resume_switches_only_its_own_domain)
{
	pdcon_event_t before[PDCON_DOMAIN_COUNT];
	pdcon_event_t after[PDCON_DOMAIN_COUNT];

	ARRAY_FOR_EACH(domains, i) {
		pdcon_states_read(before);
		hold_domain(i);
		pdcon_states_read(after);

		assert_only_domain_switched(before, after, &domains[i]);
	}
}

/* The domains are powered off one at a time, each next to the ones still on. */
ZTEST(power_domains, test_suspend_switches_only_its_own_domain)
{
	pdcon_event_t before[PDCON_DOMAIN_COUNT];
	pdcon_event_t after[PDCON_DOMAIN_COUNT];

	ARRAY_FOR_EACH(domains, i) {
		hold_domain(i);
	}

	ARRAY_FOR_EACH(domains, i) {
		pdcon_states_read(before);
		release_domain(i);
		pdcon_states_read(after);

		assert_only_domain_switched(before, after, &domains[i]);
	}
}

ZTEST(power_domains, test_resume_restores_domain_clock_roots)
{
	assert_resume_restores_roots(COMM_DOMAIN, comm_roots, ARRAY_SIZE(comm_roots));
	assert_resume_restores_roots(MEDIA_DOMAIN, media_roots, ARRAY_SIZE(media_roots));
}

static void *power_domains_setup(void)
{
	ARRAY_FOR_EACH(domains, i) {
		boot_states[i].ret =
			pm_device_state_get(domains[i].dev, &boot_states[i].device_state);
		boot_states[i].pdcon_state = pdcon_state(domains[i].id);
	}

	return stay_awake_setup();
}

/* A failed test leaves the domains it still holds powered: release them. */
static void power_domains_after(void *fixture)
{
	ARG_UNUSED(fixture);

	ARRAY_FOR_EACH(domains, i) {
		if (domain_held[i]) {
			release_domain(i);
		}
	}
}

ZTEST_SUITE(power_domains, NULL, power_domains_setup, NULL, power_domains_after,
	    stay_awake_teardown);
