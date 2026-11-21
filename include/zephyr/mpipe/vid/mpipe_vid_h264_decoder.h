/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mpipe_vid_h264_decoders
 * @brief Software H.264 decoder transform element.
 *
 * Decodes an H.264 byte stream into planar YUV 4:2:0 frames with the
 * h264swdec module. Built with CONFIG_MPIPE_VID_H264_DECODER.
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_DECODER_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_DECODER_H_

/**
 * @defgroup mpipe_vid_h264_decoders H.264 decoders
 * @ingroup mpipe_vid
 * @brief H.264 decoder elements.
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

/** Longest sequence or picture parameter set the decoder keeps, start code included */
#define MPIPE_VID_H264_DECODER_PARAM_SET_SIZE 128

struct mpipe_vid_h264_decoder;

/**
 * @brief Key frame request of the decoder.
 *
 * Called from the streaming thread when the decoder cannot go on from the
 * frames it has, after a loss or a corrupted frame or when the stream does not
 * begin with a key frame, and discards the stream until the next key frame.
 * It is called again from time to time for as long as no key frame comes. The
 * application forwards the request to whoever produces the stream. It must
 * not block.
 *
 * @param dec The decoder element
 * @param user_data The pointer registered with the callback
 */
typedef void (*mpipe_vid_h264_decoder_key_frame_cb_t)(struct mpipe_vid_h264_decoder *dec,
						      void *user_data);

/**
 * @brief Callback registration for MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB
 */
struct mpipe_vid_h264_decoder_cb {
	/** Function to call when a key frame is needed */
	mpipe_vid_h264_decoder_key_frame_cb_t fn;
	/** Passed through to the callback */
	void *user_data;
};

/**
 * @brief H.264 decoder property identifiers
 */
enum {
	/** Key frame request callback, a const struct mpipe_vid_h264_decoder_cb pointer, copied */
	MPIPE_PROP_VID_H264_DECODER_KEY_FRAME_CB = MPIPE_PROP_TRANSFORM_LAST,
};

/**
 * @brief Software H.264 decoder element.
 *
 * Takes a byte stream with start codes (VIDEO_PIX_FMT_H264) on its sink pad,
 * each buffer holding one or more whole NAL units, and produces one YUV 4:2:0
 * frame (VIDEO_PIX_FMT_YUV420) per decoded picture on its source pad, with the
 * timestamp of the buffer that completed the picture. Pictures leave in
 * decoding order, which is the display order of the Baseline streams the
 * decoder supports.
 *
 * The frame size is part of the negotiated format, since the elements
 * downstream size their buffers from it; pictures of another size are
 * dropped.
 *
 * A stream error makes the decoder start again: it calls the key frame
 * request callback and discards everything up to the next key frame. The
 * parameter sets seen so far are kept and given again to the new decoder, so
 * a key frame that comes without them is enough. A stream that is joined in
 * its middle is handled the same way: the first frame that is not a key frame
 * makes the request.
 *
 * The decoder allocates its picture buffers from the heap of the h264swdec
 * module when the format is negotiated and after a stream error.
 */
struct mpipe_vid_h264_decoder {
	/** Base transform element (must be first) */
	struct mpipe_transform transform;
	/** Internal output pool, used when downstream proposes none */
	struct mpipe_buffer_pool out_pool;
	/** Key frame request callback */
	struct mpipe_vid_h264_decoder_cb key_frame_cb;
	/** Decoder instance of the h264swdec module */
	void *inst;
	/** Number of pictures given to the decoder */
	uint32_t pic_id;
	/** Negotiated frame width in pixels */
	uint16_t width;
	/** Negotiated frame height in pixels */
	uint16_t height;
	/** Size of the last sequence parameter set, 0 when none was seen */
	uint16_t sps_size;
	/** Size of the last picture parameter set, 0 when none was seen */
	uint16_t pps_size;
	/** Last sequence parameter set, with its start code */
	uint8_t sps[MPIPE_VID_H264_DECODER_PARAM_SET_SIZE];
	/** Last picture parameter set, with its start code */
	uint8_t pps[MPIPE_VID_H264_DECODER_PARAM_SET_SIZE];
	/** Slices to discard before the key frame is asked for, the first time or again */
	uint16_t slices_to_request;
	/** Everything is discarded until the next key frame */
	bool waiting_for_key_frame;
	/** The size mismatch of the current stream has been reported */
	bool size_reported;
};

/**
 * @brief Initialize a software H.264 decoder element.
 *
 * @param dec Pointer to the element to initialize.
 * @param id Unique element identifier.
 *
 * @return 0 on success, negative errno otherwise.
 */
int mpipe_vid_h264_decoder_init(struct mpipe_vid_h264_decoder *dec, uint8_t id);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_H264_DECODER_H_ */
