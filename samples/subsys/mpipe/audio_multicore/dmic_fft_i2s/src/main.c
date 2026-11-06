/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Microphone -> gain -> FFT -> IFFT -> gain -> codec, as an mpipe pipeline:
 *
 *   dmic_src -> caps_filter -> queue -> gain -> fft -> ifft -> out_gain -> i2s_sink
 *
 * The HiFi4 computes gain, fft, ifft and out_gain over eRPC: each is an
 * audio transform client that opens its own function on the HiFi4, the two
 * gains each an instance of the same function. The output of each stage
 * comes back to the Cortex-M33, which copies it into a free HiFi4 input
 * buffer for the next one. fft computes one spectrum per microphone
 * channel, and ifft turns them back into the two channels of the codec.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/aud/mpipe_aud.h>
#include <zephyr/mpipe/aud/mpipe_aud_dmic_src.h>
#include <zephyr/mpipe/aud/mpipe_aud_i2s_codec_sink.h>
#include <zephyr/mpipe/aud/mpipe_aud_src.h>
#include <zephyr/mpipe/aud/mpipe_aud_transform_client_erpc.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/utils/mpipe_player.h>

#include <dsp.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Element IDs (values are arbitrary; only uniqueness within the pipeline matters) */
enum {
	PIPE_ID,
	DMIC_SRC_ID,
	CAPS_FILTER_ID,
	QUEUE_ID,
	GAIN_ID,
	FFT_ID,
	IFFT_ID,
	OUT_GAIN_ID,
	I2S_SINK_ID,
};

/* Samples in one block, as the microphone source counts them */
#define BLOCK_SAMPLES                                                                              \
	((uint32_t)(((uint64_t)DMIC_FFT_I2S_SAMPLE_RATE * DMIC_FFT_I2S_FRAME_INTERVAL_US) /        \
		    USEC_PER_SEC))

/* One block of the codec stream: 32-bit stereo */
#define TX_BLOCK_SIZE (BLOCK_SAMPLES * sizeof(int32_t) * 2U)

/*
 * Blocks of the codec stream: the sink primes 3 before it starts, the I2S
 * driver queues CONFIG_I2S_TX_BLOCK_COUNT and its DMA holds the one it plays.
 */
#define TX_BLOCK_COUNT 6U

/*
 * The DMIC source takes its blocks from this slab, as in the dmic_i2s sample.
 * Kept uncached because the audio DMA writes it.
 */
__nocache struct k_mem_slab mem_slab;

/*
 * The I2S sink copies each block from the HiFi4 into this slab: the DMA
 * reads it, so it is uncached too.
 */
K_MEM_SLAB_DEFINE_IN_SECT_STATIC(tx_slab, __nocache, TX_BLOCK_SIZE, TX_BLOCK_COUNT, 32);

static struct mpipe pipe;
static struct mpipe_aud_dmic_src dmic_src;
static struct mpipe_caps_filter caps_filter;
static struct mpipe_queue queue;
static struct mpipe_aud_transform_client gain;
static struct mpipe_aud_transform_client fft;
static struct mpipe_aud_transform_client ifft;
static struct mpipe_aud_transform_client out_gain;
static struct mpipe_aud_i2s_codec_sink i2s_sink;
static struct mpipe_player player;

