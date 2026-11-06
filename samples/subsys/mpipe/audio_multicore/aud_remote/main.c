/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Remote side: eRPC server of the mpipe audio transform interface, running
 * the audio functions of aud_func.h on request of the Cortex-M33. It is the
 * whole application of the remote image, and serves the functions enabled
 * with CONFIG_AUD_REMOTE_FUNC_<FUNC>, which the sysbuild.cmake of each
 * sample sets for the functions its pipeline opens.
 *
 * Each client element opens a function with aud_open_rpc and gets a handle;
 * every later call carries it, and the server runs the function of that
 * instance with that instance's state. Several elements may open the same
 * function: each instance keeps its own state.
 *
 * The functions take blocks of 1 to AUD_MAX_CHANNELS channels of a power
 * of two samples, from AUD_MIN_BLOCK_SAMPLES to AUD_MAX_BLOCK_SAMPLES. The
 * Cortex-M33 sets the format of each pad of an instance once negotiated,
 * and the instance then processes blocks of that channel count and length.
 *
 * The input and output buffers are this core's own. Each instance has a set
 * of each of its own, so that two instances never write the same buffer. The
 * Cortex-M33 asks for the addresses of the buffers of an instance with its
 * handle, copies each block into a free input buffer and hands both
 * addresses back with every request. Only the addresses cross the eRPC link.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/audio/audio_caps.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "erpc_mbf_setup.h"
#include "erpc_server_setup.h"
#include "erpc_transport_setup.h"

#include "aud_func.h"
#include "aud_remote_log.h"
#include "c_mpipe_aud_transform_server.h"
#include "mpipe_aud_transform_common.h"

/*
 * Buffers of each direction of one instance. A processing call takes one
 * input and one output buffer; an element upstream that hands out the input
 * buffers (a queue in front of the first stage) holds a few more.
 */
#define AUD_NUM_BUFS 4U

/* Function instances the server can hold at once */
#define AUD_MAX_INSTANCES 4U

BUILD_ASSERT(IS_POWER_OF_TWO(AUD_BUF_ALIGN), "CONFIG_AUD_REMOTE_BUF_ALIGN must be a power of two");
BUILD_ASSERT((AUD_BUF_SIZE % AUD_BUF_ALIGN) == 0U);
#if defined(CONFIG_DCACHE)
/* The buffers are invalidated whole: none may share a cache line with other data */
BUILD_ASSERT((AUD_BUF_ALIGN % CONFIG_DCACHE_LINE_SIZE) == 0U,
	     "CONFIG_AUD_REMOTE_BUF_ALIGN must be a multiple of CONFIG_DCACHE_LINE_SIZE");
#endif
/* The alignment travels as 16 bits */
BUILD_ASSERT(AUD_BUF_ALIGN <= UINT16_MAX);

struct aud_instance {
	const struct aud_func *func;
	void *state;
	/* The buffers of this instance, AUD_NUM_BUFS of each direction */
	uint8_t (*in_bufs)[AUD_BUF_SIZE];
	uint8_t (*out_bufs)[AUD_BUF_SIZE];
	/* Channels of each pad, 0 until the Cortex-M33 sets its format */
	uint8_t in_channels;
	uint8_t out_channels;
	/* Samples of one channel in a block, the same on both pads */
	uint32_t samples;
	/* Bytes of one block of the format of each pad */
	uint32_t in_size;
	uint32_t out_size;
};

static uint8_t in_bufs[AUD_MAX_INSTANCES][AUD_NUM_BUFS][AUD_BUF_SIZE] __aligned(AUD_BUF_ALIGN);
static uint8_t out_bufs[AUD_MAX_INSTANCES][AUD_NUM_BUFS][AUD_BUF_SIZE] __aligned(AUD_BUF_ALIGN);

static struct aud_instance instances[AUD_MAX_INSTANCES];

/* clang-format off */
static const struct aud_func *const funcs[] = {
	IF_ENABLED(CONFIG_AUD_REMOTE_FUNC_GAIN, (&aud_func_gain,))
	IF_ENABLED(CONFIG_AUD_REMOTE_FUNC_FFT, (&aud_func_fft,))
	IF_ENABLED(CONFIG_AUD_REMOTE_FUNC_IFFT, (&aud_func_ifft,))
};
/* clang-format on */

