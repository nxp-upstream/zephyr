/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Pipeline server: serve a pipeline to a client element on another core.
 * @ingroup mpipe_pipeline_server
 */

#ifndef ZEPHYR_INCLUDE_MPIPE_UTILS_MPIPE_PIPELINE_SERVER_H_
#define ZEPHYR_INCLUDE_MPIPE_UTILS_MPIPE_PIPELINE_SERVER_H_

/**
 * @defgroup mpipe_pipeline_server Pipeline server
 * @ingroup mpipe
 * @brief Serve a pipeline to a client element running on another core.
 *
 * A client element (@ref mpipe_transform_client, @ref mpipe_sink_client)
 * stands in one pipeline for work done on another core. When that core runs
 * this framework too, the work is an ordinary pipeline there, and the server
 * is what answers the client's calls with it:
 *
 * @code
 * core 1:  ... -> queue -> transform client -> ...
 *                               |  calls
 * core 2:               pipeline server
 *                               |  drives
 *          app_src -> element -> element -> app_sink
 * @endcode
 *
 * The server is not an element and sits in no pipeline. The application
 * builds the pipeline as usual, with an @ref mpipe_app_src where the client's
 * buffers come in and, for a transform client, an @ref mpipe_app_sink where
 * the results leave; a pipeline serving a sink client ends in a sink of its
 * own. Whatever lies between the two is served as one unit: the client never
 * sees the elements, and a buffer crosses between the cores once however many
 * elements process it.
 *
 * Each function below is the server side of one client hook. They work on
 * capabilities and addresses and know no media type and no transport: a
 * per-media adapter translates the capabilities for the wire, and a transport
 * binding receives the calls and invokes them.
 *
 * - Capabilities: the client's pads enumerate the pipeline's outer pads, and
 *   its transformation of a capability is answered by negotiating the served
 *   pipeline with that capability at the entry.
 * - Negotiation: the pipeline goes from READY to PAUSED when the client has
 *   applied its formats (@ref mpipe_pipeline_server_set_caps), never earlier,
 *   since that is when they are known.
 * - A pipeline with a source of its own streams on its own thread, and
 *   @ref mpipe_pipeline_server_produce hands its buffers to the client.
 * - Data: @ref mpipe_pipeline_server_process wraps the client's memory in
 *   buffers and walks the input through the pipeline in the caller's thread.
 *   The last element writes into the client's output memory when the elements
 *   adopt the pool the app_sink proposes; the result is copied there
 *   otherwise.
 *
 * The server drives the pipeline alone and is not reentrant: all the calls
 * for one server come from one thread, which is the streaming thread of the
 * served pipeline. The pipeline needs no queue element for that reason, and
 * one would break the synchronous contract of the processing call.
 *
 * The client's buffers belong to another core: the server invalidates this
 * core's data cache over the input before the pipeline reads it and flushes
 * it over the output afterwards.
 *
 * @{
 */

#include <stdint.h>

#include <zephyr/net_buf.h>

#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_element.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_pipeline.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>

/**
 * @brief Pipeline server
 */
struct mpipe_pipeline_server {
	/** The served pipeline */
	struct mpipe *pipeline;
	/** Where the client's buffers enter the pipeline, NULL when it has a source of its own */
	struct mpipe_app_src *in;
	/** Where the results leave it, NULL when the pipeline ends in a sink of its own */
	struct mpipe_app_sink *out;
	/** Owner of the buffers wrapping the client's input memory */
	struct mpipe_buffer_pool in_pool;
	/** Pool handing out the client's output memory, proposed by the app_sink */
	struct mpipe_buffer_pool out_pool;
	/** The client's output memory for the call in progress, until an element takes it */
	struct net_buf *out_buf;
	/** Whether the pipeline negotiated since it last was READY */
	bool negotiated;
};

