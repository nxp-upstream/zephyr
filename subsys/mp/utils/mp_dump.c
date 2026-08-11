/*
 * Copyright 2026 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/video/formats.h>

#include <zephyr/mp/mp_bin.h>
#include <zephyr/mp/mp_element.h>
#include <zephyr/mp/mp_object.h>
#include <zephyr/mp/mp_pad.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_value.h>
#include <zephyr/mp/utils/mp_dump.h>

/**
 * Longest line held before it is written out; a longer line is split, and a
 * mid-line split lets an active shell inject its prompt into the graph, so
 * this fits a node line whose ports carry caps.
 */
#define DUMP_LINE_MAX 256

/**
 * A sink with one line of output held in front of it. Writing fragment by
 * fragment floods the deferred log buffer behind printk() and drops lines;
 * holding a line brings a graph down to a dozen writes.
 */
struct mp_dump_writer {
	/** Where a completed line is written */
	const struct mp_dump_sink *sink;
	/** Number of characters held in @ref line */
	size_t len;
	/** The line being built, not NUL-terminated until it is written */
	char line[DUMP_LINE_MAX];
};

/**
 * State of one dump. Elements are indexed up front: a node's DOT name is its
 * position in this array, and an edge names its peer's node by it.
 */
struct mp_dump_ctx {
	/** Where the rendering is written */
	struct mp_dump_writer writer;
	/** Every element the dumped bin holds, nested bins included */
	struct mp_element *elements[CONFIG_MP_DUMP_MAX_ELEMENTS];
	/** Number of slots in use at the front of @ref elements */
	int num_elements;
	/** True once the bin held more elements than @ref elements can index */
	bool truncated;
};

/*
 * Names for the caps vocabulary, indexed by the identifier itself; the
 * assertions fail the build if an identifier lacks a name.
 */
/* clang-format off */
static const char *const dump_field_names[] = {
	[MP_CAPS_PIXEL_FORMAT] = "format",
	[MP_CAPS_IMAGE_WIDTH] = "width",
	[MP_CAPS_IMAGE_HEIGHT] = "height",
	[MP_CAPS_SAMPLE_RATE] = "rate",
	[MP_CAPS_BITWIDTH] = "bitwidth",
	[MP_CAPS_NUM_OF_CHANNEL] = "channels",
	[MP_CAPS_INTERLEAVED] = "interleaved",
	[MP_CAPS_FRAME_INTERVAL] = "frame-interval",
	[MP_CAPS_BUFFER_COUNT] = "buffers",
};
/* clang-format on */
BUILD_ASSERT(ARRAY_SIZE(dump_field_names) == MP_CAPS_END,
	     "A caps field identifier has no name in dump_field_names");

static const char *const dump_media_names[] = {
	[MP_MEDIA_UNKNOWN] = "unknown",
	[MP_MEDIA_AUDIO_PCM] = "audio/pcm",
	[MP_MEDIA_VIDEO] = "video",
};
BUILD_ASSERT(ARRAY_SIZE(dump_media_names) == MP_MEDIA_END,
	     "A media type has no name in dump_media_names");

static void __printf_like(2, 3) dump_emit(const struct mp_dump_sink *sink, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (sink == NULL) {
		vprintk(fmt, ap);
	} else {
		sink->vprint(sink->ctx, fmt, ap);
	}

	va_end(ap);
}

/* Write out whatever line has been built so far, if any */
static void dump_flush(struct mp_dump_writer *w)
{
	if (w->len == 0) {
		return;
	}

	w->line[w->len] = '\0';
	w->len = 0;

	dump_emit(w->sink, "%s", w->line);
}

/*
 * Append a fragment, writing the line out once complete. A fragment that does
 * not fit flushes the held line first, so an overlong line splits, not drops.
 */
