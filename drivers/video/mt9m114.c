/*
 * Copyright (c) 2019, Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT aptina_mt9m114
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/video.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mt9m114, CONFIG_VIDEO_LOG_LEVEL);

#define MT9M114_CHIP_ID_VAL				0x2481

/* Sysctl registers */
#define MT9M114_CHIP_ID					0x0000
#define MT9M114_COMMAND_REGISTER			0x0080
#define MT9M114_COMMAND_REGISTER_APPLY_PATCH		(1 << 0)
#define MT9M114_COMMAND_REGISTER_SET_STATE		(1 << 1)
#define MT9M114_COMMAND_REGISTER_REFRESH		(1 << 2)
#define MT9M114_COMMAND_REGISTER_WAIT_FOR_EVENT		(1 << 3)
#define MT9M114_COMMAND_REGISTER_OK			(1 << 15)
#define MT9M114_PAD_CONTROL				0x0032
#define MT9M114_RST_AND_MISC_CONTROL			0x001A

/* Camera Control registers */
#define MT9M114_CAM_OUTPUT_FORMAT			0xc86c

/* System Manager registers */
#define MT9M114_SYSMGR_NEXT_STATE			0xdc00
#define MT9M114_SYSMGR_CURRENT_STATE			0xdc01
#define MT9M114_SYSMGR_CMD_STATUS			0xdc02

/* System States */
#define MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE		0x28
#define MT9M114_SYS_STATE_STREAMING			0x31
#define MT9M114_SYS_STATE_START_STREAMING		0x34
#define MT9M114_SYS_STATE_ENTER_SUSPEND			0x40
#define MT9M114_SYS_STATE_SUSPENDED			0x41
#define MT9M114_SYS_STATE_ENTER_STANDBY			0x50
#define MT9M114_SYS_STATE_STANDBY			0x52
#define MT9M114_SYS_STATE_LEAVE_STANDBY			0x54

struct mt9m114_format {
	uint32_t pixelformat;
	uint16_t setting;
};

enum {
	MT9M114_VGA,
	MT9M114_720P,
};

struct mt9m114_resolution {
	uint32_t width;
	uint32_t height;
};

struct mt9m114_reg {
	uint16_t addr;
	uint16_t value_size;
	uint32_t value;
};

struct mt9m114_config {
	struct i2c_dt_spec i2c;
	const struct pinctrl_dev_config *pincfg;
	const struct device **power_regulator_list;
	uint32_t power_regulator_count;
	const struct gpio_dt_spec powerdown_gpio;
	const struct gpio_dt_spec reset_gpio;
};

struct mt9m114_data {
	struct video_format fmt;
	bool to_init;
	const struct mt9m114_resolution *curr_mode;
	const struct mt9m114_resolution *last_mode;
};


static const struct video_format_cap fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_UYVY,
		.width_min = 640,
		.width_max = 640,
		.height_min = 480,
		.height_max = 480,
		.width_step = 0,
		.height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_UYVY,
		.width_min = 1280,
		.width_max = 720,
		.height_min = 1280,
		.height_max = 720,
		.width_step = 0,
		.height_step = 0,
	},
	{ 0 }
};

static const struct mt9m114_resolution mt9m114_resolutions[] = {
	[MT9M114_VGA] = {
		.width  = 640,
		.height = 480,
	},
	[MT9M114_720P] = {
		.width  = 1280,
		.height = 720,
	},
	{0},
};

