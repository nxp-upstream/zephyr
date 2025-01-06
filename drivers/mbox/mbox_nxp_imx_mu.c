/*
 * Copyright 2024-2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wrapper of the i.MX Message Unit driver into Zephyr's MBOX model.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util_macro.h>
#include <fsl_mu.h>

#define LOG_LEVEL CONFIG_MBOX_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nxp_mbox_imx_mu);

#define DT_DRV_COMPAT nxp_mbox_imx_mu

#define MU_MAX_CHANNELS 4
#define MU_MBOX_SIZE    sizeof(uint32_t)

struct nxp_imx_mu_data {
	mbox_callback_t cb[MU_MAX_CHANNELS];
	void *user_data[MU_MAX_CHANNELS];
	uint32_t received_data;
};

struct nxp_imx_mu_config {
	MU_Type *base;
};

/*
 * Functions to translate the channel number to Generic Interrupt Mask for mu driver.
 * The kMU_GenInt[0..3]InterruptTrigger comes from MU driver header mu.h.
 * There are more MU drivers implementations in one `mu` driver the
 * kMU_GenInt0InterruptTrigger goes from 3 to 0 relative bit for the
 * `mu1` driver kMU_GenInt0InterruptTrigger goes from 0 to 3.
 * Same for kMU_Rx0FullFlag.
 * Therefore use this mapping table to select correct mask based on channel index.
 *
 */
static uint32_t get_gen_int_mask(const uint32_t channel)
{
	uint32_t mask = 0;

	switch (channel) {
	case 0:
		mask = kMU_GenInt0InterruptTrigger;
		break;

	case 1:
		mask = kMU_GenInt1InterruptTrigger;
		break;

	case 2:
		mask = kMU_GenInt2InterruptTrigger;
		break;

	case 3:
		mask = kMU_GenInt3InterruptTrigger;
		break;

	default:
		/* Invalid channel provided */
		assert(0);
		break;
	}

	return mask;
}

static uint32_t get_rx_int_mask(const uint32_t channel)
{
	uint32_t mask = 0;

	switch (channel) {
	case 0:
		mask = kMU_Rx0FullFlag;
		break;

	case 1:
		mask = kMU_Rx1FullFlag;
		break;

	case 2:
		mask = kMU_Rx1FullFlag;
		break;

	case 3:
		mask = kMU_Rx1FullFlag;
		break;

	default:
		/* Invalid channel provided */
		assert(0);
		break;
	}

	return mask;
}

static int nxp_imx_mu_send(const struct device *dev, uint32_t channel, const struct mbox_msg *msg)
{
	uint32_t __aligned(4) data32;
	const struct nxp_imx_mu_config *cfg = dev->config;

	if (channel >= MU_MAX_CHANNELS) {
		return -EINVAL;
	}

	/* Signalling mode. */
	if (msg == NULL) {
		return MU_TriggerInterrupts(cfg->base, get_gen_int_mask(channel));
	}

	/* Data transfer mode. */
	if (msg->size != MU_MBOX_SIZE) {
		/* We can only send this many bytes at a time. */
		return -EMSGSIZE;
	}

	/* memcpy to avoid issues when msg->data is not word-aligned. */
	memcpy(&data32, msg->data, msg->size);
	MU_SendMsg(cfg->base, channel, data32);
	return 0;
}

static int nxp_imx_mu_register_callback(const struct device *dev, uint32_t channel,
					mbox_callback_t cb, void *user_data)
{
	struct nxp_imx_mu_data *data = dev->data;

	if (channel >= MU_MAX_CHANNELS) {
		return -EINVAL;
	}

	data->cb[channel] = cb;
	data->user_data[channel] = user_data;

	return 0;
}

static int nxp_imx_mu_mtu_get(const struct device *dev)
{
	ARG_UNUSED(dev);
	return MU_MBOX_SIZE;
}

static uint32_t nxp_imx_mu_max_channels_get(const struct device *dev)
{
	ARG_UNUSED(dev);
	return MU_MAX_CHANNELS;
}