BUILD_ASSERT(ARRAY_SIZE(funcs) > 0U, "No CONFIG_AUD_REMOTE_FUNC_<FUNC> is enabled");

/*
 * AUDIO_SAMPLE_RATE_<hz>, the bit of the configured rate. A rate without a
 * bit names no macro and fails the build.
 */
#define AUD_SAMPLE_RATE_BIT UTIL_CAT(AUDIO_SAMPLE_RATE_, AUD_SAMPLE_RATE)

/* The wire carries the audio_caps values as they are */
BUILD_ASSERT((int)MPIPE_RPC_AUD_FORMAT_PCM == (int)AUDIO_FORMAT_PCM);
BUILD_ASSERT((int)MPIPE_RPC_AUD_FORMAT_FFT == (int)AUDIO_FORMAT_FFT);

/* Blocks are captured interleaved, and the spectra follow channel after channel */
const mpipe_rpc_aud_caps aud_pcm_cap = {
	.min_total_channels = 1U,
	.max_total_channels = AUD_MAX_CHANNELS,
	.supported_sample_rates = AUD_SAMPLE_RATE_BIT,
	.supported_bit_widths = AUDIO_BIT_WIDTH_32,
	.min_frame_interval = AUD_MIN_FRAME_INTERVAL_US,
	.max_frame_interval = AUD_MAX_FRAME_INTERVAL_US,
	.interleaved = true,
	.format = MPIPE_RPC_AUD_FORMAT_PCM,
};

const mpipe_rpc_aud_caps aud_stereo_cap = {
	.min_total_channels = 2U,
	.max_total_channels = 2U,
	.supported_sample_rates = AUD_SAMPLE_RATE_BIT,
	.supported_bit_widths = AUDIO_BIT_WIDTH_32,
	.min_frame_interval = AUD_MIN_FRAME_INTERVAL_US,
	.max_frame_interval = AUD_MAX_FRAME_INTERVAL_US,
	.interleaved = true,
	.format = MPIPE_RPC_AUD_FORMAT_PCM,
};

BUILD_ASSERT(AUD_PCM_BIT_WIDTH == 32U, "the PCM caps advertise AUDIO_BIT_WIDTH_32");

/* The bins are float pairs: the bit width is that of one part */
const mpipe_rpc_aud_caps aud_fft_cap = {
	.min_total_channels = 1U,
	.max_total_channels = AUD_MAX_CHANNELS,
	.supported_sample_rates = AUD_SAMPLE_RATE_BIT,
	.supported_bit_widths = AUDIO_BIT_WIDTH_32,
	.min_frame_interval = AUD_MIN_FRAME_INTERVAL_US,
	.max_frame_interval = AUD_MAX_FRAME_INTERVAL_US,
	.interleaved = false,
	.format = MPIPE_RPC_AUD_FORMAT_FFT,
};

/* True when [min_a, max_a] and [min_b, max_b] share a value */
static bool spans_overlap(uint32_t min_a, uint32_t max_a, uint32_t min_b, uint32_t max_b)
{
	return min_a <= max_b && min_b <= max_a;
}

/* True when cap fits ref: same format, the masks share a bit and the spans a value */
static bool cap_matches(const mpipe_rpc_aud_caps *cap, const mpipe_rpc_aud_caps *ref)
{
	return cap->format == ref->format &&
	       (cap->supported_sample_rates & ref->supported_sample_rates) != 0U &&
	       (cap->supported_bit_widths & ref->supported_bit_widths) != 0U &&
	       spans_overlap(cap->min_total_channels, cap->max_total_channels,
			     ref->min_total_channels, ref->max_total_channels) &&
	       spans_overlap(cap->min_frame_interval, cap->max_frame_interval,
			     ref->min_frame_interval, ref->max_frame_interval);
}