static struct mt9m114_reg mt9m114_vga_24mhz_pll[] = {
	{ 0x98E,  2, 0x1000	},
	{ 0xC97E, 2, 0x01	}, /* cam_sysctl_pll_enable = 1 */
	{ 0xC980, 2, 0x0120	}, /* cam_sysctl_pll_divider_m_n = 288 */
	{ 0xC982, 2, 0x0700	}, /* cam_sysctl_pll_divider_p = 1792 */
	{ 0xC984, 2, 0x8000	}, /* cam_port_output_control = 32776 */
	{ 0xC800, 2, 0x0000	}, /* cam_sensor_cfg_y_addr_start = 0 */
	{ 0xC802, 2, 0x0000	}, /* cam_sensor_cfg_x_addr_start = 0 */
	{ 0xC804, 2, 0x03CD	}, /* cam_sensor_cfg_y_addr_end = 973 */
	{ 0xC806, 2, 0x050D	}, /* cam_sensor_cfg_x_addr_end = 1293 */
	{ 0xC808, 4, 0x2DC6C00	}, /* cam_sensor_cfg_pixclk = 48000000 */
	{ 0xC80C, 2, 0x0001	}, /* cam_sensor_cfg_row_speed = 1 */
	{ 0xC80E, 2, 0x00DB	}, /* cam_sensor_cfg_fine_integ_min = 219 */
	{ 0xC810, 2, 0x07C2	}, /* cam_sensor_cfg_fine_integ_max = 1986 */
	{ 0xC812, 2, 0x02FE	}, /* cam_sensor_cfg_frame_length_lines = 766 */
	{ 0xC814, 2, 0x0845	}, /* cam_sensor_cfg_line_length_pck = 2117 */
	{ 0xC816, 2, 0x0060	}, /* cam_sensor_cfg_fine_correction = 96 */
	{ 0xC818, 2, 0x01E3	}, /* cam_sensor_cfg_cpipe_last_row = 483 */
	{ 0xC826, 2, 0x0020	}, /* cam_sensor_cfg_reg_0_data = 32 */
	{ 0xC834, 2, 0x0110	}, /* cam_sensor_control_read_mode = 272 */
	{ 0xC854, 2, 0x0000	}, /* cam_crop_window_xoffset = 0 */
	{ 0xC856, 2, 0x0000	}, /* cam_crop_window_yoffset = 0 */
	{ 0xC858, 2, 0x0280	}, /* cam_crop_window_width = 640 */
	{ 0xC85A, 2, 0x01E0	}, /* cam_crop_window_height = 480 */
	{ 0xC85C, 1, 0x03	}, /* cam_crop_cropmode = 3 */
	{ 0xC868, 2, 0x0280	}, /* cam_output_width = 640 */
	{ 0xC86A, 2, 0x01E0	}, /* cam_output_height = 480 */
	{ 0xC878, 1, 0x00	}, /* cam_aet_aemode = 0 */
	{ 0xC88C, 2, 0x1D9A	}, /* cam_aet_max_frame_rate = 7578 */
	{ 0xC914, 2, 0x0000	}, /* cam_stat_awb_clip_window_xstart = 0 */
	{ 0xC88E, 2, 0x1D9A	}, /* cam_aet_min_frame_rate = 7578 */
	{ 0xC916, 2, 0x0000	}, /* cam_stat_awb_clip_window_ystart = 0 */
	{ 0xC918, 2, 0x027F	}, /* cam_stat_awb_clip_window_xend = 639 */
	{ 0xC91A, 2, 0x01DF	}, /* cam_stat_awb_clip_window_yend = 479 */
	{ 0xC91C, 2, 0x0000	}, /* cam_stat_ae_initial_window_xstart = 0 */
	{ 0xC91E, 2, 0x0000	}, /* cam_stat_ae_initial_window_ystart = 0 */
	{ 0xC920, 2, 0x007F	}, /* cam_stat_ae_initial_window_xend = 127 */
	{ 0xC922, 2, 0x005F	}, /* cam_stat_ae_initial_window_yend = 95 */
};

static struct mt9m114_reg mt9m114_regs_init[] = {
	/* PLL settings */
	{ 0x098E, 2, 0x1000 },
	{ 0xC97E, 1, 0x01 },
	{ 0xC980, 2, 0x0120 },
	{ 0xC982, 2, 0x0700 },
	{ 0xC808, 4, 0x02DC6C00 },

	/* Sensor optimization */
	{ 0x316A, 2, 0x8270 },
	{ 0x316C, 2, 0x8270 },
	{ 0x3ED0, 2, 0x2305 },
	{ 0x3ED2, 2, 0x77CF },
	{ 0x316E, 2, 0x8202 },
	{ 0x3180, 2, 0x87FF },
	{ 0x30D4, 2, 0x6080 },
	{ 0xA802, 2, 0x0008 },
	{ 0x3E14, 2, 0xFF39 },

