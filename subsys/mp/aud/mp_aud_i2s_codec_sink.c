/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/audio/codec.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

#include <zephyr/mp/mp_buffer.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>

#include <zephyr/mp/aud/mp_aud.h>
#include <zephyr/mp/aud/mp_aud_i2s_codec_sink.h>

LOG_MODULE_REGISTER(mp_aud_i2s_codec_sink, CONFIG_MP_LOG_LEVEL);

#define DEFAULT_PROP_I2S_DEVICE   DEVICE_DT_GET(DT_ALIAS(i2s_codec_tx))
#define DEFAULT_PROP_CODEC_DEVICE DEVICE_DT_GET(DT_NODELABEL(audio_codec));

/*
 * Number of buffers to queue into the I2S TX FIFO before issuing the START
 * trigger. The source and sink are clocked at the same rate, so the I2S
 * consumer can momentarily get one buffer ahead of the source right after
 * start. Priming more than one buffer keeps the TX queue from draining to
 * empty on a scheduling tie, which would otherwise raise a TX underrun.
 * This needs the negotiated buffer count to be at least 2 (true for any
 * double-buffered codec).
 */
#define AUD_I2S_SINK_START_PRIME 3

/* A capability of this sink is one the I2S link and the codec both support */
static int mp_aud_i2s_codec_sink_enum_caps(struct mp_pad *pad, uint32_t index,
					   const struct mp_structure *filter,
					   struct mp_structure *out)
{
	struct mp_aud_i2s_codec_sink *aud = (struct mp_aud_i2s_codec_sink *)pad->object.container;
	struct audio_caps i2s_caps;
	struct audio_caps codec_caps;
	struct audio_caps caps;

	if (i2s_get_caps(aud->i2s_dev, &i2s_caps, I2S_DIR_TX) != 0) {
		LOG_ERR("Failed to get I2S capabilities");
		return -ENODEV;
	}

	if (audio_codec_get_caps(aud->codec_dev, &codec_caps) != 0) {
		LOG_ERR("Failed to get codec capabilities");
		return -ENODEV;
	}

	if (i2s_caps.interleaved != codec_caps.interleaved) {
		LOG_ERR("Interleaved capabilities mismatch between I2S and codec");
		return -ENOTSUP;
	}

	caps.supported_sample_rates =
		i2s_caps.supported_sample_rates & codec_caps.supported_sample_rates;
	caps.supported_bit_widths = i2s_caps.supported_bit_widths & codec_caps.supported_bit_widths;
	caps.min_total_channels = MAX(i2s_caps.min_total_channels, codec_caps.min_total_channels);
	caps.max_total_channels = MIN(i2s_caps.max_total_channels, codec_caps.max_total_channels);
	caps.min_frame_interval = MAX(i2s_caps.min_frame_interval, codec_caps.min_frame_interval);
	caps.max_frame_interval = MIN(i2s_caps.max_frame_interval, codec_caps.max_frame_interval);
	caps.interleaved = codec_caps.interleaved;

	/*
	 * The I2S driver primes its transmit queue before the START trigger,
	 * holding that many buffers from the shared pool before any are
	 * transmitted and returned. Make sure the negotiated pool can satisfy
	 * this, otherwise the source starves before the sink ever starts.
	 */
	caps.min_num_buffers = MAX(MAX(i2s_caps.min_num_buffers, codec_caps.min_num_buffers),
				   AUD_I2S_SINK_START_PRIME);

	return mp_aud_enum_caps(&caps, index, filter, out);
}

static void mp_aud_i2s_codec_sink_update_caps(struct mp_sink *sink)
{
	/* The capabilities are enumerated from the devices, so nothing is built here */
	sink->sinkpad.enum_capsfn = mp_aud_i2s_codec_sink_enum_caps;
}