static void __printf_like(2, 3) dump_print(struct mp_dump_writer *w, const char *fmt, ...)
{
	va_list ap;
	va_list retry;
	size_t room = sizeof(w->line) - w->len;
	int written;

	va_start(ap, fmt);
	va_copy(retry, ap);

	written = vsnprintk(&w->line[w->len], room, fmt, ap);
	if (written >= 0 && (size_t)written >= room) {
		w->line[w->len] = '\0';
		dump_flush(w);
		written = vsnprintk(w->line, sizeof(w->line), fmt, retry);
	}

	va_end(retry);
	va_end(ap);

	if (written < 0) {
		return;
	}

	w->len = MIN(w->len + (size_t)written, sizeof(w->line) - 1);

	if (memchr(w->line, '\n', w->len) != NULL) {
		dump_flush(w);
	}
}

const char *mp_dump_state_str(enum mp_state state)
{
	switch (state) {
	case MP_STATE_READY:
		return "READY";
	case MP_STATE_PAUSED:
		return "PAUSED";
	case MP_STATE_PLAYING:
		return "PLAYING";
	default:
		return "?";
	}
}

/* A NULL entry is a mid-enum hole the size assertions cannot see */
static const char *dump_name(const char *const *names, size_t count, uint8_t id)
{
	if (id >= count || names[id] == NULL) {
		return "?";
	}

	return names[id];
}

/* A pixel format is a fourcc, so show the four characters rather than a number */
static void dump_fourcc(struct mp_dump_writer *w, uint32_t fourcc)
{
	const char *str = VIDEO_FOURCC_TO_STR(fourcc);

	for (int i = 0; i < 4; i++) {
		if (isprint((unsigned char)str[i]) == 0) {
			dump_print(w, "0x%08x", fourcc);
			return;
		}
	}

	dump_print(w, "%s", str);
}

static void dump_value(struct mp_dump_writer *w, uint8_t field_id, const struct mp_value *value)
{
	switch (value->type) {
	case MP_TYPE_BOOLEAN:
		dump_print(w, "%s", value->v_boolean ? "true" : "false");
		break;
	case MP_TYPE_INT:
		dump_print(w, "%d", value->v_int);
		break;
	case MP_TYPE_UINT:
		if (field_id == MP_CAPS_PIXEL_FORMAT) {
			dump_fourcc(w, value->v_uint);
		} else {
			dump_print(w, "%u", value->v_uint);
		}
		break;
	case MP_TYPE_INT_RANGE:
		dump_print(w, "[%d, %d, %d]", value->range.min.v_int, value->range.max.v_int,
			   value->range.step.v_int);
		break;
	case MP_TYPE_UINT_RANGE:
		dump_print(w, "[%u, %u, %u]", value->range.min.v_uint, value->range.max.v_uint,
			   value->range.step.v_uint);
		break;
	default:
		dump_print(w, "?");
		break;
	}
}

/* The media name and field list, without the framing the context chooses */
static void dump_caps_fields(struct mp_dump_writer *w, const struct mp_structure *caps)
{
	uint8_t num_fields;

	dump_print(w, "%s", dump_name(dump_media_names, ARRAY_SIZE(dump_media_names),
				      caps->media_type_id));

	/* Nothing locks the pad against a negotiation running underneath us */
	num_fields = MIN(caps->num_fields, (uint8_t)CONFIG_MP_STRUCTURE_MAX_FIELDS);

	for (uint8_t i = 0; i < num_fields; i++) {
		dump_print(w, ", %s=", dump_name(dump_field_names, ARRAY_SIZE(dump_field_names),
						 caps->ids[i]));
		dump_value(w, caps->ids[i], &caps->values[i]);
	}
}

/* Render a capability inline: it joins the caller's line, not one of its own */
static void dump_caps(struct mp_dump_writer *w, const struct mp_structure *caps)
{
	/* Both constrain nothing, but ANY intersects with anything, empty with nothing */
	if (mp_structure_is_any(caps)) {
		dump_print(w, "<any>");
		return;
	}

	if (mp_structure_is_empty(caps)) {
		dump_print(w, "<empty>");
		return;
	}

	dump_print(w, "<");
	dump_caps_fields(w, caps);
	dump_print(w, ">");
}