	{ 0xC80C, 2, 0x0001 },
	{ 0xC80E, 2, 0x00DB },
	{ 0xC810, 2, 0x07C2 },
	{ 0xC812, 2, 0x02FE },
	{ 0xC814, 2, 0x0845 },
	{ 0xC816, 2, 0x0060 },
	{ 0xC826, 2, 0x0020 },
	{ 0xC834, 2, 0x0000 },
	{ 0xC854, 2, 0x0000 },
	{ 0xC856, 2, 0x0000 },
	{ 0xC85C, 1, 0x03 },
	{ 0xC878, 1, 0x00 },
	{ 0xC88C, 2, 0x1D9A },
	{ 0xC88E, 2, 0x1D9A },
	{ 0xC914, 2, 0x0000 },
	{ 0xC916, 2, 0x0000 },
	{ 0xC91C, 2, 0x0000 },
	{ 0xC91E, 2, 0x0000 },
	{ 0x001E, 2, 0x0777 },
	{ 0xC86E, 2, 0x0038 },	//MT9M114_CAM_OUTPUT_FORMAT_YUV
};

static struct mt9m114_reg mt9m114_regs_vga[] = {
	{ 0x098E, 2, 0x1000	},
	{ 0xC800, 2, 0x0000 },
	{ 0xC802, 2, 0x0000 },
	{ 0xC804, 2, 0x03CD },
	{ 0xC806, 2, 0x050D },
	{ 0xC80C, 2, 0x0001 },
	{ 0xC80E, 2, 0x01C3 },
	{ 0xC810, 2, 0x03F7 },
	{ 0xC812, 2, 0x0500 },
	{ 0xC814, 2, 0x04E2 },
	{ 0xC816, 2, 0x00E0 },
	{ 0xC818, 2, 0x01E3 },
	{ 0xC826, 2, 0x0020 },
	{ 0xC854, 2, 0x0000 },
	{ 0xC856, 2, 0x0000 },
	{ 0xC858, 2, 0x0280 },
	{ 0xC85A, 2, 0x01E0 },
	{ 0xC85C, 1, 0x03 },
	{ 0xC868, 2, 0x0280 },
	{ 0xC86A, 2, 0x01E0 },
	{ 0xC878, 1, 0x00 },
	{ 0xC914, 2, 0x0000 },
	{ 0xC916, 2, 0x0000 },
	{ 0xC918, 2, 0x027F },
	{ 0xC91A, 2, 0x01DF },
	{ 0xC91C, 2, 0x0000 },
	{ 0xC91E, 2, 0x0000 },
	{ 0xC920, 2, 0x007F },
	{ 0xC922, 2, 0x005F },
};

static struct mt9m114_reg mt9m114_regs_720p[] = {
	{ 0xC800, 2, 0x0004 },
	{ 0xC802, 2, 0x0004 },
	{ 0xC804, 2, 0x03CB },
	{ 0xC806, 2, 0x050B },
	{ 0xC80C, 2, 0x0001 },
	{ 0xC80E, 2, 0x00DB },
	{ 0xC810, 2, 0x05B3 },
	{ 0xC812, 2, 0x03EE },
	{ 0xC814, 2, 0x0636 },
	{ 0xC816, 2, 0x0060 },
	{ 0xC818, 2, 0x03C3 },
	{ 0xC826, 2, 0x0020 },
	{ 0xC854, 2, 0x0000 },
	{ 0xC856, 2, 0x0000 },
	{ 0xC858, 2, 0x0500 },
	{ 0xC85A, 2, 0x03C0 },
	{ 0xC85C, 1, 0x03 },
	{ 0xC868, 2, 0x0500 },
	{ 0xC86A, 2, 0x02D0 },
	{ 0xC878, 1, 0x00 },
	{ 0xC914, 2, 0x0000 },
	{ 0xC916, 2, 0x0000 },
	{ 0xC918, 2, 0x04FF },
	{ 0xC91A, 2, 0x02CF },
	{ 0xC91C, 2, 0x0000 },
	{ 0xC91E, 2, 0x0000 },
	{ 0xC920, 2, 0x00FF },
	{ 0xC922, 2, 0x008F },
};

static inline int i2c_burst_read16_dt(const struct i2c_dt_spec *spec,
				   uint16_t start_addr, uint8_t *buf, uint32_t num_bytes)
{
	uint8_t addr_buffer[2];

	addr_buffer[1] = start_addr & 0xFF;
	addr_buffer[0] = start_addr >> 8;
	return i2c_write_read_dt(spec, addr_buffer, sizeof(addr_buffer), buf, num_bytes);
}