static int mp_aud_i2s_codec_sink_set_property(struct mp_object *obj, uint32_t key, const void *val)
{
	struct mp_aud_i2s_codec_sink *aud_i2s_codec_sink = (struct mp_aud_i2s_codec_sink *)obj;

	switch (key) {
	case MP_PROP_AUD_SINK_SLAB_PTR:
		aud_i2s_codec_sink->mem_slab = (struct k_mem_slab *)val;
		break;
	case MP_PROP_AUD_SINK_CLK_ROLE:
		if ((enum mp_aud_i2s_codec_clk_role)(uintptr_t)val !=
			    MP_AUD_I2S_CONTROLLER_CODEC_TARGET &&
		    (enum mp_aud_i2s_codec_clk_role)(uintptr_t)val !=
			    MP_AUD_I2S_TARGET_CODEC_CONTROLLER) {
			LOG_ERR("Invalid clock role value");
			return -EINVAL;
		}
		aud_i2s_codec_sink->clk_role = (enum mp_aud_i2s_codec_clk_role)(uintptr_t)val;
		break;
	case MP_PROP_AUD_SINK_I2S_DEVICE:
		aud_i2s_codec_sink->i2s_dev = (const struct device *)val;

		/* Device set, update supported caps */
		mp_aud_i2s_codec_sink_update_caps(&aud_i2s_codec_sink->sink);
		break;
	case MP_PROP_AUD_SINK_CODEC_DEVICE:
		aud_i2s_codec_sink->codec_dev = (const struct device *)val;

		/* Device set, update supported caps */
		mp_aud_i2s_codec_sink_update_caps(&aud_i2s_codec_sink->sink);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int mp_aud_i2s_codec_sink_get_property(struct mp_object *obj, uint32_t key, void *val)
{
	struct mp_aud_i2s_codec_sink *aud_i2s_codec_sink = (struct mp_aud_i2s_codec_sink *)obj;

	if (val == NULL) {
		return -1;
	}

	switch (key) {
	case MP_PROP_AUD_SINK_SLAB_PTR:
		if (aud_i2s_codec_sink->mem_slab != NULL) {
			*(void **)val = (void *)aud_i2s_codec_sink->mem_slab;
		} else {
			*(void **)val = NULL;
		}
		break;
	case MP_PROP_AUD_SINK_CLK_ROLE:
		*(enum mp_aud_i2s_codec_clk_role *)val = aud_i2s_codec_sink->clk_role;
		break;
	case MP_PROP_AUD_SINK_I2S_DEVICE:
		*(const struct device **)val = aud_i2s_codec_sink->i2s_dev;
		break;
	case MP_PROP_AUD_SINK_CODEC_DEVICE:
		*(const struct device **)val = aud_i2s_codec_sink->codec_dev;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int mp_aud_i2s_codec_sink_set_caps(struct mp_sink *sink, const struct mp_structure *caps)
{
	struct mp_aud_i2s_codec_sink *aud_i2s_codec_sink = (struct mp_aud_i2s_codec_sink *)sink;
	struct i2s_config config;
	struct audio_codec_cfg audio_cfg;
	int ret;

	uint32_t sample_rate, bit_width, num_of_channel, frame_interval;

	if (mp_aud_get_uint(caps, MP_CAPS_SAMPLE_RATE, &sample_rate) != 0 ||
	    mp_aud_get_uint(caps, MP_CAPS_BITWIDTH, &bit_width) != 0 ||
	    mp_aud_get_uint(caps, MP_CAPS_NUM_OF_CHANNEL, &num_of_channel) != 0 ||
	    mp_aud_get_uint(caps, MP_CAPS_FRAME_INTERVAL, &frame_interval) != 0) {
		return -EINVAL;
	}

	if (aud_i2s_codec_sink->mem_slab == NULL) {
		LOG_ERR("Memory slab not configured");
		return -EINVAL;
	}

	audio_cfg.dai_route = AUDIO_ROUTE_PLAYBACK;
	audio_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
	audio_cfg.dai_cfg.i2s.word_size = bit_width;
	audio_cfg.dai_cfg.i2s.channels = num_of_channel;
	audio_cfg.dai_cfg.i2s.format = I2S_FMT_DATA_FORMAT_I2S;

	if (aud_i2s_codec_sink->clk_role == MP_AUD_I2S_TARGET_CODEC_CONTROLLER) {
		audio_cfg.dai_cfg.i2s.options =
			I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
	} else {
		audio_cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_TARGET | I2S_OPT_BIT_CLK_TARGET;
	}

	audio_cfg.dai_cfg.i2s.frame_clk_freq = sample_rate;
	audio_cfg.dai_cfg.i2s.mem_slab = aud_i2s_codec_sink->mem_slab;
	audio_cfg.dai_cfg.i2s.block_size =
		(bit_width >> 3) * ((sample_rate * frame_interval / 1000000) * num_of_channel);
	ret = audio_codec_configure(aud_i2s_codec_sink->codec_dev, &audio_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure codec: %d", ret);
		return ret;
	}

#ifdef CONFIG_MP_AUD_I2S_CODEC_SINK_SET_OUTPUT_VOLUME
	ret = audio_codec_set_property(
		aud_i2s_codec_sink->codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL,
		(audio_property_value_t){.vol = CONFIG_MP_AUD_I2S_CODEC_SINK_OUTPUT_VOLUME});
	if (ret < 0) {
		LOG_WRN("Failed to set codec output volume: %d", ret);
	} else {
		ret = audio_codec_apply_properties(aud_i2s_codec_sink->codec_dev);
		if (ret < 0) {
			LOG_WRN("Failed to apply codec properties: %d", ret);
		}
	}
#endif

	k_msleep(1000);

	config.word_size = bit_width;
	config.channels = num_of_channel;
	config.format = I2S_FMT_DATA_FORMAT_I2S;

	if (aud_i2s_codec_sink->clk_role == MP_AUD_I2S_CONTROLLER_CODEC_TARGET) {
		config.options = I2S_OPT_BIT_CLK_CONTROLLER | I2S_OPT_FRAME_CLK_CONTROLLER;
	} else {
		config.options = I2S_OPT_BIT_CLK_TARGET | I2S_OPT_FRAME_CLK_TARGET;
	}

	config.frame_clk_freq = sample_rate;
	config.mem_slab = aud_i2s_codec_sink->mem_slab;
	config.block_size =
		(bit_width >> 3) * ((sample_rate * frame_interval / 1000000) * num_of_channel);
	config.timeout = frame_interval * 10;

	ret = i2s_configure(aud_i2s_codec_sink->i2s_dev, I2S_DIR_TX, &config);
	if (ret < 0) {
		LOG_ERR("Failed to configure I2S stream: %d", ret);
		return ret;
	}

	return 0;
}

int mp_aud_i2s_codec_sink_chainfn(struct mp_pad *pad, struct net_buf *in_buf,
				  struct net_buf **out_buf)
{
	struct mp_aud_i2s_codec_sink *aud_i2s_codec_sink = CONTAINER_OF(
		pad->object.container, struct mp_aud_i2s_codec_sink, sink.element.object);
	uint32_t bytes_used = mp_buffer_get_meta(in_buf)->bytes_used;
	int ret = -1;

	ret = i2s_write(aud_i2s_codec_sink->i2s_dev, in_buf->data, bytes_used);
	if (ret < 0) {
		LOG_DBG("Failed to write data: %d\n", ret);
		*out_buf = NULL;
		return -EIO;
	}

	if (!aud_i2s_codec_sink->started) {
		aud_i2s_codec_sink->count++;
		if (aud_i2s_codec_sink->count == AUD_I2S_SINK_START_PRIME) {
			ret = i2s_trigger(aud_i2s_codec_sink->i2s_dev, I2S_DIR_TX,
					  I2S_TRIGGER_START);
			if (ret < 0) {
				LOG_ERR("Failed to start I2S stream: %d", ret);
				*out_buf = NULL;
				return -EIO;
			}
			aud_i2s_codec_sink->started = true;
		}
	}

	/* Done with the buffer */
	net_buf_unref(in_buf);

	/* Sink returns NULL - end of chain */
	*out_buf = NULL;

	return 0;
}

void mp_aud_i2s_codec_sink_init(struct mp_element *self)
{
	struct mp_aud_i2s_codec_sink *aud_i2s_codec_sink = (struct mp_aud_i2s_codec_sink *)self;
	struct mp_sink *sink = &aud_i2s_codec_sink->sink;

	/* Init base class */
	mp_sink_init(self);

	aud_i2s_codec_sink->i2s_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(i2s_codec_tx));
	aud_i2s_codec_sink->codec_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(audio_codec));

	aud_i2s_codec_sink->clk_role = MP_AUD_I2S_CONTROLLER_CODEC_TARGET;

	self->object.get_property = mp_aud_i2s_codec_sink_get_property;
	self->object.set_property = mp_aud_i2s_codec_sink_set_property;

	sink->sinkpad.chainfn = mp_aud_i2s_codec_sink_chainfn;
	sink->set_caps = mp_aud_i2s_codec_sink_set_caps;

	mp_aud_i2s_codec_sink_update_caps(sink);

	aud_i2s_codec_sink->started = false;
	aud_i2s_codec_sink->count = 0;
	aud_i2s_codec_sink->mem_slab = NULL;
}
