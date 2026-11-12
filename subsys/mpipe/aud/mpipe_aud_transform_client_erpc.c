/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/erpc/mpipe_erpc.h>
#include <zephyr/mpipe/aud/mpipe_aud_transform_client.h>
#include <zephyr/mpipe/aud/mpipe_aud_transform_client_erpc.h>

#include "c_mpipe_aud_transform_client.h"
#include "mpipe_aud_transform_common.h"
#include "mpipe_aud_transform_client_erpc_log.h"

LOG_MODULE_REGISTER(mpipe_aud_transform_client_erpc, CONFIG_MPIPE_LOG_LEVEL);

/* The direction, the format and the function cross the wire as their numeric values */
BUILD_ASSERT((int)MPIPE_RPC_AUD_PAD_UNKNOWN == (int)MPIPE_PAD_UNKNOWN);
BUILD_ASSERT((int)MPIPE_RPC_AUD_PAD_SRC == (int)MPIPE_PAD_SRC);
BUILD_ASSERT((int)MPIPE_RPC_AUD_PAD_SINK == (int)MPIPE_PAD_SINK);

BUILD_ASSERT((int)MPIPE_RPC_AUD_FORMAT_PCM == (int)AUDIO_FORMAT_PCM);
BUILD_ASSERT((int)MPIPE_RPC_AUD_FORMAT_FFT == (int)AUDIO_FORMAT_FFT);
BUILD_ASSERT((int)MPIPE_RPC_AUD_FUNC_GAIN == (int)MPIPE_AUD_REMOTE_FUNC_GAIN);
BUILD_ASSERT((int)MPIPE_RPC_AUD_FUNC_FFT == (int)MPIPE_AUD_REMOTE_FUNC_FFT);
BUILD_ASSERT((int)MPIPE_RPC_AUD_FUNC_IFFT == (int)MPIPE_AUD_REMOTE_FUNC_IFFT);

/* mpipe_aud_transform.erpc returns -5 for -EIO when a call fails in eRPC */
BUILD_ASSERT(EIO == 5);

/*
 * The remote serves one call at a time and the generated client is not
 * reentrant: every element calls through this lock, so calls from several
 * pipeline threads run one after the other, each to completion.
 */
static K_MUTEX_DEFINE(rpc_lock);

/* Set once the interface is bound: the generated client binds only once */
static bool rpc_ready;

/*
 * Print what the remote logged during the control call that returned ret.
 * The caller holds rpc_lock. A call eRPC itself failed (-EIO) is not followed
 * by a read, which would most likely fail the same way.
 */
static void mpipe_aud_transform_client_erpc_drain(int32_t ret)
{
	if (ret != -EIO) {
		mpipe_aud_transform_client_erpc_log_drain();
	}
}