static inline int i2c_burst_write16_dt(const struct i2c_dt_spec *spec,
				    uint16_t start_addr, const uint8_t *buf,
				    uint32_t num_bytes)
{
	uint8_t addr_buffer[2];
	struct i2c_msg msg[2];

	addr_buffer[1] = start_addr & 0xFF;
	addr_buffer[0] = start_addr >> 8;
	msg[0].buf = addr_buffer;
	msg[0].len = 2U;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = (uint8_t *)buf;
	msg[1].len = num_bytes;
	msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	return i2c_transfer_dt(spec, msg, 2);
}

static int mt9m114_write_reg(const struct device *dev, uint16_t reg_addr,
			     uint8_t reg_size,
			     void *value)
{
	const struct mt9m114_config *cfg = dev->config;

	switch (reg_size) {
	case 2:
		*(uint16_t *)value = sys_cpu_to_be16(*(uint16_t *)value);
		break;
	case 4:
		*(uint32_t *)value = sys_cpu_to_be32(*(uint32_t *)value);
		break;
	case 1:
		break;
	default:
		return -ENOTSUP;
	}

	return i2c_burst_write16_dt(&cfg->i2c, reg_addr, value, reg_size);
}

static int mt9m114_read_reg(const struct device *dev, uint16_t reg_addr,
			    uint8_t reg_size,
			    void *value)
{
	const struct mt9m114_config *cfg = dev->config;
	int err;

	if (reg_size > 4) {
		return -ENOTSUP;
	}

	err = i2c_burst_read16_dt(&cfg->i2c, reg_addr, value, reg_size);
	if (err) {
		return err;
	}

	switch (reg_size) {
	case 2:
		*(uint16_t *)value = sys_be16_to_cpu(*(uint16_t *)value);
		break;
	case 4:
		*(uint32_t *)value = sys_be32_to_cpu(*(uint32_t *)value);
		break;
	case 1:
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int mt9m114_write_all(const struct device *dev,
			     struct mt9m114_reg *reg, int len)
{
	int i = 0;

	for (i = 0; i < len; i++) {
		int err;

		err = mt9m114_write_reg(dev, reg[i].addr, reg[i].value_size,
					&reg[i].value);
		if (err) {
			return err;
		}

	}

	return 0;
}

static int mt9m114_set_state(const struct device *dev, uint8_t state)
{
	uint16_t val;
	int err;

	/* Set next state. */
	mt9m114_write_reg(dev, MT9M114_SYSMGR_NEXT_STATE, 1, &state);

	/* Check that the FW is ready to accept a new command. */
	while (1) {
		err = mt9m114_read_reg(dev, MT9M114_COMMAND_REGISTER, 2, &val);
		if (err) {
			return err;
		}

		if (!(val & MT9M114_COMMAND_REGISTER_SET_STATE)) {
			break;
		}

		k_sleep(K_MSEC(1));
	}

	/* Issue the Set State command. */
	val = MT9M114_COMMAND_REGISTER_SET_STATE | MT9M114_COMMAND_REGISTER_OK;
	mt9m114_write_reg(dev, MT9M114_COMMAND_REGISTER, 2, &val);

	/* Wait for the FW to complete the command. */
	while (1) {
		err = mt9m114_read_reg(dev, MT9M114_COMMAND_REGISTER, 2, &val);
		if (err) {
			return err;
		}

		if (!(val & MT9M114_COMMAND_REGISTER_SET_STATE)) {
			break;
		}

		k_sleep(K_MSEC(1));
	}

	/* Check the 'OK' bit to see if the command was successful. */
	err = mt9m114_read_reg(dev, MT9M114_COMMAND_REGISTER, 2, &val);
	if (err || !(val & MT9M114_COMMAND_REGISTER_OK)) {
		return -EIO;
	}

	return 0;
}

static int mt9m114_set_res(const struct device *dev, uint32_t width, uint32_t height)
{
	uint16_t read_mode;
	int ret = 0;

	if ((width == mt9m114_resolutions[MT9M114_VGA].width) &&
				(height == mt9m114_resolutions[MT9M114_VGA].height))
	{
		ret = mt9m114_write_all(dev, mt9m114_regs_vga, ARRAY_SIZE(mt9m114_regs_vga));
		if (ret)
			return ret;
		ret = mt9m114_read_reg(dev, 0xc834, 2, &read_mode);
		read_mode = (read_mode & 0xfccf) | 0x0330;
		ret = mt9m114_write_reg(dev, 0xc834, 2, &read_mode);
	}
	else if ((width == mt9m114_resolutions[MT9M114_720P].width) &&
				(height == mt9m114_resolutions[MT9M114_720P].height))
	{
		ret = mt9m114_write_all(dev, mt9m114_regs_720p, ARRAY_SIZE(mt9m114_regs_720p));
		if (ret)
			return ret;
		ret = mt9m114_read_reg(dev, 0xc834, 2, &read_mode);
		read_mode &= 0xfccf;
		ret = mt9m114_write_reg(dev, 0xc834, 2, &read_mode);
	}
	else
	{
		LOG_ERR("Resolution (%dx%d) not supported", width, height);
		return -EINVAL;
	}

	return ret;
}

static int mt9m114_find_res(const struct device *dev, uint32_t width, uint32_t height)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt9m114_resolutions); i++) {
		if ((width == mt9m114_resolutions[i].width) &&
					(height == mt9m114_resolutions[i].height))
			return i;
	}

	return -EINVAL;
}