int mp_dump_caps(const struct mp_structure *caps, const struct mp_dump_sink *sink)
{
	struct mp_dump_writer w = {.sink = sink};

	if (caps == NULL) {
		return -EINVAL;
	}

	dump_caps(&w, caps);
	dump_flush(&w);

	return 0;
}

/*
 * Render "mp_vid_src_init" as "vid_src #1": the name is the init function's,
 * so drop the "mp_" prefix and "_init" suffix.
 */
static void dump_element_name(struct mp_dump_ctx *ctx, struct mp_element *element)
{
	const char *name = element->name;
	size_t len;

	if (name == NULL) {
		dump_print(&ctx->writer, "element #%u", element->object.id);
		return;
	}

	if (strncmp(name, "mp_", 3) == 0) {
		name += 3;
	}

	len = strlen(name);
	if (len > 5U && strcmp(&name[len - 5U], "_init") == 0) {
		len -= 5U;
	}

	dump_print(&ctx->writer, "%.*s #%u", (int)len, name, element->object.id);
}

static int dump_element_index(struct mp_dump_ctx *ctx, struct mp_element *element)
{
	for (int i = 0; i < ctx->num_elements; i++) {
		if (ctx->elements[i] == element) {
			return i;
		}
	}

	return -1;
}

/*
 * Index every element the bin holds, descending into nested bins: a bin is a
 * container, not a node of the graph.
 */
static void dump_index_bin(struct mp_dump_ctx *ctx, struct mp_bin *bin)
{
	struct mp_object *obj;

	SYS_DLIST_FOR_EACH_CONTAINER(&bin->children, obj, node) {
		struct mp_element *element = (struct mp_element *)obj;

		if ((element->object.flags & MP_OBJECT_FLAG_BIN) != 0) {
			dump_index_bin(ctx, (struct mp_bin *)element);
			continue;
		}

		if (ctx->num_elements >= (int)ARRAY_SIZE(ctx->elements)) {
			ctx->truncated = true;
			return;
		}

		ctx->elements[ctx->num_elements] = element;
		ctx->num_elements++;
	}
}

/* Pads are record-label ports, so an edge can land on the pad it uses */
static void dump_dot_ports(struct mp_dump_ctx *ctx, sys_dlist_t *pads, const char *side)
{
	struct mp_object *obj;
	bool first = true;

	dump_print(&ctx->writer, "{");

	/* The port name is an identifier an edge refers to, the text is the label */
	SYS_DLIST_FOR_EACH_CONTAINER(pads, obj, node) {
		struct mp_pad *pad = (struct mp_pad *)obj;

		dump_print(&ctx->writer, "%s<%s%u> %s #%u", first ? "" : "|", side, obj->id, side,
			   obj->id);

		/*
		 * A linked pad's caps ride its edge; an unlinked pad has no edge, so
		 * its caps show here - in parentheses, a record label reserves <>.
		 */
		if (pad->peer == NULL && !mp_structure_is_any(&pad->caps)) {
			if (mp_structure_is_empty(&pad->caps)) {
				dump_print(&ctx->writer, "\\n(empty)");
			} else {
				dump_print(&ctx->writer, "\\n(");
				dump_caps_fields(&ctx->writer, &pad->caps);
				dump_print(&ctx->writer, ")");
			}
		}

		first = false;
	}

	dump_print(&ctx->writer, "}");
}

static const char *dump_dot_fill(enum mp_state state)
{
	switch (state) {
	case MP_STATE_PLAYING:
		return "#d7f0d7";
	case MP_STATE_PAUSED:
		return "#fdf3d0";
	default:
		return "#e6e6e6";
	}
}

