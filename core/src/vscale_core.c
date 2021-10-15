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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define ULOG_TAG vcsale_core
#include <ulog.h>

#include <futils/timetools.h>
#include <video-scale/vscale_core.h>
#include <video-scale/vscale_internal.h>


bool vscale_default_input_filter(struct mbuf_raw_video_frame *frame,
				 void *userdata)
{
	int ret;
	bool accept;
	struct vscale_scaler *scaler = userdata;
	const struct vdef_raw_format *supported_formats;
	struct vdef_raw_frame frame_info;

	if (!frame || !scaler)
		return false;

	ret = mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	if (ret != 0)
		return false;

	ret = scaler->ops->get_supported_input_formats(&supported_formats);
	if (ret < 0)
		return false;
	accept = vscale_default_input_filter_internal(
		scaler, frame, &frame_info, supported_formats, ret);
	if (accept)
		vscale_default_input_filter_internal_confirm_frame(
			scaler, frame, &frame_info);
	return accept;
}


bool vscale_default_input_filter_internal(
	struct vscale_scaler *scaler,
	struct mbuf_raw_video_frame *frame,
	struct vdef_raw_frame *frame_info,
	const struct vdef_raw_format *supported_formats,
	unsigned int nb_supported_formats)
{
	if (!vdef_raw_format_intersect(&frame_info->format,
				       supported_formats,
				       nb_supported_formats)) {
		ULOG_ERRNO(
			"unsupported format:"
			" " VDEF_RAW_FORMAT_TO_STR_FMT,
			EPROTO,
			VDEF_RAW_FORMAT_TO_STR_ARG(&frame_info->format));
		return false;
	}

	if (frame_info->info.timestamp <= scaler->last_timestamp &&
	    scaler->last_timestamp != UINT64_MAX) {
		ULOG_ERRNO("non-strictly-monotonic timestamp (%" PRIu64
			   " <= %" PRIu64 ")",
			   EPROTO,
			   frame_info->info.timestamp,
			   scaler->last_timestamp);
		return false;
	}

	if (!vdef_dim_cmp(&scaler->config.input.info.resolution,
			  &frame_info->info.resolution)) {
		ULOG_ERRNO("invalid frame information resolution:%ux%u",
			   EPROTO,
			   frame_info->info.resolution.width,
			   frame_info->info.resolution.height);
		return false;
	}

	return true;
}

void vscale_default_input_filter_internal_confirm_frame(
	struct vscale_scaler *scaler,
	struct mbuf_raw_video_frame *frame,
	struct vdef_raw_frame *frame_info)
{
	int err;
	uint64_t ts_us;
	struct timespec cur_ts = {0, 0};

	/* Save frame timestamp to last_timestamp */
	scaler->last_timestamp = frame_info->info.timestamp;

	/* Set the input time ancillary data to the frame */
	time_get_monotonic(&cur_ts);
	time_timespec_to_us(&cur_ts, &ts_us);
	err = mbuf_raw_video_frame_add_ancillary_buffer(
		frame, VSCALE_ANCILLARY_KEY_INPUT_TIME, &ts_us, sizeof(ts_us));
	if (err < 0)
		ULOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer", -err);
}
