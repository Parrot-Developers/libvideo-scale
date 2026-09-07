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

#include <stdatomic.h>

#include "vscale_test.h"


/* Build a finalized single-plane I420 frame with the given format and
 * timestamp. Caller must unref frame and mem when done. */
static struct mbuf_raw_video_frame *
new_test_frame(struct mbuf_mem **out_mem,
	       struct vdef_raw_format format,
	       uint32_t width,
	       uint32_t height,
	       uint64_t timestamp)
{
	int ret;
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	size_t frame_size;
	unsigned int plane_count;

	/* I420: Y + U/2 + V/2; NV12/NV21: Y + UV/2; RGB: 1 plane */
	plane_count = vdef_get_raw_frame_plane_count(&format);
	if (plane_count == 3)
		frame_size = (size_t)width * height * 3 / 2;
	else if (plane_count == 2)
		frame_size = (size_t)width * height * 3 / 2;
	else
		frame_size = (size_t)width * height * 3;

	ret = mbuf_mem_generic_new(frame_size, &mem);
	if (ret != 0)
		return NULL;

	memset(&frame_info, 0, sizeof(frame_info));
	frame_info.format = format;
	frame_info.info.resolution.width = width;
	frame_info.info.resolution.height = height;
	frame_info.info.timescale = 1000000;
	frame_info.info.timestamp = timestamp;

	if (plane_count == 3) {
		/* I420 */
		frame_info.plane_stride[0] = width;
		frame_info.plane_stride[1] = width / 2;
		frame_info.plane_stride[2] = width / 2;
	} else if (plane_count == 2) {
		/* NV12 / NV21 */
		frame_info.plane_stride[0] = width;
		frame_info.plane_stride[1] = width;
	} else {
		/* RGB */
		frame_info.plane_stride[0] = width * 3;
	}

	ret = mbuf_raw_video_frame_new(&frame_info, &frame);
	if (ret != 0) {
		mbuf_mem_unref(mem);
		return NULL;
	}

	if (plane_count == 3) {
		size_t y_size = (size_t)width * height;
		size_t uv_size = y_size / 4;
		mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, y_size);
		mbuf_raw_video_frame_set_plane(frame, 1, mem, y_size, uv_size);
		mbuf_raw_video_frame_set_plane(
			frame, 2, mem, y_size + uv_size, uv_size);
	} else if (plane_count == 2) {
		size_t y_size = (size_t)width * height;
		size_t uv_size = y_size / 2;
		mbuf_raw_video_frame_set_plane(frame, 0, mem, 0, y_size);
		mbuf_raw_video_frame_set_plane(frame, 1, mem, y_size, uv_size);
	} else {
		mbuf_raw_video_frame_set_plane(
			frame, 0, mem, 0, (size_t)width * height * 3);
	}

	mbuf_raw_video_frame_finalize(frame);

	*out_mem = mem;
	return frame;
}


/* ---- enum string tests ---- */

