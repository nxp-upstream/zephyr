/*
 * Copyright (c) 2025 NXP
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/otp.h>
#include <zephyr/logging/log.h>
#include <soc.h>

#define DT_DRV_COMPAT nxp_rt7xx_ocotp

LOG_MODULE_REGISTER(nxp_ocotp, CONFIG_OTP_LOG_LEVEL);

#define OCOTP_WR_UNLOCK_KEY  0x3E77U
#define OCOTP_BUSY_TIMEOUT_US 100000U

struct nxp_ocotp_config {
	OCOTP_Type *base;
	uint32_t size;
};

static inline void nxp_ocotp_lock(const struct device *dev)
{
	struct k_sem *lock = dev->data;

	if (!k_is_pre_kernel()) {
		(void)k_sem_take(lock, K_FOREVER);
	}
}

static inline void nxp_ocotp_unlock(const struct device *dev)
{
	struct k_sem *lock = dev->data;

	if (!k_is_pre_kernel()) {
		k_sem_give(lock);
	}
}

static int nxp_ocotp_wait_busy(OCOTP_Type *base)
{
	uint32_t timeout = OCOTP_BUSY_TIMEOUT_US;

	while ((base->HW_OCOTP_STATUS & OCOTP_HW_OCOTP_STATUS_BUSY_MASK) && timeout > 0) {
		k_busy_wait(1);
		timeout--;
	}

	if (timeout == 0) {
		LOG_ERR("OCOTP timeout waiting for busy clear");
		return -ETIMEDOUT;
	}

	return 0;
}

static int nxp_ocotp_read(const struct device *dev, off_t offset, void *buf, size_t len)
{
	const struct nxp_ocotp_config *config = dev->config;
	OCOTP_Type *base = config->base;
	uint8_t *buf_u8 = buf;
	uint32_t addr;
	size_t part, skip;
	uint32_t raw;

	if (offset < 0 || buf == NULL) {
		return -EINVAL;
	}

	if ((offset + len) > config->size) {
		LOG_ERR("Read out of range (offset=0x%lx len=0x%zx size=0x%x)",
			offset, len, config->size);
		return -EINVAL;
	}

	/* Compute the word index and byte offset within the first word */
	addr = offset / sizeof(uint32_t);
	skip = offset % sizeof(uint32_t);

	nxp_ocotp_lock(dev);

	while (len > 0) {
		if (addr >= OCOTP_OTP_SHADOW_COUNT) {
			nxp_ocotp_unlock(dev);
			return -EINVAL;
		}

		/* Read directly from the shadow register */
		raw = base->OTP_SHADOW[addr];

		part = MIN(len + skip, sizeof(uint32_t));
		part -= skip;

		memcpy(buf_u8, (uint8_t *)&raw + skip, part);
		buf_u8 += part;

		addr++;
		len -= part;

		/* skip is only relevant for the first word */
		skip = 0;
	}

	nxp_ocotp_unlock(dev);

	return 0;
}

#if defined(CONFIG_OTP_PROGRAM)
static int nxp_ocotp_program(const struct device *dev, off_t offset, const void *buf, size_t len)
{
	const struct nxp_ocotp_config *config = dev->config;
	OCOTP_Type *base = config->base;
	const uint32_t *buf_u32 = buf;
	uint32_t addr, data;
	int ret;

	if (offset < 0 || buf == NULL) {
		return -EINVAL;
	}

	if (!IS_ALIGNED(offset, sizeof(uint32_t)) || !IS_ALIGNED(len, sizeof(uint32_t))) {
		LOG_ERR("Unaligned program not allowed (0x%lx/0x%zx)", offset, len);
		return -EINVAL;
	}

	if ((offset + len) > config->size) {
		LOG_ERR("Program out of range (offset=0x%lx len=0x%zx size=0x%x)",
			offset, len, config->size);
		return -EINVAL;
	}

	addr = offset / sizeof(uint32_t);
	len /= sizeof(uint32_t);

	nxp_ocotp_lock(dev);

	while (len > 0) {
		ret = nxp_ocotp_wait_busy(base);
		if (ret) {
			nxp_ocotp_unlock(dev);
			return ret;
		}

		memcpy(&data, buf_u32, sizeof(uint32_t));

		/* Write the fuse address and unlock key to CTRL */
		base->CTRL = OCOTP_CTRL_ADDR(addr) |
			     OCOTP_CTRL_WR_UNLOCK(OCOTP_WR_UNLOCK_KEY);

		/* Write data to trigger the program operation */
		base->HW_OCOTP_WRITE_DATA = data;

		ret = nxp_ocotp_wait_busy(base);
		if (ret) {
			nxp_ocotp_unlock(dev);
			return ret;
		}

		if (base->HW_OCOTP_STATUS & OCOTP_HW_OCOTP_STATUS_PROGFAIL_MASK) {
			LOG_ERR("OCOTP program failed at addr %u", addr);
			nxp_ocotp_unlock(dev);
			return -EIO;
		}

		addr++;
		buf_u32++;
		len--;
	}

	nxp_ocotp_unlock(dev);

	return 0;
}
#endif /* CONFIG_OTP_PROGRAM */

static DEVICE_API(otp, nxp_ocotp_api) = {
	.read = nxp_ocotp_read,
#if defined(CONFIG_OTP_PROGRAM)
	.program = nxp_ocotp_program,
#endif
};

static int nxp_ocotp_init(const struct device *dev)
{
	const struct nxp_ocotp_config *config = dev->config;
	OCOTP_Type *base = config->base;

	/* Verify that fuses have been loaded into shadow registers */
	if (!(base->HW_OCOTP_STATUS & OCOTP_HW_OCOTP_STATUS_FUSE_LATCHED_MASK)) {
		LOG_ERR("OTP fuses not latched into shadow registers");
		return -EIO;
	}

	return 0;
}

#define NXP_OCOTP_INIT(inst)								\
	static K_SEM_DEFINE(nxp_ocotp_##inst##_lock, 1, 1);				\
											\
	static const struct nxp_ocotp_config nxp_ocotp_##inst##_config = {		\
		.base = (OCOTP_Type *)DT_INST_REG_ADDR(inst),				\
		.size = DT_INST_REG_SIZE(inst),						\
	};										\
											\
	DEVICE_DT_INST_DEFINE(inst, nxp_ocotp_init, NULL, &nxp_ocotp_##inst##_lock,	\
			      &nxp_ocotp_##inst##_config, PRE_KERNEL_1,			\
			      CONFIG_OTP_INIT_PRIORITY, &nxp_ocotp_api);

DT_INST_FOREACH_STATUS_OKAY(NXP_OCOTP_INIT)