#if defined(CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS) && (CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS > 0)
static void mpipe_aud_transform_client_erpc_poll(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(log_poll, mpipe_aud_transform_client_erpc_poll);

/* Read the log between control calls too, for what the remote logs while streaming */
static void mpipe_aud_transform_client_erpc_poll(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	mpipe_aud_transform_client_erpc_log_drain();
	(void)k_mutex_unlock(&rpc_lock);

	(void)k_work_reschedule(&log_poll, K_MSEC(CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS));
}
#endif

static void mpipe_aud_transform_client_erpc_caps_to_rpc(const struct audio_caps *caps,
							mpipe_rpc_aud_caps *rpc)
{
	rpc->min_total_channels = caps->min_total_channels;
	rpc->max_total_channels = caps->max_total_channels;
	rpc->supported_sample_rates = caps->supported_sample_rates;
	rpc->supported_bit_widths = caps->supported_bit_widths;
	rpc->min_num_buffers = caps->min_num_buffers;
	rpc->min_frame_interval = caps->min_frame_interval;
	rpc->max_frame_interval = caps->max_frame_interval;
	rpc->interleaved = caps->interleaved;
	rpc->format = (mpipe_rpc_aud_format)caps->format;
}

static void mpipe_aud_transform_client_erpc_rpc_to_caps(const mpipe_rpc_aud_caps *rpc,
							struct audio_caps *caps)
{
	caps->min_total_channels = rpc->min_total_channels;
	caps->max_total_channels = rpc->max_total_channels;
	caps->supported_sample_rates = rpc->supported_sample_rates;
	caps->supported_bit_widths = rpc->supported_bit_widths;
	caps->min_num_buffers = rpc->min_num_buffers;
	caps->min_frame_interval = rpc->min_frame_interval;
	caps->max_frame_interval = rpc->max_frame_interval;
	caps->interleaved = rpc->interleaved;
	caps->format = (uint8_t)rpc->format;
}

static int32_t mpipe_aud_transform_client_erpc_open(enum mpipe_aud_remote_func func,
						    uint32_t *handle)
{
	int32_t ret;

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_open_rpc((mpipe_rpc_aud_func)func, handle);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

static int32_t mpipe_aud_transform_client_erpc_get_buf_caps(uint32_t handle,
							    enum mpipe_pad_direction direction,
							    uint8_t *num_buffers,
							    uint32_t *buf_size, uint16_t *buf_align)
{
	int32_t ret;

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_get_buf_caps_rpc(handle, (mpipe_rpc_aud_pad_direction)direction, num_buffers,
				   buf_size, buf_align);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

static int32_t mpipe_aud_transform_client_erpc_get_buf(uint32_t handle,
						       enum mpipe_pad_direction direction,
						       uint8_t ind, uint32_t *addr)
{
	int32_t ret;

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_get_buf_rpc(handle, (mpipe_rpc_aud_pad_direction)direction, ind, addr);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

static int32_t mpipe_aud_transform_client_erpc_get_format_caps(uint32_t handle,
							       enum mpipe_pad_direction direction,
							       uint8_t ind, struct audio_caps *caps)
{
	mpipe_rpc_aud_caps rpc;
	int32_t ret;

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_get_format_caps_rpc(handle, (mpipe_rpc_aud_pad_direction)direction, ind, &rpc);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	if (ret != 0) {
		return ret;
	}

	mpipe_aud_transform_client_erpc_rpc_to_caps(&rpc, caps);

	return 0;
}

static int32_t mpipe_aud_transform_client_erpc_set_format(uint32_t handle,
							  enum mpipe_pad_direction direction,
							  const struct audio_caps *caps,
							  uint32_t *buf_size)
{
	mpipe_rpc_aud_caps rpc;
	int32_t ret;

	mpipe_aud_transform_client_erpc_caps_to_rpc(caps, &rpc);

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_set_format_rpc(handle, (mpipe_rpc_aud_pad_direction)direction, &rpc, buf_size);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

static int32_t mpipe_aud_transform_client_erpc_transform_cap(uint32_t handle,
							     enum mpipe_pad_direction direction,
							     uint16_t ind,
							     const struct audio_caps *caps,
							     struct audio_caps *other_caps)
{
	mpipe_rpc_aud_caps rpc, other_rpc;
	int32_t ret;

	mpipe_aud_transform_client_erpc_caps_to_rpc(caps, &rpc);

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_transform_cap_rpc(handle, (mpipe_rpc_aud_pad_direction)direction, ind, &rpc,
				    &other_rpc);
	mpipe_aud_transform_client_erpc_drain(ret);
	(void)k_mutex_unlock(&rpc_lock);

	if (ret != 0) {
		return ret;
	}

	mpipe_aud_transform_client_erpc_rpc_to_caps(&other_rpc, other_caps);

	return 0;
}

static int32_t mpipe_aud_transform_client_erpc_process(uint32_t handle, uint32_t in_buf,
						       uint32_t in_sz, uint32_t out_buf,
						       uint32_t *out_sz)
{
	int32_t ret;

	/* No log read here: this runs once per block, on the data path */
	(void)k_mutex_lock(&rpc_lock, K_FOREVER);
	ret = aud_process_fn_rpc(handle, in_buf, in_sz, out_buf, out_sz);
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

/* Bind the interface, once for every element */
static int mpipe_aud_transform_client_erpc_init_rpc(void)
{
	erpc_client_t client;
	int ret = 0;

	(void)k_mutex_lock(&rpc_lock, K_FOREVER);

	if (rpc_ready) {
		goto out;
	}

	client = mpipe_erpc_client_init();
	if (client == NULL) {
		ret = -EIO;
		goto out;
	}

	/* The generated client asserts each interface is bound only once */
	initMpipeAudTransform_client(client);
	IF_ENABLED(CONFIG_MPIPE_AUD_REMOTE_LOG, (initMpipeAudLog_client(client);))
	rpc_ready = true;

	/* What the remote logged while it booted, its banner first */
	mpipe_aud_transform_client_erpc_log_drain();

#if defined(CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS) && (CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS > 0)
	(void)k_work_schedule(&log_poll, K_MSEC(CONFIG_MPIPE_AUD_REMOTE_LOG_POLL_MS));
#endif

out:
	(void)k_mutex_unlock(&rpc_lock);

	return ret;
}

int mpipe_aud_transform_client_erpc_init(struct mpipe_aud_transform_client *atc, uint8_t id,
					 enum mpipe_aud_remote_func func)
{
	__ASSERT_NO_MSG(atc != NULL);

	atc->transform.init_rpc = mpipe_aud_transform_client_erpc_init_rpc;

	atc->open_rpc = mpipe_aud_transform_client_erpc_open;
	atc->get_buf_caps_rpc = mpipe_aud_transform_client_erpc_get_buf_caps;
	atc->get_buf_rpc = mpipe_aud_transform_client_erpc_get_buf;
	atc->get_format_caps_rpc = mpipe_aud_transform_client_erpc_get_format_caps;
	atc->set_format_rpc = mpipe_aud_transform_client_erpc_set_format;
	atc->transform_cap_rpc = mpipe_aud_transform_client_erpc_transform_cap;
	atc->process_rpc = mpipe_aud_transform_client_erpc_process;

	return mpipe_aud_transform_client_init(atc, id, func);
}
