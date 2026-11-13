/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Video adapter of the pipeline server.
 * @ingroup mpipe_vid_pipeline_servers
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_PIPELINE_SERVER_H_
#define ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_PIPELINE_SERVER_H_

/**
 * @defgroup mpipe_vid_pipeline_servers Pipeline Servers
 * @ingroup mpipe_vid
 * @brief Serve a video pipeline to a video client element on another core.
 *
 * The server side of the hooks of @ref mpipe_vid_transform_client and
 * @ref mpipe_vid_sink_client, answered with a @ref mpipe_pipeline_server.
 * The clients speak the vocabulary of the video API, formats and format
 * capabilities; these functions translate it from and to the capabilities
 * the served pipeline negotiates with. They carry no transport: a transport
 * binding receives the client's calls and invokes them. The processing,
 * state and end-of-stream calls need no translation and go to the pipeline
 * server directly.
 *
 * @{
 */

#include <stdint.h>

#include <zephyr/drivers/video.h>

#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/utils/mpipe_pipeline_server.h>

/**
 * @brief What the client's buffers must satisfy
 *
 * @param server The pipeline server.
 * @param direction MPIPE_PAD_SINK for the input buffers, MPIPE_PAD_SRC for the output ones.
 * @param[out] min_buffers Number of buffers the server uses at once.
 * @param[out] buf_align Alignment of the buffers in bytes.
 *
 * @retval 0 Success.
 */
int mpipe_vid_pipeline_server_get_buf_caps(struct mpipe_pipeline_server *server,
					   enum mpipe_pad_direction direction, uint8_t *min_buffers,
					   uint16_t *buf_align);

/**
 * @brief One format capability of the pipeline's entry or exit
 *
 * A dimension the pipeline does not constrain is reported as the widest
 * range the format capability can express.
 *
 * @param server The pipeline server.
 * @param direction MPIPE_PAD_SINK for the entry, MPIPE_PAD_SRC for the exit.
 * @param ind Zero-based index of the capability.
 * @param[out] vfc Storage for the capability.
 *
 * @retval 0 Success.
 * @retval -ENOENT Past the last capability
 * @retval -ENOTSUP The pipeline states no pixel format there, or has no such side
 */
int mpipe_vid_pipeline_server_get_format_caps(struct mpipe_pipeline_server *server,
					      enum mpipe_pad_direction direction, uint8_t ind,
					      struct video_format_cap *vfc);

/**
 * @brief Apply the client's format
 *
 * The format's type says which side it is for, VIDEO_BUF_TYPE_INPUT for the
 * entry and VIDEO_BUF_TYPE_OUTPUT for the exit. The pipeline negotiates once
 * it has the formats it needs, see @ref mpipe_pipeline_server_set_caps. The
 * pitch and the buffer size the format implies are filled in; a compressed
 * format is given one byte per pixel.
 *
 * @param server The pipeline server.
 * @param[in,out] fmt The format to apply.
 *
 * @retval 0 Success.
 * @retval -EINVAL Unknown buffer type
 * @return Any negative errno of @ref mpipe_pipeline_server_set_caps
 */
int mpipe_vid_pipeline_server_set_format(struct mpipe_pipeline_server *server,
					 struct video_format *fmt);

/**
 * @brief Cross a format capability over the pipeline
 *
 * See @ref mpipe_pipeline_server_transform_caps.
 *
 * @param server The pipeline server.
 * @param direction The side to produce capabilities for.
 * @param ind Zero-based index of the answer.
 * @param vfc The capability to cross.
 * @param[out] other_vfc Storage for the answer.
 *
 * @retval 0 Success.
 * @retval -ENOENT Past the last answer
 * @retval -ENOTSUP The pipeline has no application sink
 */
int mpipe_vid_pipeline_server_transform_cap(struct mpipe_pipeline_server *server,
					    enum mpipe_pad_direction direction, uint16_t ind,
					    const struct video_format_cap *vfc,
					    struct video_format_cap *other_vfc);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_VID_MPIPE_VID_PIPELINE_SERVER_H_ */