/* True when any pad of the element has no peer */
static bool dump_has_unlinked_pad(struct mp_element *element)
{
	struct mp_object *obj;

	SYS_DLIST_FOR_EACH_CONTAINER(&element->sinkpads, obj, node) {
		if (((struct mp_pad *)obj)->peer == NULL) {
			return true;
		}
	}

	SYS_DLIST_FOR_EACH_CONTAINER(&element->srcpads, obj, node) {
		if (((struct mp_pad *)obj)->peer == NULL) {
			return true;
		}
	}

	return false;
}

static void dump_dot_nodes(struct mp_dump_ctx *ctx)
{
	for (int i = 0; i < ctx->num_elements; i++) {
		struct mp_element *element = ctx->elements[i];

		dump_print(&ctx->writer, "  e%d [", i);

		/* An unlinked pad marks the element rather than growing a dangling edge */
		if (dump_has_unlinked_pad(element)) {
			dump_print(&ctx->writer, "color=\"#cc0000\", penwidth=2, ");
		}

		dump_print(&ctx->writer, "fillcolor=\"%s\", label=\"{",
			   dump_dot_fill(element->current_state));

		if (!sys_dlist_is_empty(&element->sinkpads)) {
			dump_dot_ports(ctx, &element->sinkpads, "sink");
			dump_print(&ctx->writer, "|");
		}

		dump_element_name(ctx, element);
		dump_print(&ctx->writer, "\\n%s", mp_dump_state_str(element->current_state));

		if (!sys_dlist_is_empty(&element->srcpads)) {
			dump_print(&ctx->writer, "|");
			dump_dot_ports(ctx, &element->srcpads, "src");
		}

		dump_print(&ctx->writer, "}\"];\n");
	}
}

static void dump_dot_edges(struct mp_dump_ctx *ctx)
{
	for (int i = 0; i < ctx->num_elements; i++) {
		struct mp_object *obj;

		SYS_DLIST_FOR_EACH_CONTAINER(&ctx->elements[i]->srcpads, obj, node) {
			struct mp_pad *srcpad = (struct mp_pad *)obj;
			/* Read once: a concurrent relink may clear it between uses */
			struct mp_pad *peer = srcpad->peer;
			int peer_index;

			/* An unlinked pad has no edge to draw; the node carries the mark */
			if (peer == NULL) {
				continue;
			}

			peer_index = dump_element_index(
				ctx, (struct mp_element *)peer->object.container);
			if (peer_index < 0) {
				continue;
			}

			dump_print(&ctx->writer, "  e%d:src%u -> e%d:sink%u [label=\"", i, obj->id,
				   peer_index, peer->object.id);
			dump_caps(&ctx->writer, &srcpad->caps);
			dump_print(&ctx->writer, "\"];\n");
		}
	}
}

int mp_dump_bin(struct mp_bin *bin, const struct mp_dump_sink *sink)
{
	struct mp_dump_ctx ctx = {
		.writer = {.sink = sink},
	};
	struct mp_element *self = (struct mp_element *)bin;

	if (bin == NULL) {
		return -EINVAL;
	}

	dump_index_bin(&ctx, bin);

	dump_print(&ctx.writer, "digraph mp_pipeline {\n");
	dump_print(&ctx.writer, "  rankdir=LR;\n");
	dump_print(&ctx.writer, "  node [shape=record, style=filled, fontname=\"sans\"];\n");
	dump_print(&ctx.writer, "  label=\"");
	dump_element_name(&ctx, self);
	dump_print(&ctx.writer, " %s%s\";\n", mp_dump_state_str(self->current_state),
		   ctx.truncated ? " (truncated)" : "");

	dump_dot_nodes(&ctx);
	dump_dot_edges(&ctx);

	dump_print(&ctx.writer, "}\n");

	/* The rendering ends on a newline, so this only catches a truncated line */
	dump_flush(&ctx.writer);

	return 0;
}
