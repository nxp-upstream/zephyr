/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT	nxp_imx_lpspi_nor

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include "memc_mcux_lpspi.h"

LOG_MODULE_REGISTER(memc_lpspi, CONFIG_MEMC_LOG_LEVEL);

struct memc_lpspi_data {
	LPSPI_Type *base;
	const struct gpio_dt_spec cs_gpios;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	const struct pinctrl_dev_config *pincfg;
};

int memc_lpspi_transfer(const struct device *dev, spi_mem_xfer_t *xfer)
{
	struct memc_lpspi_data *data = dev->data;
	LPSPI_Type *base = data->base;
	status_t status = kStatus_Success;

    do
    {
        if (xfer == NULL)
        {
            status = kStatus_InvalidArgument;
            break;
        }
        /* Set CS to active */
		gpio_pin_set_dt(&data->cs_gpios, 1);

        switch (xfer->mode)
        {
            case kSpiMem_Xfer_CommandOnly:
            {
                lpspi_transfer_t txXfer;
                txXfer.txData      = xfer->cmd;
                txXfer.dataSize    = xfer->cmdSize;
                txXfer.rxData      = NULL;
                txXfer.configFlags = (uint32_t)kLPSPI_MasterPcs0 | (uint32_t)kLPSPI_MasterPcsContinuous;
                status             = LPSPI_MasterTransferBlocking(base, &txXfer);
            }
            break;
            case kSpiMem_Xfer_CommandWriteData:
            {
                lpspi_transfer_t cmdXfer;
                cmdXfer.txData      = xfer->cmd;
                cmdXfer.dataSize    = xfer->cmdSize;
                cmdXfer.rxData      = NULL;
                cmdXfer.configFlags = (uint32_t)kLPSPI_MasterPcs0 | (uint32_t)kLPSPI_MasterPcsContinuous;
                lpspi_transfer_t dataXfer;
                dataXfer.txData      = xfer->data;
                dataXfer.dataSize    = xfer->dataSize;
                dataXfer.rxData      = NULL;
                dataXfer.configFlags = (uint32_t)kLPSPI_MasterPcs0 | (uint32_t)kLPSPI_MasterPcsContinuous;
                status               = LPSPI_MasterTransferBlocking(base, &cmdXfer);
                if (status != kStatus_Success)
                {
                    break;
                }
                status = LPSPI_MasterTransferBlocking(base, &dataXfer);
            }
            break;
            case kSpiMem_Xfer_CommandReadData:
            {
                lpspi_transfer_t cmdXfer;
                cmdXfer.txData      = xfer->cmd;
                cmdXfer.dataSize    = xfer->cmdSize;
                cmdXfer.rxData      = NULL;
                cmdXfer.configFlags = (uint32_t)kLPSPI_MasterPcs0 | (uint32_t)kLPSPI_MasterPcsContinuous;
                lpspi_transfer_t dataXfer;
                dataXfer.txData      = NULL;
                dataXfer.dataSize    = xfer->dataSize;
                dataXfer.rxData      = xfer->data;
                dataXfer.configFlags = (uint32_t)kLPSPI_MasterPcs0 | (uint32_t)kLPSPI_MasterPcsContinuous;
                status               = LPSPI_MasterTransferBlocking(base, &cmdXfer);
                if (status != kStatus_Success)
                {
                    break;
                }
                status = LPSPI_MasterTransferBlocking(base, &dataXfer);
            }
            break;
            default:
                break;
		}

		gpio_pin_set_dt(&data->cs_gpios, 0);

    } while (false);

	if (status != kStatus_Success) {
		LOG_ERR("Transfer error: %d", status);
		return -EIO;
	}

	return 0;
}

int memc_lpspi_config(const struct device *dev, uint32_t baudrate)
{
	struct memc_lpspi_data *data = dev->data;
	LPSPI_Type *lpspiInstance = data->base;
	uint32_t clock_freq = 0;
	int ret;

	if (!device_is_ready(data->clock_dev)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	if (clock_control_get_rate(data->clock_dev, data->clock_subsys,
				   &clock_freq)) {
		return -EINVAL;
	}

    lpspi_master_config_t lpspiMasterCfg;
    LPSPI_MasterGetDefaultConfig(&lpspiMasterCfg);

    lpspiMasterCfg.baudRate                      = baudrate;
    lpspiMasterCfg.pcsToSckDelayInNanoSec        = 1000000000U / lpspiMasterCfg.baudRate;
    lpspiMasterCfg.lastSckToPcsDelayInNanoSec    = 1000000000U / lpspiMasterCfg.baudRate;
    lpspiMasterCfg.betweenTransferDelayInNanoSec = 1000000000U / lpspiMasterCfg.baudRate;

    LPSPI_MasterInit(lpspiInstance, &lpspiMasterCfg, clock_freq);
    ret = 0;

	return ret;
}

static int lpspi_mcux_init(const struct device *dev)
{
	int err, ret;
	struct memc_lpspi_data *data = dev->data;
    const struct gpio_dt_spec *cs_gpio = &data->cs_gpios;

	if (!device_is_ready(cs_gpio->port)) {
		LOG_ERR("CS GPIO port %s pin %d is not ready",
			cs_gpio->port->name, cs_gpio->pin);
		return -ENODEV;
	}

	err = pinctrl_apply_state(data->pincfg, PINCTRL_STATE_DEFAULT);
	if (err) {
		return err;
	}

    ret = gpio_pin_configure_dt(&data->cs_gpios, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

#define MEMC_LPSPI(n)							\
    PINCTRL_DT_INST_DEFINE(n);                 \
	static struct memc_lpspi_data					\
		memc_lpspi_data_##n = {				\
		.base = (LPSPI_Type *) DT_INST_REG_ADDR(n),		\
	    .clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),	\
		.clock_subsys =	(clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, name),	\
        .pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),		\
        .cs_gpios = GPIO_DT_SPEC_GET(DT_DRV_INST(n), cs_gpios),   \
	};						\
									\
	DEVICE_DT_INST_DEFINE(n,					\
			      lpspi_mcux_init,			\
			      NULL,			\
			      &memc_lpspi_data_##n,			\
			      NULL,					\
			      POST_KERNEL,				\
			      CONFIG_MEMC_MCUX_LPSPI_INIT_PRIORITY,	\
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(MEMC_LPSPI)