static int mt9m114_soft_reset(const struct device *dev)
{
	int ret;
	uint16_t val;

	/* reset the sensor */
	val = 0x0001;
	ret = mt9m114_write_reg(dev, 0x001a, 2, &val);
	k_sleep(K_MSEC(10));

	val = 0x0000;
	ret = mt9m114_write_reg(dev, 0x001a, 2, &val);
	k_sleep(K_MSEC(45));

	return ret;
}

static int mt9m114_set_fmt(const struct device *dev,
			   enum video_endpoint_id ep,
			   struct video_format *fmt)
{
	struct mt9m114_data *drv_data = dev->data;
	int ret, i;
	uint16_t val;

	/* Only support YUV422 format for now */
	if (fmt->pixelformat != VIDEO_PIX_FMT_UYVY) {
		LOG_ERR("Format (%c%c%c%c) not supported",
					(char)fmt->pixelformat, (char)(fmt->pixelformat >> 8),
					(char)(fmt->pixelformat >> 16), (char)(fmt->pixelformat >> 24));
		return -ENOTSUP;
	}

	i = mt9m114_find_res(dev, fmt->width, fmt->height);
	if (i < 0) {
		LOG_ERR("Resolution (%dx%d) not supported", fmt->width, fmt->height);
		return -ENOTSUP;
	}
	drv_data->curr_mode = &mt9m114_resolutions[i];

	if (drv_data->to_init) {
		ret = mt9m114_write_all(dev, mt9m114_regs_init, ARRAY_SIZE(mt9m114_regs_init));
		if (ret) {
			LOG_ERR("Failed to configure mt9m114_regs_init");
			return ret;
		}
		drv_data->to_init = false;

		/* PIXCLK is only generated for valid output pixels or continuous. */
		val = 0x8020;
		ret = mt9m114_write_reg(dev, 0xC984, 2, &val);

		/* Cofigure YUV422 as default format*/
		val = 0x0012;
		ret = mt9m114_write_reg(dev, MT9M114_CAM_OUTPUT_FORMAT, 2, &val);
	}

	if (drv_data->curr_mode != drv_data->last_mode) {
		drv_data->last_mode = drv_data->curr_mode;

		/* Cofigure sensor to specific resolution*/
		ret = mt9m114_set_res(dev, fmt->width, fmt->height);
		if (ret)
			LOG_ERR("Failed to set res wxh=%dx%d", fmt->width, fmt->height);

		/* Apply Config */
		ret = mt9m114_set_state(dev, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
		if (ret)
			LOG_ERR("Failed to set state config change");

		mt9m114_set_state(dev, MT9M114_SYS_STATE_ENTER_SUSPEND);
	}

	drv_data->fmt = *fmt;

	return 0;
}

static int mt9m114_get_fmt(const struct device *dev,
			   enum video_endpoint_id ep,
			   struct video_format *fmt)
{
	struct mt9m114_data *drv_data = dev->data;

	*fmt = drv_data->fmt;

	return 0;
}

static int mt9m114_stream_start(const struct device *dev)
{
	return mt9m114_set_state(dev, MT9M114_SYS_STATE_START_STREAMING);
}

static int mt9m114_stream_stop(const struct device *dev)
{
	return mt9m114_set_state(dev, MT9M114_SYS_STATE_ENTER_SUSPEND);
}

static int mt9m114_get_caps(const struct device *dev,
			    enum video_endpoint_id ep,
			    struct video_caps *caps)
{
	caps->format_caps = fmts;
	return 0;
}

static const struct video_driver_api mt9m114_driver_api = {
	.set_format = mt9m114_set_fmt,
	.get_format = mt9m114_get_fmt,
	.get_caps = mt9m114_get_caps,
	.stream_start = mt9m114_stream_start,
	.stream_stop = mt9m114_stream_stop,
};

static int mt9m114_configure_gpios(const struct mt9m114_config *cfg)
{
	int ret;

	ret = gpio_pin_configure_dt(&cfg->powerdown_gpio, GPIO_OUTPUT_INACTIVE);
	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);

