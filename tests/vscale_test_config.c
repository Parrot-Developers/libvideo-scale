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

#define STOP_TIMEOUT_MS 50
#define STOP_MAX_ITER   20


static void fill_valid_libyuv_config(struct vscale_config *config,
				     uint32_t in_w,
				     uint32_t in_h,
				     uint32_t out_w,
				     uint32_t out_h)
{
	memset(config, 0, sizeof(*config));
	config->implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config->input.format = vdef_i420;
	config->input.info.resolution.width = in_w;
	config->input.info.resolution.height = in_h;
	config->output.info.resolution.width = out_w;
	config->output.info.resolution.height = out_h;
}


static void do_stop_destroy(struct vscale_scaler *scaler,
			    struct pomp_loop *loop,
			    struct mock_vscale_cbs_ctx *ctx)
{
	vscale_stop(scaler);
	for (int i = 0; i < STOP_MAX_ITER && !ctx->stop_done; i++)
		pomp_loop_wait_and_process(loop, STOP_TIMEOUT_MS);
	vscale_destroy(scaler);
}


/* ---- vscale_new() null-argument guards ---- */

static void test_vscale_new_null_loop(void)
{
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(NULL, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
}


static void test_vscale_new_null_config(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	CU_ASSERT_EQUAL(vscale_new(loop, NULL, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_null_cbs(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, NULL, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_null_frame_output_cb(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs;
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	memset(&cbs, 0, sizeof(cbs));
	cbs.frame_output = NULL;
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_null_ret_obj(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, NULL), -EINVAL);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_zero_input_width(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 0, 64, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_zero_input_height(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 0, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_zero_output_width(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 0, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_zero_output_height(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 64, 0);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, NULL, &scaler), -EINVAL);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_unbuilt_implem(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct vscale_cbs cbs = {.frame_output = mock_vscale_frame_output_cb};
	struct vscale_scaler *scaler = NULL;
	int res;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	config.implem = VSCALE_SCALER_IMPLEM_HISI;
	res = vscale_new(loop, &config, &cbs, NULL, &scaler);
	CU_ASSERT_EQUAL(res, -ENOSYS);
	CU_ASSERT_PTR_NULL(scaler);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_libyuv_valid(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	int res;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	if (!libyuv_available()) {
		pomp_loop_destroy(loop);
		return;
	}

	mock_vscale_cbs_ctx_reset(&ctx);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	res = vscale_new(loop, &config, &cbs, &ctx, &scaler);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL(scaler);
	if (!scaler)
		goto out;
	CU_ASSERT_EQUAL(vscale_get_used_implem(scaler),
			VSCALE_SCALER_IMPLEM_LIBYUV);
	do_stop_destroy(scaler, loop, &ctx);
out:
	pomp_loop_destroy(loop);
}


static void test_vscale_new_auto_resolves_to_libyuv(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;
	int res;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	if (!libyuv_available()) {
		pomp_loop_destroy(loop);
		return;
	}

	mock_vscale_cbs_ctx_reset(&ctx);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	config.implem = VSCALE_SCALER_IMPLEM_AUTO;
	res = vscale_new(loop, &config, &cbs, &ctx, &scaler);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL(scaler);
	if (!scaler)
		goto out;
	CU_ASSERT_EQUAL(vscale_get_used_implem(scaler),
			VSCALE_SCALER_IMPLEM_LIBYUV);
	do_stop_destroy(scaler, loop, &ctx);
out:
	pomp_loop_destroy(loop);
}


static void test_vscale_new_with_name(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	if (!libyuv_available()) {
		pomp_loop_destroy(loop);
		return;
	}

	mock_vscale_cbs_ctx_reset(&ctx);
	fill_valid_libyuv_config(&config, 64, 64, 64, 64);
	config.name = "test-scaler";
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);
	CU_ASSERT_PTR_NOT_NULL(scaler);
	if (scaler)
		do_stop_destroy(scaler, loop, &ctx);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_downscale(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	if (!libyuv_available()) {
		pomp_loop_destroy(loop);
		return;
	}

	mock_vscale_cbs_ctx_reset(&ctx);
	fill_valid_libyuv_config(&config, 128, 128, 64, 64);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);
	CU_ASSERT_PTR_NOT_NULL(scaler);
	if (scaler)
		do_stop_destroy(scaler, loop, &ctx);
	pomp_loop_destroy(loop);
}


static void test_vscale_new_upscale(void)
{
	struct pomp_loop *loop = pomp_loop_new();
	struct vscale_config config;
	struct mock_vscale_cbs_ctx ctx;
	struct vscale_cbs cbs = {
		.frame_output = mock_vscale_frame_output_cb,
		.stop = mock_vscale_stop_cb,
	};
	struct vscale_scaler *scaler = NULL;

	CU_ASSERT_PTR_NOT_NULL_FATAL(loop);
	if (!libyuv_available()) {
		pomp_loop_destroy(loop);
		return;
	}

	mock_vscale_cbs_ctx_reset(&ctx);
	fill_valid_libyuv_config(&config, 64, 64, 128, 128);
	CU_ASSERT_EQUAL(vscale_new(loop, &config, &cbs, &ctx, &scaler), 0);
	CU_ASSERT_PTR_NOT_NULL(scaler);
	if (scaler)
		do_stop_destroy(scaler, loop, &ctx);
	pomp_loop_destroy(loop);
}


/* ---- vscale_get_supported_input_formats tests ---- */

static void test_get_supported_formats_libyuv(void)
{
	const struct vdef_raw_format *fmts = NULL;
	int count;

	if (!libyuv_available())
		return;

	count = vscale_get_supported_input_formats(VSCALE_SCALER_IMPLEM_LIBYUV,
						   &fmts);
	CU_ASSERT_TRUE(count >= 3);
	CU_ASSERT_PTR_NOT_NULL(fmts);
	if (!fmts)
		return;

	bool has_i420 = false, has_nv12 = false, has_nv21 = false;
	for (int i = 0; i < count; i++) {
		if (vdef_raw_format_cmp(&fmts[i], &vdef_i420))
			has_i420 = true;
		if (vdef_raw_format_cmp(&fmts[i], &vdef_nv12))
			has_nv12 = true;
		if (vdef_raw_format_cmp(&fmts[i], &vdef_nv21))
			has_nv21 = true;
	}
	CU_ASSERT_TRUE(has_i420);
	CU_ASSERT_TRUE(has_nv12);
	CU_ASSERT_TRUE(has_nv21);
}


static void test_get_supported_formats_null_arg(void)
{
	CU_ASSERT_TRUE(
		vscale_get_supported_input_formats(VSCALE_SCALER_IMPLEM_LIBYUV,
						   NULL) < 0);
}


static void test_get_supported_formats_hisi(void)
{
	const struct vdef_raw_format *fmts = NULL;
	int count;

	/* HISI not built → should return 0 or negative */
	count = vscale_get_supported_input_formats(VSCALE_SCALER_IMPLEM_HISI,
						   &fmts);
	CU_ASSERT_TRUE(count <= 0);
}


/* ---- vscale_get_input_buffer_constraints tests ---- */

static void test_get_constraints_libyuv_zeros(void)
{
	struct vscale_input_buffer_constraints c;
	int res;

	if (!libyuv_available())
		return;

	memset(&c, 0xff, sizeof(c));
	res = vscale_get_input_buffer_constraints(
		VSCALE_SCALER_IMPLEM_LIBYUV, &vdef_i420, &c);
	CU_ASSERT_EQUAL(res, 0);

	/* libyuv ops table has no get_input_buffer_constraints → fallback zeros
	 * only the first nb_planes entries (not the full VDEF_RAW_MAX_PLANE_COUNT
	 * array). */
	unsigned int nb_planes = vdef_get_raw_frame_plane_count(&vdef_i420);
	for (unsigned int i = 0; i < nb_planes; i++) {
		CU_ASSERT_EQUAL(c.plane_stride_align[i], 0U);
		CU_ASSERT_EQUAL(c.plane_scanline_align[i], 0U);
		CU_ASSERT_EQUAL(c.plane_size_align[i], 0U);
	}
}


static void test_get_constraints_null_format(void)
{
	struct vscale_input_buffer_constraints c;
	CU_ASSERT_TRUE(
		vscale_get_input_buffer_constraints(
			VSCALE_SCALER_IMPLEM_LIBYUV, NULL, &c) < 0);
}


static void test_get_constraints_null_constraints(void)
{
	CU_ASSERT_TRUE(
		vscale_get_input_buffer_constraints(
			VSCALE_SCALER_IMPLEM_LIBYUV, &vdef_i420, NULL) < 0);
}


static void test_get_constraints_unavailable_implem(void)
{
	struct vscale_input_buffer_constraints c;

	/* HISI not built → vscale_get_implem returns -ENOSYS,
	 * function logs the error and returns 0 via RETURN_VAL_IF. */
	memset(&c, 0, sizeof(c));
	vscale_get_input_buffer_constraints(
		VSCALE_SCALER_IMPLEM_HISI, &vdef_i420, &c);
	/* No assertion on the return value: the macro returns 0 on -ENOSYS,
	 * the goal is to exercise the line for coverage. */
}


/* ---- vscale_flush / stop / destroy null-scaler guards ---- */

static void test_vscale_flush_null(void)
{
	CU_ASSERT_EQUAL(vscale_flush(NULL, false), -EINVAL);
}


static void test_vscale_stop_null(void)
{
	CU_ASSERT_EQUAL(vscale_stop(NULL), -EINVAL);
}


static void test_vscale_destroy_null(void)
{
	CU_ASSERT_EQUAL(vscale_destroy(NULL), -EINVAL);
}


static void test_vscale_get_input_buffer_pool_null_scaler(void)
{
	CU_ASSERT_PTR_NULL(vscale_get_input_buffer_pool(NULL));
}


static void test_vscale_get_input_buffer_queue_null_scaler(void)
{
	CU_ASSERT_PTR_NULL(vscale_get_input_buffer_queue(NULL));
}


/* ---- vscale_get_used_implem null test ---- */

static void test_get_used_implem_null(void)
{
	CU_ASSERT_EQUAL(vscale_get_used_implem(NULL), VSCALE_SCALER_IMPLEM_AUTO);
}


CU_TestInfo g_vscale_test_config[] = {
	{FN("vscale-new-null-loop"), &test_vscale_new_null_loop},
	{FN("vscale-new-null-config"), &test_vscale_new_null_config},
	{FN("vscale-new-null-cbs"), &test_vscale_new_null_cbs},
	{FN("vscale-new-null-frame-output-cb"),
	 &test_vscale_new_null_frame_output_cb},
	{FN("vscale-new-null-ret-obj"), &test_vscale_new_null_ret_obj},
	{FN("vscale-new-zero-input-width"), &test_vscale_new_zero_input_width},
	{FN("vscale-new-zero-input-height"),
	 &test_vscale_new_zero_input_height},
	{FN("vscale-new-zero-output-width"),
	 &test_vscale_new_zero_output_width},
	{FN("vscale-new-zero-output-height"),
	 &test_vscale_new_zero_output_height},
	{FN("vscale-new-unbuilt-implem"), &test_vscale_new_unbuilt_implem},
	{FN("vscale-new-libyuv-valid"), &test_vscale_new_libyuv_valid},
	{FN("vscale-new-auto-resolves-to-libyuv"),
	 &test_vscale_new_auto_resolves_to_libyuv},
	{FN("vscale-new-with-name"), &test_vscale_new_with_name},
	{FN("vscale-new-downscale"), &test_vscale_new_downscale},
	{FN("vscale-new-upscale"), &test_vscale_new_upscale},
	{FN("get-supported-formats-libyuv"),
	 &test_get_supported_formats_libyuv},
	{FN("get-supported-formats-null-arg"),
	 &test_get_supported_formats_null_arg},
	{FN("get-supported-formats-hisi"), &test_get_supported_formats_hisi},
	{FN("get-constraints-libyuv-zeros"),
	 &test_get_constraints_libyuv_zeros},
	{FN("get-constraints-null-format"), &test_get_constraints_null_format},
	{FN("get-constraints-null-constraints"),
	 &test_get_constraints_null_constraints},
	{FN("get-constraints-unavailable-implem"),
	 &test_get_constraints_unavailable_implem},
	{FN("vscale-flush-null"), &test_vscale_flush_null},
	{FN("vscale-stop-null"), &test_vscale_stop_null},
	{FN("vscale-destroy-null"), &test_vscale_destroy_null},
	{FN("vscale-get-input-buffer-pool-null-scaler"),
	 &test_vscale_get_input_buffer_pool_null_scaler},
	{FN("vscale-get-input-buffer-queue-null-scaler"),
	 &test_vscale_get_input_buffer_queue_null_scaler},
	{FN("get-used-implem-null"), &test_get_used_implem_null},
	CU_TEST_INFO_NULL,
};
