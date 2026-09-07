/**
 * Copyright (c) 2019 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Drones SAS Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "vscale_test.h"

#define PUMP_TIMEOUT_MS 100
#define PUMP_MAX_ITER   150

#define W_SRC 64
#define H_SRC 64
#define W_DST 128
#define H_DST 128

/* I420 64x64: Y=4096, U=1024, V=1024 */
#define I420_Y_SIZE(w, h)  ((size_t)(w) * (h))
#define I420_UV_SIZE(w, h) ((size_t)((w) / 2) * ((h) / 2))
#define I420_FRAME_SIZE(w, h) \
	(I420_Y_SIZE(w, h) + 2 * I420_UV_SIZE(w, h))

/* NV12/NV21 64x64: Y=4096, UV=2048 */
#define NV_Y_SIZE(w, h)  ((size_t)(w) * (h))
#define NV_UV_SIZE(w, h) ((size_t)(w) * ((h) / 2))
#define NV_FRAME_SIZE(w, h) (NV_Y_SIZE(w, h) + NV_UV_SIZE(w, h))


static void pump_until(struct pomp_loop *loop, bool *flag)
{
	for (int i = 0; i < PUMP_MAX_ITER && !*flag; i++)
		pomp_loop_wait_and_process(loop, PUMP_TIMEOUT_MS);
}


static int push_i420_frame(struct mbuf_raw_video_frame_queue *queue,
			   unsigned int index,
			   uint32_t w,
			   uint32_t h)
{
	int ret;
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	size_t y_size = I420_Y_SIZE(w, h);
	size_t uv_size = I420_UV_SIZE(w, h);
	size_t frame_size = y_size + 2 * uv_size;

	ret = mbuf_mem_generic_new(frame_size, &mem);
	if (ret != 0)
		return ret;

	memset(&frame_info, 0, sizeof(frame_info));
	frame_info.format = vdef_i420;
	frame_info.info.resolution.width = w;
	frame_info.info.resolution.height = h;
	frame_info.info.timescale = 1000000;
	frame_info.info.timestamp = (uint64_t)index * 33333;
	frame_info.info.index = index;
	frame_info.plane_stride[0] = w;
	frame_info.plane_stride[1] = w / 2;
	frame_info.plane_stride[2] = w / 2;

	ret = mbuf_raw_video_frame_new(&frame_info, &frame);
	if (ret != 0)
		goto out;

	mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, y_size);
	mbuf_raw_video_frame_set_plane(frame, 1, mem, y_size, uv_size);
	mbuf_raw_video_frame_set_plane(frame, 2, mem, y_size + uv_size, uv_size);
	mbuf_raw_video_frame_finalize(frame);

	ret = mbuf_raw_video_frame_queue_push(queue, frame);

out:
	if (frame)
		mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
	return ret;
}


static int push_nv_frame(struct mbuf_raw_video_frame_queue *queue,
			 unsigned int index,
			 uint32_t w,
			 uint32_t h,
			 struct vdef_raw_format format)
{
	int ret;
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	size_t y_size = NV_Y_SIZE(w, h);
	size_t uv_size = NV_UV_SIZE(w, h);
	size_t frame_size = y_size + uv_size;

	ret = mbuf_mem_generic_new(frame_size, &mem);
	if (ret != 0)
		return ret;

	memset(&frame_info, 0, sizeof(frame_info));
	frame_info.format = format;
	frame_info.info.resolution.width = w;
	frame_info.info.resolution.height = h;
	frame_info.info.timescale = 1000000;
	frame_info.info.timestamp = (uint64_t)index * 33333;
	frame_info.info.index = index;
	frame_info.plane_stride[0] = w;
	frame_info.plane_stride[1] = w;

	ret = mbuf_raw_video_frame_new(&frame_info, &frame);
	if (ret != 0)
		goto out;

	mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, y_size);
	mbuf_raw_video_frame_set_plane(frame, 1, mem, y_size, uv_size);
	mbuf_raw_video_frame_finalize(frame);

	ret = mbuf_raw_video_frame_queue_push(queue, frame);

out:
	if (frame)
		mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
	return ret;
}