/**
 * @brief Initialize a pipeline server
 *
 * The pipeline is built and linked, and in the READY state. @p in and @p out
 * are elements of it, and say what the pipeline is served as:
 * - a transform, with both: each input of the client gives one output;
 * - a sink, with @p in only: the pipeline consumes the inputs of the client;
 * - a source, with @p out only: the pipeline starts with a source of its own,
 *   streams on its own thread, and the client takes what reaches @p out with
 *   mpipe_pipeline_server_produce().
 *
 * @param server Server to initialize.
 * @param pipeline The pipeline to serve.
 * @param in Its application source, or NULL when it has a source of its own.
 * @param out Its application sink, or NULL when it ends in a sink of its own.
 *
 * @retval 0 Success.
 * @retval -ENOTCONN @p in or @p out is not linked
 */
int mpipe_pipeline_server_init(struct mpipe_pipeline_server *server, struct mpipe *pipeline,
			       struct mpipe_app_src *in, struct mpipe_app_sink *out);

/**
 * @brief Produce one capability of the pipeline's entry or exit
 *
 * The entry is the pad the application source is linked to and the exit the
 * pad linked to the application sink. Indices are dense: the capabilities are
 * numbered from 0 without gaps.
 *
 * @param server The server.
 * @param direction MPIPE_PAD_SINK for the entry, MPIPE_PAD_SRC for the exit.
 * @param index Zero-based index of the capability.
 * @param[out] caps Storage for the capability.
 *
 * @retval 0 Success.
 * @retval -ENOENT Past the last capability
 * @retval -ENOTSUP The exit of a pipeline that has no application sink
 * @return Any other negative errno of the pad's enumeration
 */
int mpipe_pipeline_server_enum_caps(struct mpipe_pipeline_server *server,
				    enum mpipe_pad_direction direction, uint32_t index,
				    struct mpipe_structure *caps);

/**
 * @brief Cross a capability over the pipeline
 *
 * What the pipeline makes of @p in, as the transformation function of an
 * element would say it. With MPIPE_PAD_SRC, @p in is a capability at the
 * entry and the answers are the capabilities the pipeline can deliver at the
 * exit for it, one per index. With MPIPE_PAD_SINK, @p in is a capability at
 * the exit and the answers are the entry capabilities that lead to it.
 *
 * Each answer comes from a capability query through the pipeline, so it
 * accounts for every element in it. The pipeline must not be streaming: it is
 * taken back to READY first if it is not there.
 *
 * @param server The server.
 * @param direction The side to produce capabilities for.
 * @param index Zero-based index of the answer.
 * @param in The capability to cross.
 * @param[out] out Storage for the answer.
 *
 * @retval 0 Success.
 * @retval -ENOENT Past the last answer
 * @retval -ENOTSUP The pipeline has no application sink
 */
int mpipe_pipeline_server_transform_caps(struct mpipe_pipeline_server *server,
					 enum mpipe_pad_direction direction, uint32_t index,
					 const struct mpipe_structure *in,
					 struct mpipe_structure *out);

/**
 * @brief Apply the client's format at the entry or at the exit
 *
 * Once the formats are known, the entry one for a pipeline ending in its own
 * sink and the exit one otherwise, the pipeline negotiates and goes to PAUSED:
 * it is then ready for @ref mpipe_pipeline_server_process.
 *
 * @param server The server.
 * @param direction MPIPE_PAD_SINK for the entry, MPIPE_PAD_SRC for the exit.
 * @param caps The fixed capability the client settled on.
 *
 * @retval 0 Success.
 * @retval -EINVAL @p caps is not fixed
 * @retval -ENOTSUP The exit of a pipeline that has no application sink
 * @return The errno of the pipeline's failed state change
 */
int mpipe_pipeline_server_set_caps(struct mpipe_pipeline_server *server,
				   enum mpipe_pad_direction direction,
				   const struct mpipe_structure *caps);