/* True when cap is one exact format: one bit of each mask, min == max */
static bool cap_is_fixed(const mpipe_rpc_aud_caps *cap)
{
	return IS_POWER_OF_TWO(cap->supported_sample_rates) && cap->supported_sample_rates != 0U &&
	       IS_POWER_OF_TWO(cap->supported_bit_widths) && cap->supported_bit_widths != 0U &&
	       cap->min_total_channels == cap->max_total_channels &&
	       cap->min_frame_interval == cap->max_frame_interval;
}

static const char *format_name(const mpipe_rpc_aud_caps *caps)
{
	return (caps->format == MPIPE_RPC_AUD_FORMAT_FFT) ? "FFT" : "PCM";
}

/* Handles are the instance index plus one, so that 0 is never valid */
static struct aud_instance *instance_of(uint32_t handle)
{
	if (handle == 0U || handle > ARRAY_SIZE(instances)) {
		return NULL;
	}

	if (instances[handle - 1U].func == NULL) {
		return NULL;
	}

	return &instances[handle - 1U];
}

static const mpipe_rpc_aud_caps *cap_of(const struct aud_instance *inst,
					mpipe_rpc_aud_pad_direction direction)
{
	if (direction == MPIPE_RPC_AUD_PAD_SINK) {
		return inst->func->in_cap;
	}

	if (direction == MPIPE_RPC_AUD_PAD_SRC) {
		return inst->func->out_cap;
	}

	return NULL;
}

/* Index of the buffer starting at addr, or -1 when it is none of ours */
static int buf_index(const uint8_t *base, size_t buf_size, uint32_t addr)
{
	for (uint32_t i = 0U; i < AUD_NUM_BUFS; i++) {
		if (addr == (uint32_t)(uintptr_t)&base[i * buf_size]) {
			return (int)i;
		}
	}

	return -1;
}

int32_t aud_open_rpc(mpipe_rpc_aud_func func, uint32_t *handle)
{
	const struct aud_func *f = NULL;

	for (size_t i = 0U; i < ARRAY_SIZE(funcs); i++) {
		if (funcs[i]->id == func) {
			f = funcs[i];
			break;
		}
	}

	if (f == NULL) {
		return -ENOTSUP;
	}

	for (uint32_t i = 0U; i < ARRAY_SIZE(instances); i++) {
		struct aud_instance *inst = &instances[i];

		if (inst->func != NULL) {
			continue;
		}

		inst->state = f->open();
		if (inst->state == NULL) {
			return -ENOMEM;
		}

		inst->func = f;
		inst->in_bufs = in_bufs[i];
		inst->out_bufs = out_bufs[i];
		*handle = i + 1U;

		aud_log(AUD_LOG_INF, "[DSP] Opened %s as handle %u", f->name, *handle);

		return 0;
	}

	return -ENOMEM;
}

int32_t aud_get_buf_caps_rpc(uint32_t handle, mpipe_rpc_aud_pad_direction direction,
			     uint8_t *num_buffers, uint32_t *buf_size, uint16_t *buf_align)
{
	if (instance_of(handle) == NULL) {
		return -EBADF;
	}

	if (direction != MPIPE_RPC_AUD_PAD_SINK && direction != MPIPE_RPC_AUD_PAD_SRC) {
		return -EINVAL;
	}

	*buf_size = AUD_BUF_SIZE;
	*num_buffers = AUD_NUM_BUFS;
	*buf_align = AUD_BUF_ALIGN;

	return 0;
}

int32_t aud_get_buf_rpc(uint32_t handle, mpipe_rpc_aud_pad_direction direction, uint8_t ind,
			uint32_t *addr)
{
	const struct aud_instance *inst = instance_of(handle);

	if (inst == NULL) {
		return -EBADF;
	}

	if (ind >= AUD_NUM_BUFS) {
		return -ERANGE;
	}

	if (direction == MPIPE_RPC_AUD_PAD_SINK) {
		*addr = (uint32_t)(uintptr_t)inst->in_bufs[ind];
	} else if (direction == MPIPE_RPC_AUD_PAD_SRC) {
		*addr = (uint32_t)(uintptr_t)inst->out_bufs[ind];
	} else {
		return -EINVAL;
	}

	return 0;
}