static int push_rgb_frame(struct mbuf_raw_video_frame_queue *queue,
			  unsigned int index,
			  uint32_t w,
			  uint32_t h)
{
	int ret;
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	size_t frame_size = (size_t)w * h * 3;

	ret = mbuf_mem_generic_new(frame_size, &mem);
	if (ret != 0)
		return ret;

	memset(&frame_info, 0, sizeof(frame_info));
	frame_info.format = vdef_rgb;
	frame_info.info.resolution.width = w;
	frame_info.info.resolution.height = h;
	frame_info.info.timescale = 1000000;
	frame_info.info.timestamp = (uint64_t)index * 33333;
	frame_info.info.index = index;
	frame_info.plane_stride[0] = w * 3;

	ret = mbuf_raw_video_frame_new(&frame_info, &frame);
	if (ret != 0)
		goto out;

	mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, frame_size);
	mbuf_raw_video_frame_finalize(frame);

	ret = mbuf_raw_video_frame_queue_push(queue, frame);

out:
	if (frame)
		mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
	return ret;
}


static void assert_ancillary_key_present(struct mbuf_raw_video_frame *frame,
					 const char *key)
{
	int res;
	struct mbuf_ancillary_data *anc = NULL;
	const void *buf = NULL;
	size_t len = 0;

	res = mbuf_raw_video_frame_get_ancillary_data(frame, key, &anc);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL(anc);
	if (!anc)
		return;

	buf = mbuf_ancillary_data_get_buffer(anc, &len);
	CU_ASSERT_PTR_NOT_NULL(buf);
	CU_ASSERT_EQUAL(len, sizeof(uint64_t));
	if (buf)
		CU_ASSERT_TRUE(*(const uint64_t *)buf > 0);

	mbuf_ancillary_data_unref(anc);
}


static void fill_config(struct vscale_config *config,
			struct vdef_raw_format in_fmt,
			uint32_t in_w,
			uint32_t in_h,
			struct vdef_raw_format out_fmt,
			uint32_t out_w,
			uint32_t out_h)
{
	memset(config, 0, sizeof(*config));
	config->implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config->input.format = in_fmt;
	config->input.info.resolution.width = in_w;
	config->input.info.resolution.height = in_h;
	config->output.preferred_format = out_fmt;
	config->output.info.resolution.width = out_w;
	config->output.info.resolution.height = out_h;
}


/* ---- drain tests ---- */