static int nxp_imx_mu_set_enabled(const struct device *dev, uint32_t channel, bool enable)
{
	struct nxp_imx_mu_data *data = dev->data;
	const struct nxp_imx_mu_config *cfg = dev->config;

	if (channel >= MU_MAX_CHANNELS) {
		return -EINVAL;
	}

	if (enable) {
		if (data->cb[channel] == NULL) {
			LOG_WRN("Enabling channel without a registered callback");
		}
		MU_EnableInterrupts(
			cfg->base, kMU_GenInt0InterruptEnable | kMU_GenInt1InterruptEnable |
					   kMU_GenInt2InterruptEnable | kMU_GenInt3InterruptEnable |
					   kMU_Rx0FullInterruptEnable | kMU_Rx1FullInterruptEnable |
					   kMU_Rx2FullInterruptEnable | kMU_Rx3FullInterruptEnable);
	} else {
		MU_DisableInterrupts(
			cfg->base, kMU_GenInt0InterruptEnable | kMU_GenInt1InterruptEnable |
					   kMU_GenInt2InterruptEnable | kMU_GenInt3InterruptEnable |
					   kMU_Rx0FullInterruptEnable | kMU_Rx1FullInterruptEnable |
					   kMU_Rx2FullInterruptEnable | kMU_Rx3FullInterruptEnable);
	}

	return 0;
}

static DEVICE_API(mbox, nxp_imx_mu_driver_api) = {
	.send = nxp_imx_mu_send,
	.register_callback = nxp_imx_mu_register_callback,
	.mtu_get = nxp_imx_mu_mtu_get,
	.max_channels_get = nxp_imx_mu_max_channels_get,
	.set_enabled = nxp_imx_mu_set_enabled,
};

static void handle_irq(const struct device *dev);

#define MU_INSTANCE_DEFINE(idx)                                                                    \
	static struct nxp_imx_mu_data nxp_imx_mu_##idx##_data;                                     \
	const static struct nxp_imx_mu_config nxp_imx_mu_##idx##_config = {                        \
		.base = (MU_Type *)DT_INST_REG_ADDR(idx),                                          \
	};                                                                                         \
	void MU_##idx##_IRQHandler(void);                                                          \
	static int nxp_imx_mu_##idx##_init(const struct device *dev)                               \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		MU_Init(nxp_imx_mu_##idx##_config.base);                                           \
		IRQ_CONNECT(DT_INST_IRQN(idx), DT_INST_IRQ(idx, priority), MU_##idx##_IRQHandler,  \
			    NULL, 0);                                                              \
		irq_enable(DT_INST_IRQN(idx));                                                     \
		return 0;                                                                          \
	}                                                                                          \
	DEVICE_DT_INST_DEFINE(idx, nxp_imx_mu_##idx##_init, NULL, &nxp_imx_mu_##idx##_data,        \
			      &nxp_imx_mu_##idx##_config, PRE_KERNEL_1, CONFIG_MBOX_INIT_PRIORITY, \
			      &nxp_imx_mu_driver_api)

#define MU_IRQ_HANDLER(idx)                                                                        \
	void MU_##idx##_IRQHandler(void)                                                           \
	{                                                                                          \
		const struct device *dev = DEVICE_DT_INST_GET(idx);                                \
		handle_irq(dev);                                                                   \
	}

#define MU_INST(idx)                                                                               \
	MU_INSTANCE_DEFINE(idx);                                                                   \
	MU_IRQ_HANDLER(idx);

DT_INST_FOREACH_STATUS_OKAY(MU_INST)

static void handle_irq(const struct device *dev)
{
	struct nxp_imx_mu_data *data = dev->data;
	const struct nxp_imx_mu_config *config = dev->config;
	const uint32_t flags = MU_GetStatusFlags(config->base);

	for (int i_channel = 0; i_channel < MU_MAX_CHANNELS; i_channel++) {
		uint32_t rx_int_mask = get_rx_int_mask(i_channel);
		uint32_t gen_int_mask = get_gen_int_mask(i_channel);

		if ((flags & rx_int_mask) == rx_int_mask) {
			data->received_data = MU_ReceiveMsgNonBlocking(config->base, i_channel);
			struct mbox_msg msg = {(const void *)&data->received_data, MU_MBOX_SIZE};

			if (data->cb[i_channel]) {
				data->cb[i_channel](dev, i_channel, data->user_data[i_channel],
						    &msg);
			}
		} else if ((flags & gen_int_mask) == gen_int_mask) {
			MU_ClearStatusFlags(config->base, gen_int_mask);
			if (data->cb[i_channel]) {
				data->cb[i_channel](dev, i_channel, data->user_data[i_channel],
						    NULL);
			}
		}
	}
}