	return ret;
}

static int mt9m114_power_up(const struct mt9m114_config *cfg)
{
	int ret;

	for(uint32_t i = 0; i < cfg->power_regulator_count; ++i)
	{
		printk("enable %d\n", i);
		ret = regulator_enable(cfg->power_regulator_list[i]);
		if(ret != 0)
		{
			LOG_ERR("regulator \"%s\" enable fail [%d]", cfg->power_regulator_list[i]->name, ret);
			return ret;
		}
	}

	return ret;
}

static int mt9m114_reset(const struct mt9m114_config *cfg)
{
	int ret;

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_sleep(K_MSEC(20));

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 1);
	k_sleep(K_MSEC(20));

	ret = gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_sleep(K_MSEC(50));

	return ret;
}

static int mt9m114_init(const struct device *dev)
{
	const struct mt9m114_config *cfg = dev->config;
	struct mt9m114_data *drv_data = dev->data;
	uint16_t val;
	int ret;

	ret = pinctrl_apply_state(cfg->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret) {
		LOG_ERR("Configure pinctrl failed");
		return ret;
	}

	ret = mt9m114_configure_gpios(cfg);
	if (ret) {
		LOG_ERR("Configure gpios failed");
		return -ENODEV;
	}

	ret = mt9m114_power_up(cfg);
	if (ret) {
		LOG_ERR("Power up failed");
		return -ENODEV;
	}

	//ret = mt9m114_reset(cfg);
	//if (ret) {
	//	LOG_ERR("Reset failed");
	//	return -ENODEV;
	//}

	ret = mt9m114_read_reg(dev, MT9M114_CHIP_ID, sizeof(val), &val);
	if (ret) {
		LOG_ERR("Unable to read chip ID");
		return -ENODEV;
	}

	if (val != MT9M114_CHIP_ID_VAL) {
		LOG_ERR("Wrong ID: %04x (exp %04x)", val, MT9M114_CHIP_ID_VAL);
		return -ENODEV;
	}

	//ret = mt9m114_soft_reset(dev);
	//if (ret) {
	//	LOG_ERR("SW reset failed");
	//	return -ENODEV;
	//}

	drv_data->fmt.pixelformat = VIDEO_PIX_FMT_UYVY;
	drv_data->fmt.width = 640;
	drv_data->fmt.height = 480;
	drv_data->fmt.pitch = 640 * 2;

	drv_data->to_init = true;
	drv_data->curr_mode = &mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions) - 1];
	drv_data->last_mode = &mt9m114_resolutions[ARRAY_SIZE(mt9m114_resolutions) - 1];

	LOG_INF("camera %s is found", dev->name);

	return 0;
}

#if 1 /* Unique Instance */
PINCTRL_DT_INST_DEFINE(0);

#define MT9M114_GET_REGULATOR(node_id, prop, idx) \
	DEVICE_DT_GET(DT_PROP_BY_IDX(node_id, prop, idx))

static const struct device *power_regulators_0[] =
{
	DT_FOREACH_PROP_ELEM_SEP(DT_DRV_INST(0), regulators, MT9M114_GET_REGULATOR, (,))
};

static const struct mt9m114_config mt9m114_cfg_0 = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
	.power_regulator_list = power_regulators_0,
	.power_regulator_count = DT_INST_PROP_LEN(0, regulators),
	.powerdown_gpio = GPIO_DT_SPEC_INST_GET(0, powerdown_gpios),
	.reset_gpio = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
};

static struct mt9m114_data mt9m114_data_0;

static int mt9m114_init_0(const struct device *dev)
{
	const struct mt9m114_config *cfg = dev->config;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	return mt9m114_init(dev);
}

DEVICE_DT_INST_DEFINE(0, &mt9m114_init_0, NULL,
		    &mt9m114_data_0, &mt9m114_cfg_0,
		    POST_KERNEL, CONFIG_VIDEO_MT9M114_INIT_PRIORITY,
		    &mt9m114_driver_api);
#endif
