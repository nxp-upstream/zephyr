/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <psa/crypto.h>

#include <Hse_Ip.h>

/* These header files come from PSA_wHSE package */
#include <global_variables.h>
#include <keystore_mgmt.h>
#include <psa_demo.h>

static Hse_Ip_MuStateType hse_mu_state;

psa_status_t psa_crypto_hse_init(void)
{
	hseStatus_t hse_status;
	Hse_Ip_StatusType hse_ip_status;
	KeymgmtErrCodeT err;

	keystore_config_t keystore_cfg = {(hseMuMask_t)(1U << MU0), gRamCatalog, gNvmCatalog};

	/* Wait for HSE to initialize (along with RNG module) by reading the status bits in FSR */
	do {
		hse_status = Hse_Ip_GetHseStatus(MU0);
	} while ((hse_status & (HSE_STATUS_INIT_OK | HSE_STATUS_RNG_INIT_OK)) !=
		 (HSE_STATUS_INIT_OK | HSE_STATUS_RNG_INIT_OK));

	/* HSE and RNG are up - booted correctly and initialized successfully */
	hse_ip_status = Hse_Ip_Init(MU0, &hse_mu_state);
	if (HSE_IP_STATUS_SUCCESS == hse_ip_status) {
		printf("HSE Init success\r\n");
	}

	/*
	 * Key configuration must always be provided to the underlying driver.
	 * The underlying driver will format the key catalogs only if it has
	 * not been done (i.e HSE_STATUS_INSTALL_OK is not set).
	 * The Key configuration will also be stored as internal data to be
	 * used by the driver.
	 */
	err = KeystoreMgmt_Init(&keystore_cfg);
	if (err != KEYMGMT_ERR_SUCCESS) {
		while(1);
	}

	return PSA_SUCCESS;
}

int main(void)
{
	psa_demo_non_os();
}