int32_t aud_get_format_caps_rpc(uint32_t handle, mpipe_rpc_aud_pad_direction direction, uint8_t ind,
				mpipe_rpc_aud_caps *caps)
{
	const struct aud_instance *inst = instance_of(handle);
	const mpipe_rpc_aud_caps *ref;

	if (inst == NULL) {
		return -EBADF;
	}

	ref = cap_of(inst, direction);
	if (ref == NULL) {
		return -EINVAL;
	}

	if (ind > 0U) {
		return -ERANGE;
	}

	*caps = *ref;

	return 0;
}

int32_t aud_set_format_rpc(uint32_t handle, mpipe_rpc_aud_pad_direction direction,
			   const mpipe_rpc_aud_caps *caps, uint32_t *buf_size)
{
	struct aud_instance *inst = instance_of(handle);
	const mpipe_rpc_aud_caps *ref;
	bool sink = (direction == MPIPE_RPC_AUD_PAD_SINK);
	uint8_t channels;
	uint32_t samples;
	uint32_t size;
	uint8_t other;

	if (inst == NULL) {
		return -EBADF;
	}

	ref = cap_of(inst, direction);
	if (ref == NULL) {
		return -EINVAL;
	}

	if (!cap_is_fixed(caps) || !cap_matches(caps, ref)) {
		aud_log(AUD_LOG_WRN,
			"[DSP] Handle %u %s %s refused: %s, %u ch, rates 0x%x, widths 0x%x, %u us",
			handle, inst->func->name, sink ? "in" : "out", format_name(caps),
		       caps->min_total_channels, caps->supported_sample_rates,
		       caps->supported_bit_widths, caps->min_frame_interval);
		return cap_is_fixed(caps) ? -ENOTSUP : -EINVAL;
	}

	/* Samples of one channel, counted the way the microphone source of mpipe counts them */
	channels = (uint8_t)caps->min_total_channels;
	samples = (uint32_t)(((uint64_t)AUD_SAMPLE_RATE * caps->min_frame_interval) / USEC_PER_SEC);

	/*
	 * The buffers and the FFT work memory hold the largest block only, and
	 * every block is an FFT length
	 */
	if (samples < AUD_MIN_BLOCK_SAMPLES || samples > AUD_MAX_BLOCK_SAMPLES ||
	    !IS_POWER_OF_TWO(samples)) {
		aud_log(AUD_LOG_WRN, "[DSP] Handle %u %s %s refused: %u us is %u samples", handle,
			inst->func->name, sink ? "in" : "out", caps->min_frame_interval, samples);
		return -ENOTSUP;
	}

	/*
	 * Every function writes a block of the length of its input, and one
	 * that keeps the channel count takes the same on both pads
	 */
	other = sink ? inst->out_channels : inst->in_channels;
	if (other != 0U && (samples != inst->samples ||
			    (inst->func->keeps_channels && other != channels))) {
		aud_log(AUD_LOG_WRN,
			"[DSP] Handle %u %s %s refused: %u ch of %u samples, %u ch of %u on the "
			"other pad",
			handle, inst->func->name, sink ? "in" : "out", channels, samples, other,
			inst->samples);
		return -EINVAL;
	}

	size = (caps->format == MPIPE_RPC_AUD_FORMAT_FFT) ? AUD_FFT_SIZE(channels, samples)
							  : AUD_PCM_SIZE(channels, samples);
	inst->samples = samples;
	*buf_size = size;

	if (sink) {
		inst->in_channels = channels;
		inst->in_size = size;

		/* A new stream starts from silence */
		inst->func->reset(inst->state);
	} else {
		inst->out_channels = channels;
		inst->out_size = size;
	}

	aud_log(AUD_LOG_INF,
		"[DSP] Handle %u %s %s: %s, %u ch, %u Hz, %u bit, %u us (%u samples), %u bytes",
		handle, inst->func->name, sink ? "in" : "out", format_name(caps), channels,
		AUD_SAMPLE_RATE, AUD_PCM_BIT_WIDTH, caps->min_frame_interval, samples, *buf_size);

	return 0;
}

