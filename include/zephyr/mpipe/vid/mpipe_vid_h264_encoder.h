/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_vid_h264_encoders
 * @brief Software H.264 encoder transform element.
 *
 * Encodes planar YUV 4:2:0 frames into an H.264 byte stream with the
 * minih264 module. Built with CONFIG_MPIPE_VID_H264_ENCODER.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_ENCODER_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_ENCODER_H_

/**
 * @defgroup mpipe_vid_h264_encoders H.264 encoders
 * @ingroup mpipe_vid
 * @brief H.264 encoder elements.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

/**
 * @brief H.264 encoder property identifiers
 */
enum {
	/**
	 * Quantizer, a const uint8_t pointer, 10 (best quality) to 51. The
	 * encoder uses it for every frame, so the bit rate follows the content.
	 */
	MPIPE_PROP_VID_H264_ENCODER_QP = MPIPE_PROP_TRANSFORM_LAST,
	/**
	 * Key frame period in frames, a const uint16_t pointer. 1 makes every
	 * frame a key frame; 0 produces one key frame at the start and then
	 * only on request. Applied when the format is next negotiated.
	 */
	MPIPE_PROP_VID_H264_ENCODER_GOP,
	/**
	 * Speed against quality, a const uint8_t pointer, 0 (every coding tool)
	 * to 10 (fastest). The default, 9, is the fastest setting that keeps
	 * the deblocking filter, without which the blocks of the picture show
	 * in flat areas.
	 */
	MPIPE_PROP_VID_H264_ENCODER_SPEED,
	/**
	 * Key frame request, a const bool pointer: true makes the next frame a
	 * key frame, with its parameter sets. May be set from any thread while
	 * the element is streaming, which is how a receiver that lost a frame
	 * asks for a new starting point.
	 */
	MPIPE_PROP_VID_H264_ENCODER_KEY_FRAME,
};

/**
 * @brief Software H.264 encoder element.
 *
 * Takes one YUV 4:2:0 frame (VIDEO_PIX_FMT_YUV420) per buffer on its sink pad
 * and produces one access unit per buffer on its source pad, as a byte stream
 * with start codes (VIDEO_PIX_FMT_H264): the slices of the frame, preceded by
 * the sequence and picture parameter sets on a key frame. The stream is
 * Constrained Baseline, with one reference frame and no frame reordering, so
 * every output buffer corresponds to the input buffer it was made from and
 * carries its timestamp.
 *
 * The frame size comes from the negotiated format; both dimensions must be
 * even. The encoder works in a static memory area sized by
 * @kconfig{CONFIG_MPIPE_VID_H264_ENCODER_PERSIST_SIZE} and
 * @kconfig{CONFIG_MPIPE_VID_H264_ENCODER_SCRATCH_SIZE}, which serves one
 * element at a time: the last one initialized.
 */
struct mpipe_vid_h264_encoder {
	/** Base transform element (must be first) */
	struct mpipe_transform transform;
	/** Internal output pool, used when downstream proposes none */
	struct mpipe_buffer_pool out_pool;
	/** Key frame requested for the next frame */
	atomic_t key_frame;
	/** Frame width in pixels */
	uint16_t width;
	/** Frame height in pixels */
	uint16_t height;
	/** Key frame period in frames */
	uint16_t gop;
	/** Quantizer */
	uint8_t qp;
	/** Speed against quality */
	uint8_t speed;
	/** The encoder is created for the negotiated frame size */
	bool ready;
};

/**
 * @brief Initialize a software H.264 encoder element.
 *
 * @param enc Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_h264_encoder_init(struct mpipe_vid_h264_encoder *enc, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_ENCODER_H_ */