/**
 * @brief What the client's buffers must satisfy
 *
 * One buffer in each direction is in use during a call. The alignment is
 * this core's data cache line, so that the cache maintenance of a buffer
 * touches nothing else; the size is the client's to derive from the format.
 *
 * @param server The server.
 * @param direction MPIPE_PAD_SINK for the input buffers, MPIPE_PAD_SRC for the output ones.
 * @param[out] cfg Storage for the requirement.
 *
 * @retval 0 Success.
 */
int mpipe_pipeline_server_get_pool_config(struct mpipe_pipeline_server *server,
					  enum mpipe_pad_direction direction,
					  struct mpipe_buffer_pool_config *cfg);

/**
 * @brief Run one input through the pipeline
 *
 * Returns when the pipeline is done with the input: the result, if the
 * pipeline has an application sink, is in @p out, and a pipeline ending in
 * its own sink has consumed the input.
 *
 * @param server The server.
 * @param in The client's input memory.
 * @param in_size Number of valid bytes in it.
 * @param out The client's output memory, NULL for a pipeline with no application sink.
 * @param out_capacity Size of the output memory in bytes.
 * @param[out] out_size Number of bytes written to @p out, 0 when the pipeline
 *                      produced nothing for this input. May be NULL when @p out is.
 *
 * @retval 0 Success.
 * @retval -EINVAL A NULL or empty input, or no output memory for a pipeline that needs it
 * @retval -ENOTSUP The pipeline has a source of its own
 * @retval -ENOTCONN The pipeline has not negotiated
 * @retval -ENOBUFS No buffer header was free
 * @retval -EMSGSIZE The result does not fit in @p out
 * @return The errno of the element that failed
 */
int mpipe_pipeline_server_process(struct mpipe_pipeline_server *server, void *in, uint32_t in_size,
				  void *out, uint32_t out_capacity, uint32_t *out_size);

/**
 * @brief Take the next buffer of a pipeline that has a source of its own
 *
 * Waits for a buffer to reach the application sink and copies it into the
 * client's memory. The wait is bounded so that the client can stop: a client
 * that gets -EAGAIN asks again.
 *
 * A buffer that reaches the application sink while its queue is full is
 * dropped: a client that asks too slowly loses buffers and does not hold the
 * source.
 *
 * @param server The server.
 * @param out The client's output memory.
 * @param out_capacity Size of the output memory in bytes.
 * @param[out] out_size Number of bytes written to @p out.
 * @param timeout How long to wait for a buffer.
 *
 * @retval 0 Success.
 * @retval -EAGAIN No buffer arrived within @p timeout
 * @retval -ENODATA The stream of the pipeline ended and every buffer was taken
 * @retval -EINVAL No output memory
 * @retval -ENOTSUP The pipeline takes its buffers from the client, or ends in a sink of its own
 * @retval -ENOTCONN The pipeline has not negotiated
 * @retval -EMSGSIZE The buffer does not fit in @p out
 */
int mpipe_pipeline_server_produce(struct mpipe_pipeline_server *server, void *out,
				  uint32_t out_capacity, uint32_t *out_size, k_timeout_t timeout);

/**
 * @brief Take the pipeline to a state
 *
 * The client's transitions other than READY to PAUSED end here. PAUSED and
 * PLAYING are refused until the pipeline has negotiated.
 *
 * @param server The server.
 * @param state The state to reach.
 *
 * @retval 0 Success.
 * @retval -ENOTCONN PAUSED or PLAYING asked of a pipeline that has not negotiated
 * @return The errno of the pipeline's failed state change
 */
int mpipe_pipeline_server_set_state(struct mpipe_pipeline_server *server, enum mpipe_state state);

/**
 * @brief End the stream
 *
 * Sends the end of stream through the pipeline and returns once its sinks
 * have seen it.
 *
 * @param server The server.
 *
 * @retval 0 Success, also when the pipeline never negotiated
 * @return The errno of the element that refused the event
 */
int mpipe_pipeline_server_eos(struct mpipe_pipeline_server *server);

/** @} */

#endif /* ZEPHYR_INCLUDE_MPIPE_UTILS_MPIPE_PIPELINE_SERVER_H_ */
