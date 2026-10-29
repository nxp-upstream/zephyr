/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Face detection with TensorFlow Lite Micro on the NXP Neutron NPU. The
 * model is the Neutron-compiled TFLite flatbuffer itself: its NeutronGraph
 * operator carries the microcode and weights.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <tensorflow/lite/micro/kernels/neutron/neutron.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include <model_tflite.h>

#include "inference.h"

LOG_MODULE_REGISTER(inference, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * The NPU reads the model (microcode and weights) in place and reads and
 * writes the tensors and its scratch buffer itself, so all of them live in a
 * region it reaches. inference_init() copies the model in from flash.
 */
#define NPU_SECTION Z_GENERIC_SECTION(CONFIG_CAM_FACE_DETECTION_NPU_REGION_NAME)

alignas(16) static uint8_t model_data[sizeof(g_model_tflite)] NPU_SECTION;
alignas(16) static uint8_t tensor_arena[CONFIG_CAM_FACE_DETECTION_ARENA_SIZE] NPU_SECTION;

static tflite::MicroInterpreter *interpreter;
static TfLiteTensor *input_tensor;
static TfLiteTensor *output_tensor;
static size_t output_count;

int inference_init(struct inference_input *input, size_t *num_outputs)
{
	__ASSERT_NO_MSG(input != NULL);
	__ASSERT_NO_MSG(num_outputs != NULL);

	memcpy(model_data, g_model_tflite, sizeof(model_data));
	memset(tensor_arena, 0, sizeof(tensor_arena));

	const tflite::Model *model = tflite::GetModel(model_data);

	if (model->version() != TFLITE_SCHEMA_VERSION) {
		LOG_ERR("Model schema version %u, expected %d", (unsigned int)model->version(),
			TFLITE_SCHEMA_VERSION);
		return -EINVAL;
	}

	/*
	 * The operators the Neutron converter leaves on the CPU besides the NPU
	 * graphs. A missing one fails AllocateTensors().
	 */
	static tflite::MicroMutableOpResolver<6> resolver;

	resolver.AddAdd();
	resolver.AddConcatenation();
	resolver.AddMul();
	resolver.AddSlice();
	resolver.AddSoftmax();
	resolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());

	static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena,
							   sizeof(tensor_arena));

	interpreter = &static_interpreter;

	/* Allocating the tensors has the Neutron kernel prepare the NPU model */
	if (interpreter->AllocateTensors() != kTfLiteOk) {
		LOG_ERR("AllocateTensors failed");
		interpreter = NULL;
		return -ENOMEM;
	}

	input_tensor = interpreter->input(0);
	output_tensor = interpreter->output(0);

	if (input_tensor == NULL || output_tensor == NULL) {
		interpreter = NULL;
		return -EINVAL;
	}

	if (input_tensor->type != kTfLiteInt8 || input_tensor->dims->size != 4 ||
	    input_tensor->dims->data[0] != 1 ||
	    (input_tensor->dims->data[3] != 1 && input_tensor->dims->data[3] != 3)) {
		LOG_ERR("Input must be int8 [1, H, W, 1 or 3]");
		interpreter = NULL;
		return -EINVAL;
	}

	if (output_tensor->dims->size < 2 || output_tensor->dims->data[0] != 1) {
		LOG_ERR("Output must be [1, ...]");
		interpreter = NULL;
		return -EINVAL;
	}

	input->data = input_tensor->data.int8;
	input->height = (uint16_t)input_tensor->dims->data[1];
	input->width = (uint16_t)input_tensor->dims->data[2];
	input->channels = (uint16_t)input_tensor->dims->data[3];
	input->scale = input_tensor->params.scale;
	input->zero_point = input_tensor->params.zero_point;

	/* Every output value after the batch dimension, in row-major order */
	size_t count = 1U;

	for (int i = 1; i < output_tensor->dims->size; i++) {
		count *= (size_t)output_tensor->dims->data[i];
	}
	output_count = count;
	*num_outputs = count;

	LOG_INF("TFLM model %u bytes, input %ux%ux%u, %u outputs, arena %u of %u bytes used",
		(unsigned int)sizeof(model_data), input->width, input->height, input->channels,
		(unsigned int)*num_outputs, (unsigned int)interpreter->arena_used_bytes(),
		(unsigned int)sizeof(tensor_arena));

	return 0;
}

int inference_run(float *scores, size_t num_scores, uint32_t *usec)
{
	__ASSERT_NO_MSG(interpreter != NULL);
	__ASSERT_NO_MSG(scores != NULL);
	__ASSERT_NO_MSG(usec != NULL);

	uint32_t start = k_cycle_get_32();
	TfLiteStatus status = interpreter->Invoke();

	*usec = k_cyc_to_us_floor32(k_cycle_get_32() - start);

	if (status != kTfLiteOk) {
		LOG_ERR("Invoke failed (%d)", (int)status);
		return -EIO;
	}

	size_t n = MIN(num_scores, output_count);

	for (size_t i = 0; i < n; i++) {
		switch (output_tensor->type) {
		case kTfLiteFloat32:
			scores[i] = output_tensor->data.f[i];
			break;
		case kTfLiteInt8:
			scores[i] = ((int)output_tensor->data.int8[i] -
				     output_tensor->params.zero_point) *
				    output_tensor->params.scale;
			break;
		case kTfLiteUInt8:
			scores[i] = ((int)output_tensor->data.uint8[i] -
				     output_tensor->params.zero_point) *
				    output_tensor->params.scale;
			break;
		default:
			return -ENOTSUP;
		}
	}

	return 0;
}