int32_t aud_transform_cap_rpc(uint32_t handle, mpipe_rpc_aud_pad_direction direction, uint16_t ind,
			      const mpipe_rpc_aud_caps *caps, mpipe_rpc_aud_caps *other_caps)
{
	const struct aud_instance *inst = instance_of(handle);
	const mpipe_rpc_aud_caps *from;
	const mpipe_rpc_aud_caps *to;

	if (inst == NULL) {
		return -EBADF;
	}

	to = cap_of(inst, direction);
	if (to == NULL || ind > 0U) {
		return -EINVAL;
	}

	/* The output format is fixed by the input one, and the other way round */
	from = (direction == MPIPE_RPC_AUD_PAD_SRC) ? inst->func->in_cap : inst->func->out_cap;
	if (!cap_matches(caps, from)) {
		return -EINVAL;
	}

	*other_caps = *to;

	/* Every function keeps the block duration */
	other_caps->min_frame_interval = MAX(caps->min_frame_interval, to->min_frame_interval);
	other_caps->max_frame_interval = MIN(caps->max_frame_interval, to->max_frame_interval);

	/* The channel counts both pads take, for a function that keeps the count */
	if (inst->func->keeps_channels) {
		other_caps->min_total_channels =
			MAX(caps->min_total_channels, to->min_total_channels);
		other_caps->max_total_channels =
			MIN(caps->max_total_channels, to->max_total_channels);
	}

	return 0;
}

int32_t aud_process_fn_rpc(uint32_t handle, uint32_t in_buf, uint32_t in_sz, uint32_t out_buf,
			   uint32_t *out_sz)
{
	struct aud_instance *inst = instance_of(handle);
	uint8_t *in;
	uint8_t *out;
	int in_ind;
	int out_ind;
	uint32_t written = 0U;
	int32_t ret;

	if (inst == NULL) {
		return -EBADF;
	}

	/*
	 * The addresses come from the other core: accept only the buffers
	 * handed out for this instance
	 */
	in_ind = buf_index(&inst->in_bufs[0][0], AUD_BUF_SIZE, in_buf);
	out_ind = buf_index(&inst->out_bufs[0][0], AUD_BUF_SIZE, out_buf);
	if (in_ind < 0 || out_ind < 0) {
		return -EINVAL;
	}

	/* Both formats must be set, and the block must fit the input one */
	if (inst->in_channels == 0U || inst->out_channels == 0U || in_sz > inst->in_size) {
		return -EINVAL;
	}

	in = inst->in_bufs[in_ind];
	out = inst->out_bufs[out_ind];

	/* The Cortex-M33 wrote the block to memory: drop what this core has cached of it */
	(void)sys_cache_data_invd_range(in, AUD_BUF_SIZE);

	ret = inst->func->process(inst->state, inst->in_channels, inst->samples, in, in_sz, out,
				  &written);
	if (ret != 0) {
		return ret;
	}

	/* Push the result out of this core's cache for the Cortex-M33 */
	(void)sys_cache_data_flush_range(out, AUD_BUF_SIZE);

	*out_sz = MIN(written, inst->out_size);

	return 0;
}

int main(void)
{
	erpc_transport_t transport;
	erpc_server_t server;
	erpc_status_t status;

	/* Kept in the log until the Cortex-M33 reads it, once eRPC is up */
	aud_log(AUD_LOG_INF, "[DSP] Audio server on %s/%s", CONFIG_BOARD, CONFIG_BOARD_QUALIFIERS);

	transport = erpc_transport_zephyr_ipc_remote_init(CONFIG_ERPC_ENDPOINT_NAME, 0);
	if (transport == NULL) {
		aud_log(AUD_LOG_ERR, "[DSP] eRPC transport init failed");
		return 0;
	}

	server = erpc_server_init(transport, erpc_mbf_ipc_service_init(transport));
	erpc_add_service_to_server(server, create_MpipeAudTransform_service());
	/* The Cortex-M33 reads the log through its own interface */
	erpc_add_service_to_server(server, create_MpipeAudLog_service());

	/* One call at a time: the functions, their buffers and the log need no lock */
	status = erpc_server_run(server);
	aud_log(AUD_LOG_ERR, "[DSP] eRPC server stopped (%d)", (int)status);

	return 0;
}