int main(void)
{
	uint8_t queue_size = 2U;
	enum mpipe_base_queue_leak queue_leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_aud_dmic_src_init(&dmic_src, DMIC_SRC_ID);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&dmic_src,
					  MPIPE_PROP_AUD_SRC_SLAB_PTR, &mem_slab,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	if (ret != 0) {
		goto err;
	}

	/*
	 * The PCM format the HiFi4 takes: 32-bit, the only width the MICFIL
	 * driver produces, the two microphones interleaved. The DMIC_FFT_I2S_*
	 * values come from dmic_fft_i2s.cmake, set by sysbuild for both images.
	 */
	ret = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_AUDIO_PCM, MPIPE_CAPS_SAMPLE_RATE,
					  MPIPE_TYPE_UINT, DMIC_FFT_I2S_SAMPLE_RATE,
					  MPIPE_CAPS_BIT_WIDTH, MPIPE_TYPE_UINT, 32U,
					  MPIPE_CAPS_NUM_CHANNELS, MPIPE_TYPE_UINT, 2U,
					  MPIPE_CAPS_FRAME_INTERVAL, MPIPE_TYPE_UINT,
					  DMIC_FFT_I2S_FRAME_INTERVAL_US, MPIPE_CAPS_END);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
					  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		goto err;
	}

	/*
	 * The queue runs the HiFi4 stages and the playback on a thread of
	 * their own. When they fall behind, it drops the oldest block instead
	 * of holding the capture: MICFIL stops when it runs out of blocks.
	 */
	ret = mpipe_queue_init(&queue, QUEUE_ID);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&queue,
					  MPIPE_PROP_BASE_QUEUE_SIZE, &queue_size,
					  MPIPE_PROP_BASE_QUEUE_LEAK, &queue_leak,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_aud_i2s_codec_sink_init(&i2s_sink, I2S_SINK_ID);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_object_set_properties((struct mpipe_object *)&i2s_sink,
					  MPIPE_PROP_AUD_SINK_SLAB_PTR, &tx_slab,
					  MPIPE_PROP_LIST_END);
	if (ret != 0) {
		goto err;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&dmic_src,
			    (struct mpipe_element *)&caps_filter, (struct mpipe_element *)&queue,
			    (struct mpipe_element *)&i2s_sink, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to add elements (%d)", ret);
		goto err;
	}

	ret = mpipe_element_link((struct mpipe_element *)&dmic_src,
				 (struct mpipe_element *)&caps_filter,
				 (struct mpipe_element *)&queue, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to link elements (%d)", ret);
		goto err;
	}

	/* The HiFi4 stages, between the queue and the codec */
	dsp_start();

	/*
	 * The first element brings up eRPC: the HiFi4 answers once booted.
	 * Each element opens its own function on the HiFi4 and asks for the
	 * buffers of that instance.
	 */
	ret = mpipe_aud_transform_client_erpc_init(&gain, GAIN_ID, MPIPE_AUD_REMOTE_FUNC_GAIN);
	if (ret != 0) {
		LOG_ERR("HiFi4 did not answer (%d)", ret);
		goto err;
	}

	LOG_INF("HiFi4 ready");

	ret = mpipe_aud_transform_client_erpc_init(&fft, FFT_ID, MPIPE_AUD_REMOTE_FUNC_FFT);
	if (ret != 0) {
		LOG_ERR("HiFi4 refused the FFT (%d)", ret);
		goto err;
	}

	ret = mpipe_aud_transform_client_erpc_init(&ifft, IFFT_ID, MPIPE_AUD_REMOTE_FUNC_IFFT);
	if (ret != 0) {
		LOG_ERR("HiFi4 refused the IFFT (%d)", ret);
		goto err;
	}

	/* A second instance of the gain function, with the same gain */
	ret = mpipe_aud_transform_client_erpc_init(&out_gain, OUT_GAIN_ID,
						   MPIPE_AUD_REMOTE_FUNC_GAIN);
	if (ret != 0) {
		LOG_ERR("HiFi4 refused the output gain (%d)", ret);
		goto err;
	}

	LOG_INF("Gain, FFT, IFFT and gain computed on HiFi4");

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&gain,
			    (struct mpipe_element *)&fft, (struct mpipe_element *)&ifft,
			    (struct mpipe_element *)&out_gain, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to add the HiFi4 stages (%d)", ret);
		goto err;
	}

	ret = mpipe_element_link((struct mpipe_element *)&queue, (struct mpipe_element *)&gain,
				 (struct mpipe_element *)&fft, (struct mpipe_element *)&ifft,
				 (struct mpipe_element *)&out_gain,
				 (struct mpipe_element *)&i2s_sink, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to link the HiFi4 stages (%d)", ret);
		goto err;
	}

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to init player (%d)", ret);
		goto err;
	}

	LOG_INF("Loopback started");

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	return 0;

err:
	LOG_ERR("Aborting sample (%d)", ret);

	return 0;
}