static void test_scaler_implem_str_round_trip(void)
{
	static const struct {
		enum vscale_scaler_implem val;
		const char *str;
	} cases[] = {
		{VSCALE_SCALER_IMPLEM_LIBYUV, "LIBYUV"},
		{VSCALE_SCALER_IMPLEM_HISI, "HISI"},
		{VSCALE_SCALER_IMPLEM_QCOM, "QCOM"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CU_ASSERT_STRING_EQUAL(
			vscale_scaler_implem_to_str(cases[i].val), cases[i].str);
		CU_ASSERT_EQUAL(vscale_scaler_implem_from_str(cases[i].str),
				cases[i].val);
	}
}


static void test_scaler_implem_from_str_unknown(void)
{
	CU_ASSERT_EQUAL(vscale_scaler_implem_from_str("notanimplem"),
			VSCALE_SCALER_IMPLEM_AUTO);
	CU_ASSERT_EQUAL(vscale_scaler_implem_from_str(""),
			VSCALE_SCALER_IMPLEM_AUTO);
}


static void test_scaler_implem_to_str_auto(void)
{
	CU_ASSERT_STRING_EQUAL(
		vscale_scaler_implem_to_str(VSCALE_SCALER_IMPLEM_AUTO), "AUTO");
}


static void test_scaler_implem_to_str_unknown(void)
{
	CU_ASSERT_STRING_EQUAL(
		vscale_scaler_implem_to_str((enum vscale_scaler_implem)999),
		"UNKNOWN");
}


static void test_filter_mode_str_round_trip(void)
{
	static const struct {
		enum vscale_filter_mode val;
		const char *str;
	} cases[] = {
		{VSCALE_FILTER_MODE_AUTO, "AUTO"},
		{VSCALE_FILTER_MODE_NONE, "NONE"},
		{VSCALE_FILTER_MODE_LINEAR, "LINEAR"},
		{VSCALE_FILTER_MODE_BILINEAR, "BILINEAR"},
		{VSCALE_FILTER_MODE_BOX, "BOX"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CU_ASSERT_STRING_EQUAL(
			vscale_filter_mode_to_str(cases[i].val), cases[i].str);
		CU_ASSERT_EQUAL(vscale_filter_mode_from_str(cases[i].str),
				cases[i].val);
	}
}


static void test_filter_mode_from_str_unknown(void)
{
	CU_ASSERT_EQUAL(vscale_filter_mode_from_str("notamode"),
			VSCALE_FILTER_MODE_AUTO);
}


static void test_filter_mode_to_str_unknown(void)
{
	CU_ASSERT_STRING_EQUAL(
		vscale_filter_mode_to_str((enum vscale_filter_mode)999),
		"UNKNOWN");
}


/* ---- vscale_config_get_specific tests ---- */

static void test_config_get_specific_no_implem_cfg(void)
{
	struct vscale_config config;
	memset(&config, 0, sizeof(config));
	config.implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config.implem_cfg = NULL;

	CU_ASSERT_PTR_NULL(
		vscale_config_get_specific(&config, VSCALE_SCALER_IMPLEM_LIBYUV));
}


static void test_config_get_specific_implem_mismatch(void)
{
	struct vscale_config_impl impl_cfg;
	struct vscale_config config;

	memset(&config, 0, sizeof(config));
	memset(&impl_cfg, 0, sizeof(impl_cfg));

	impl_cfg.implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config.implem = VSCALE_SCALER_IMPLEM_HISI;
	config.implem_cfg = &impl_cfg;

	/* requested implem != config.implem → NULL */
	CU_ASSERT_PTR_NULL(
		vscale_config_get_specific(&config, VSCALE_SCALER_IMPLEM_LIBYUV));
}


static void test_config_get_specific_implem_cfg_mismatch(void)
{
	struct vscale_config_impl impl_cfg;
	struct vscale_config config;

	memset(&config, 0, sizeof(config));
	memset(&impl_cfg, 0, sizeof(impl_cfg));

	/* config.implem_cfg->implem != config.implem */
	impl_cfg.implem = VSCALE_SCALER_IMPLEM_HISI;
	config.implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config.implem_cfg = &impl_cfg;

	CU_ASSERT_PTR_NULL(
		vscale_config_get_specific(&config, VSCALE_SCALER_IMPLEM_LIBYUV));
}


static void test_config_get_specific_match(void)
{
	struct vscale_config_impl impl_cfg;
	struct vscale_config config;

	memset(&config, 0, sizeof(config));
	memset(&impl_cfg, 0, sizeof(impl_cfg));

	impl_cfg.implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config.implem = VSCALE_SCALER_IMPLEM_LIBYUV;
	config.implem_cfg = &impl_cfg;

	CU_ASSERT_PTR_EQUAL(
		vscale_config_get_specific(&config, VSCALE_SCALER_IMPLEM_LIBYUV),
		&impl_cfg);
}


/* ---- vscale_default_input_filter tests ---- */

static void test_input_filter_accepts_matching_frame(void)
{
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};
	bool accepted;

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	frame = new_test_frame(&mem, vdef_i420, 64, 64, 1000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

	mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	accepted = vscale_default_input_filter_internal(
		&scaler, frame, &frame_info, fmts, 1);
	CU_ASSERT_TRUE(accepted);

	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
}


static void test_input_filter_rejects_null_frame(void)
{
	/* vscale_default_input_filter: !frame → false */
	CU_ASSERT_FALSE(vscale_default_input_filter(NULL, (void *)1));
}


static void test_input_filter_rejects_null_scaler(void)
{
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;

	frame = new_test_frame(&mem, vdef_i420, 64, 64, 1000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

	/* userdata=NULL → scaler=NULL → false */
	CU_ASSERT_FALSE(vscale_default_input_filter(frame, NULL));

	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
}


static void test_input_filter_rejects_unsupported_format(void)
{
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	frame = new_test_frame(&mem, vdef_nv12, 64, 64, 1000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

	mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	CU_ASSERT_FALSE(vscale_default_input_filter_internal(
		&scaler, frame, &frame_info, fmts, 1));

	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
}


static void test_input_filter_rejects_wrong_resolution(void)
{
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	frame = new_test_frame(&mem, vdef_i420, 128, 128, 1000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

	mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	CU_ASSERT_FALSE(vscale_default_input_filter_internal(
		&scaler, frame, &frame_info, fmts, 1));

	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
}


static void test_input_filter_rejects_equal_timestamp(void)
{
	struct mbuf_mem *mem1 = NULL, *mem2 = NULL;
	struct mbuf_raw_video_frame *f1 = NULL, *f2 = NULL;
	struct vdef_raw_frame fi1, fi2;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	f1 = new_test_frame(&mem1, vdef_i420, 64, 64, 1000);
	f2 = new_test_frame(&mem2, vdef_i420, 64, 64, 1000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(f1);
	CU_ASSERT_PTR_NOT_NULL_FATAL(f2);

	mbuf_raw_video_frame_get_frame_info(f1, &fi1);
	mbuf_raw_video_frame_get_frame_info(f2, &fi2);

	CU_ASSERT_TRUE(
		vscale_default_input_filter_internal(&scaler, f1, &fi1, fmts, 1));
	vscale_default_input_filter_internal_confirm_frame(&scaler, f1, &fi1);

	CU_ASSERT_FALSE(
		vscale_default_input_filter_internal(&scaler, f2, &fi2, fmts, 1));

	mbuf_raw_video_frame_unref(f1);
	mbuf_raw_video_frame_unref(f2);
	mbuf_mem_unref(mem1);
	mbuf_mem_unref(mem2);
}


static void test_input_filter_rejects_earlier_timestamp(void)
{
	struct mbuf_mem *mem1 = NULL, *mem2 = NULL;
	struct mbuf_raw_video_frame *f1 = NULL, *f2 = NULL;
	struct vdef_raw_frame fi1, fi2;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	f1 = new_test_frame(&mem1, vdef_i420, 64, 64, 1000);
	f2 = new_test_frame(&mem2, vdef_i420, 64, 64, 500);
	CU_ASSERT_PTR_NOT_NULL_FATAL(f1);
	CU_ASSERT_PTR_NOT_NULL_FATAL(f2);

	mbuf_raw_video_frame_get_frame_info(f1, &fi1);
	mbuf_raw_video_frame_get_frame_info(f2, &fi2);

	CU_ASSERT_TRUE(
		vscale_default_input_filter_internal(&scaler, f1, &fi1, fmts, 1));
	vscale_default_input_filter_internal_confirm_frame(&scaler, f1, &fi1);

	CU_ASSERT_FALSE(
		vscale_default_input_filter_internal(&scaler, f2, &fi2, fmts, 1));

	mbuf_raw_video_frame_unref(f1);
	mbuf_raw_video_frame_unref(f2);
	mbuf_mem_unref(mem1);
	mbuf_mem_unref(mem2);
}


static void test_input_filter_confirm_updates_state(void)
{
	struct mbuf_mem *mem = NULL;
	struct mbuf_raw_video_frame *frame = NULL;
	struct vdef_raw_frame frame_info;
	struct vscale_scaler scaler;
	const struct vdef_raw_format fmts[] = {vdef_i420};
	bool accepted;

	memset(&scaler, 0, sizeof(scaler));
	scaler.last_timestamp = UINT64_MAX;
	scaler.config.input.info.resolution.width = 64;
	scaler.config.input.info.resolution.height = 64;

	frame = new_test_frame(&mem, vdef_i420, 64, 64, 42000);
	CU_ASSERT_PTR_NOT_NULL_FATAL(frame);

	mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	accepted = vscale_default_input_filter_internal(
		&scaler, frame, &frame_info, fmts, 1);
	CU_ASSERT_TRUE(accepted);

	vscale_default_input_filter_internal_confirm_frame(
		&scaler, frame, &frame_info);

	CU_ASSERT_EQUAL(scaler.last_timestamp, 42000U);
	CU_ASSERT_EQUAL(atomic_load(&scaler.counters.in), 1U);

	mbuf_raw_video_frame_unref(frame);
	mbuf_mem_unref(mem);
}


CU_TestInfo g_vscale_test_core[] = {
	{FN("scaler-implem-str-round-trip"),
	 &test_scaler_implem_str_round_trip},
	{FN("scaler-implem-from-str-unknown"),
	 &test_scaler_implem_from_str_unknown},
	{FN("scaler-implem-to-str-auto"), &test_scaler_implem_to_str_auto},
	{FN("scaler-implem-to-str-unknown"),
	 &test_scaler_implem_to_str_unknown},
	{FN("filter-mode-str-round-trip"), &test_filter_mode_str_round_trip},
	{FN("filter-mode-from-str-unknown"),
	 &test_filter_mode_from_str_unknown},
	{FN("filter-mode-to-str-unknown"), &test_filter_mode_to_str_unknown},
	{FN("config-get-specific-no-implem-cfg"),
	 &test_config_get_specific_no_implem_cfg},
	{FN("config-get-specific-implem-mismatch"),
	 &test_config_get_specific_implem_mismatch},
	{FN("config-get-specific-implem-cfg-mismatch"),
	 &test_config_get_specific_implem_cfg_mismatch},
	{FN("config-get-specific-match"), &test_config_get_specific_match},
	{FN("input-filter-accepts-matching-frame"),
	 &test_input_filter_accepts_matching_frame},
	{FN("input-filter-rejects-null-frame"),
	 &test_input_filter_rejects_null_frame},
	{FN("input-filter-rejects-null-scaler"),
	 &test_input_filter_rejects_null_scaler},
	{FN("input-filter-rejects-unsupported-format"),
	 &test_input_filter_rejects_unsupported_format},
	{FN("input-filter-rejects-wrong-resolution"),
	 &test_input_filter_rejects_wrong_resolution},
	{FN("input-filter-rejects-equal-timestamp"),
	 &test_input_filter_rejects_equal_timestamp},
	{FN("input-filter-rejects-earlier-timestamp"),
	 &test_input_filter_rejects_earlier_timestamp},
	{FN("input-filter-confirm-updates-state"),
	 &test_input_filter_confirm_updates_state},
	CU_TEST_INFO_NULL,
};
