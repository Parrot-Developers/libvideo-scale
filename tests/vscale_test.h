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

#ifndef _VSCALE_TEST_H_
#define _VSCALE_TEST_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <CUnit/Automated.h>
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

#include <media-buffers/mbuf_ancillary_data.h>
#include <media-buffers/mbuf_mem.h>
#include <media-buffers/mbuf_mem_generic.h>
#include <media-buffers/mbuf_raw_video_frame.h>
#include <video-defs/vdefs.h>
#include <video-metadata/vmeta_frame.h>
#include <video-scale/vscale.h>
#include <video-scale/vscale_core.h>
#include <video-scale/vscale_internal.h>

#define FN(_name) (char *)(_name)

#define VSCALE_TEST_MOCK_MAX_CAPTURED 32

struct mock_vscale_cbs_ctx {
	struct mbuf_raw_video_frame *captured[VSCALE_TEST_MOCK_MAX_CAPTURED];
	int frame_count;
	int status[VSCALE_TEST_MOCK_MAX_CAPTURED];
	bool flush_done;
	bool stop_done;
};

void mock_vscale_cbs_ctx_reset(struct mock_vscale_cbs_ctx *ctx);
void mock_vscale_cbs_ctx_clear(struct mock_vscale_cbs_ctx *ctx);

void mock_vscale_frame_output_cb(struct vscale_scaler *scaler,
				 int status,
				 struct mbuf_raw_video_frame *frame,
				 void *userdata);
void mock_vscale_flush_cb(struct vscale_scaler *scaler, void *userdata);
void mock_vscale_stop_cb(struct vscale_scaler *scaler, void *userdata);

static inline bool libyuv_available(void)
{
	const struct vdef_raw_format *fmts = NULL;
	return vscale_get_supported_input_formats(VSCALE_SCALER_IMPLEM_LIBYUV,
						  &fmts) > 0;
}

extern CU_TestInfo g_vscale_test_core[];
extern CU_TestInfo g_vscale_test_config[];
extern CU_TestInfo g_vscale_test_lifecycle[];

#endif /* !_VSCALE_TEST_H_ */