static void test_drain_i420_passthrough(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);
	for (int i = 0; i < ctx.frame_count; i++)
		CU_ASSERT_EQUAL(ctx.status[i], 0);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_EQUAL(out_info.info.resolution.width, (uint32_t)W_SRC);
		CU_ASSERT_EQUAL(out_info.info.resolution.height,
				(uint32_t)H_SRC);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_i420_downscale(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_DST, H_DST, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_DST, H_DST), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_EQUAL(out_info.info.resolution.width, (uint32_t)W_SRC);
		CU_ASSERT_EQUAL(out_info.info.resolution.height,
				(uint32_t)H_SRC);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_i420_upscale(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_DST, H_DST);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_EQUAL(out_info.info.resolution.width, (uint32_t)W_DST);
		CU_ASSERT_EQUAL(out_info.info.resolution.height,
				(uint32_t)H_DST);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_nv12(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_nv12, W_SRC, H_SRC, vdef_nv12, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(
			push_nv_frame(queue, i, W_SRC, H_SRC, vdef_nv12), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_nv21(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_nv21, W_SRC, H_SRC, vdef_nv21, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(
			push_nv_frame(queue, i, W_SRC, H_SRC, vdef_nv21), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* I420 upscale + RGB conversion: exercises the intermediate buffer path
 * (scaling_needed=true) that is dead when input/output dimensions match. */
static void test_drain_i420_to_rgb_with_scale(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	/* 64x64 I420 → 128x128 RGB: forces scaling into intermediate YUV buffer
	 * before RGB conversion (lines 568-592, 618-627 of vscale_libyuv.c). */
	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_rgb, W_DST, H_DST);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_TRUE(
			vdef_raw_format_cmp(&out_info.format, &vdef_rgb));
		CU_ASSERT_EQUAL(out_info.info.resolution.width, (uint32_t)W_DST);
		CU_ASSERT_EQUAL(out_info.info.resolution.height, (uint32_t)H_DST);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_nv12_to_rgb(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_nv12, W_SRC, H_SRC, vdef_rgb, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(
			push_nv_frame(queue, i, W_SRC, H_SRC, vdef_nv12), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_TRUE(
			vdef_raw_format_cmp(&out_info.format, &vdef_rgb));
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_nv21_to_rgb(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_nv21, W_SRC, H_SRC, vdef_rgb, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(
			push_nv_frame(queue, i, W_SRC, H_SRC, vdef_nv21), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_TRUE(
			vdef_raw_format_cmp(&out_info.format, &vdef_rgb));
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_drain_i420_to_rgb(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct vdef_raw_frame out_info;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_rgb, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		mbuf_raw_video_frame_get_frame_info(ctx.captured[0], &out_info);
		CU_ASSERT_TRUE(
			vdef_raw_format_cmp(&out_info.format, &vdef_rgb));
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- ancillary keys test ---- */

static void test_ancillary_keys_present(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	CU_ASSERT_EQUAL(push_i420_frame(queue, 0, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 1);

	if (ctx.frame_count > 0 && ctx.captured[0]) {
		assert_ancillary_key_present(ctx.captured[0],
					     VSCALE_ANCILLARY_KEY_INPUT_TIME);
		assert_ancillary_key_present(ctx.captured[0],
					     VSCALE_ANCILLARY_KEY_DEQUEUE_TIME);
		assert_ancillary_key_present(ctx.captured[0],
					     VSCALE_ANCILLARY_KEY_OUTPUT_TIME);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- flush discard tests ---- */

static void test_flush_discard_i420(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, true), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_flush_discard_nv12(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_nv12, W_SRC, H_SRC, vdef_nv12, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(
			push_nv_frame(queue, i, W_SRC, H_SRC, vdef_nv12), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, true), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- rescale after flush ---- */

static void test_rescale_after_flush(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	/* First batch */
	for (unsigned int i = 0; i < 3; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);
	mock_vscale_cbs_ctx_clear(&ctx);

	/* Second batch: start timestamps after last accepted (index=3..5) */
	for (unsigned int i = 3; i < 6; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 3);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- metadata copy test ---- */

static void attach_test_metadata(struct mbuf_raw_video_frame *frame)
{
	struct vmeta_frame *meta = NULL;
	if (vmeta_frame_new(VMETA_FRAME_TYPE_V3, &meta) < 0)
		return;
	mbuf_raw_video_frame_set_metadata(frame, meta);
	vmeta_frame_unref(meta);
}


static void test_drain_with_metadata(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	struct vmeta_frame *out_meta = NULL;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	/* Build one I420 frame with vmeta attached */
	size_t y_size = I420_Y_SIZE(W_SRC, H_SRC);
	size_t uv_size = I420_UV_SIZE(W_SRC, H_SRC);
	mbuf_mem_generic_new(y_size + 2 * uv_size, &mem);
	CU_ASSERT_PTR_NOT_NULL_FATAL(mem);

	memset(&frame_info, 0, sizeof(frame_info));
	frame_info.format = vdef_i420;
	frame_info.info.resolution.width = W_SRC;
	frame_info.info.resolution.height = H_SRC;
	frame_info.info.timescale = 1000000;
	frame_info.info.timestamp = 33333;
	frame_info.plane_stride[0] = W_SRC;
	frame_info.plane_stride[1] = W_SRC / 2;
	frame_info.plane_stride[2] = W_SRC / 2;

	mbuf_raw_video_frame_new(&frame_info, &frame);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);
	mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, y_size);
	mbuf_raw_video_frame_set_plane(frame, 1, mem, y_size, uv_size);
	mbuf_raw_video_frame_set_plane(frame, 2, mem, y_size + uv_size, uv_size);
	mbuf_raw_video_frame_finalize(frame);
	attach_test_metadata(frame);

	CU_ASSERT_EQUAL(mbuf_raw_video_frame_queue_push(queue, frame), 0);
	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);

	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 1);

	/* Verify metadata was copied to the output frame */
	if (ctx.frame_count > 0 && ctx.captured[0]) {
		int res = mbuf_raw_video_frame_get_metadata(
			ctx.captured[0], &out_meta);
		CU_ASSERT_EQUAL(res, 0);
		CU_ASSERT_PTR_NOT_NULL(out_meta);
		if (out_meta)
			vmeta_frame_unref(out_meta);
	}

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- input filter rejection tests ---- */

static void test_filter_rejects_unsupported_format(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	int push_ret;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	/* RGB is not in libyuv's supported input format list → -EPROTO */
	push_ret = push_rgb_frame(queue, 0, W_SRC, H_SRC);
	CU_ASSERT_EQUAL(push_ret, -EPROTO);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 0);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_filter_rejects_wrong_resolution(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;
	int push_ret;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	/* Frame is 128x128 but scaler configured for 64x64 → -EPROTO */
	push_ret = push_i420_frame(queue, 0, W_DST, H_DST);
	CU_ASSERT_EQUAL(push_ret, -EPROTO);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 0);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_filter_rejects_non_monotonic(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.flush = mock_vscale_flush_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	/* index=1 → ts=33333 (accepted) */
	CU_ASSERT_EQUAL(push_i420_frame(queue, 1, W_SRC, H_SRC), 0);
	/* index=1 again → ts=33333 (equal, rejected) */
	CU_ASSERT_EQUAL(push_i420_frame(queue, 1, W_SRC, H_SRC), -EPROTO);
	/* index=0 → ts=0 < 33333 (earlier, rejected) */
	CU_ASSERT_EQUAL(push_i420_frame(queue, 0, W_SRC, H_SRC), -EPROTO);

	CU_ASSERT_EQUAL(vscale_flush(scaler, false), 0);
	pump_until(loop, &ctx.flush_done);
	CU_ASSERT_TRUE(ctx.flush_done);
	CU_ASSERT_EQUAL(ctx.frame_count, 1);

	mock_vscale_cbs_ctx_clear(&ctx);
	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- stop without flush ---- */

static void test_stop_without_flush(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	struct mbuf_raw_video_frame_queue *queue;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	queue = vscale_get_input_buffer_queue(scaler);
	CU_ASSERT_PTR_NOT_NULL_FATAL(queue);

	for (unsigned int i = 0; i < 2; i++)
		CU_ASSERT_EQUAL(push_i420_frame(queue, i, W_SRC, H_SRC), 0);

	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	CU_ASSERT_TRUE(ctx.stop_done);

	mock_vscale_cbs_ctx_clear(&ctx);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


/* ---- API accessor tests ---- */

static void test_get_input_buffer_pool_null(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	CU_ASSERT_PTR_NULL(vscale_get_input_buffer_pool(scaler));

	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


static void test_get_input_buffer_queue_non_null(void)
{
	struct pomp_loop *loop;
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;

	if (!libyuv_available())
		return;

	loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	mock_vscale_cbs_ctx_reset(&ctx);

	fill_config(&config, vdef_i420, W_SRC, H_SRC, vdef_i420, W_SRC, H_SRC);
	CU_ASSERT_EQUAL_FATAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);

	CU_ASSERT_PTR_NOT_NULL(vscale_get_input_buffer_queue(scaler));

	CU_ASSERT_EQUAL(vscale_stop(scaler), 0);
	pump_until(loop, &ctx.stop_done);
	vscale_destroy(scaler);
	pomp_loop_destroy(loop);
}


CU_TestInfo g_vscale_test_lifecycle[] = {
	{FN("drain-i420-passthrough"), &test_drain_i420_passthrough},
	{FN("drain-i420-downscale"), &test_drain_i420_downscale},
	{FN("drain-i420-upscale"), &test_drain_i420_upscale},
	{FN("drain-nv12"), &test_drain_nv12},
	{FN("drain-nv21"), &test_drain_nv21},
	{FN("drain-nv12-to-rgb"), &test_drain_nv12_to_rgb},
	{FN("drain-nv21-to-rgb"), &test_drain_nv21_to_rgb},
	{FN("drain-i420-to-rgb"), &test_drain_i420_to_rgb},
	{FN("drain-i420-to-rgb-with-scale"), &test_drain_i420_to_rgb_with_scale},
	{FN("ancillary-keys-present"), &test_ancillary_keys_present},
	{FN("flush-discard-i420"), &test_flush_discard_i420},
	{FN("flush-discard-nv12"), &test_flush_discard_nv12},
	{FN("rescale-after-flush"), &test_rescale_after_flush},
	{FN("drain-with-metadata"), &test_drain_with_metadata},
	{FN("filter-rejects-unsupported-format"),
	 &test_filter_rejects_unsupported_format},
	{FN("filter-rejects-wrong-resolution"),
	 &test_filter_rejects_wrong_resolution},
	{FN("filter-rejects-non-monotonic"), &test_filter_rejects_non_monotonic},
	{FN("stop-without-flush"), &test_stop_without_flush},
	{FN("get-input-buffer-pool-null"), &test_get_input_buffer_pool_null},
	{FN("get-input-buffer-queue-non-null"),
	 &test_get_input_buffer_queue_non_null},
	CU_TEST_INFO_NULL,
};
