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

#define ULOG_TAG vscale_libyuv
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include <stdatomic.h>

#include <pthread.h>
#include <string.h>

#if defined(__APPLE__)
#	include <TargetConditionals.h>
#endif

#include <libyuv/convert.h>
#include <libyuv/convert_argb.h>
#include <libyuv/convert_from.h>
#include <libyuv/scale.h>
#include <libyuv/version.h>

#include <futils/timetools.h>
#include <libpomp.h>
#include <media-buffers/mbuf_mem_generic.h>
#include <media-buffers/mbuf_raw_video_frame.h>
#include <video-scale/vscale_internal.h>


enum state {
	RUNNING,
	WAITING_FOR_STOP,
	WAITING_FOR_FLUSH,
	WAITING_FOR_EOS,
};


struct vscale_libyuv {
	struct vscale_scaler *base;

	pthread_mutex_t mutex;
	pthread_cond_t cond;

	bool stop_flag;
	bool flush_flag;
	bool eos_flag;

	struct pomp_evt *error_event;
	int status;

	pthread_t thread;
	bool thread_launched;

	enum state state;

	struct mbuf_raw_video_frame_queue *input_queue;
	struct mbuf_raw_video_frame_queue *output_queue;
	struct pomp_evt *output_event;
	enum FilterMode libyuv_mode;
	struct mbuf_mem *tmp_mem;
	size_t tmp_mem_size;
};


#define NB_SUPPORTED_FORMATS 3
static struct vdef_raw_format supported_formats[NB_SUPPORTED_FORMATS];
static pthread_once_t supported_formats_is_init = PTHREAD_ONCE_INIT;
static void initialize_supported_formats(void)
{
	supported_formats[0] = vdef_i420;
	supported_formats[1] = vdef_nv12;
	supported_formats[2] = vdef_nv21;
}


static const enum FilterMode HANDLED_FILTER_MODES[] = {
	[VSCALE_FILTER_MODE_AUTO] = kFilterBilinear,
	[VSCALE_FILTER_MODE_NONE] = kFilterNone,
	[VSCALE_FILTER_MODE_LINEAR] = kFilterLinear,
	[VSCALE_FILTER_MODE_BILINEAR] = kFilterBilinear,
	[VSCALE_FILTER_MODE_BOX] = kFilterBox,
};


static void error_evt_cb(struct pomp_evt *evt, void *userdata)
{
	struct vscale_libyuv *self = userdata;
	pthread_mutex_lock(&self->mutex);
	int status = self->status;
	self->status = 0;
	pthread_mutex_unlock(&self->mutex);

	self->base->cbs.frame_output(
		self->base, status, NULL, self->base->userdata);
}


static void output_evt_cb(struct pomp_evt *evt, void *userdata)
{
	struct vscale_libyuv *self = userdata;

	switch (self->state) {
	case WAITING_FOR_EOS:
	case RUNNING:
		while (true) {
			struct mbuf_raw_video_frame *frame;
			int res = mbuf_raw_video_frame_queue_pop(
				self->output_queue, &frame);

			if (res < 0) {
				if (res != -EAGAIN)
					VSCALE_LOG_ERRNO(
						"mbuf_raw_video_frame"
						"_queue_pop",
						-res);
				break;
			}

			self->base->cbs.frame_output(
				self->base, 0, frame, self->base->userdata);
			self->base->counters.out++;
			mbuf_raw_video_frame_unref(frame);
		}

		if (self->state == WAITING_FOR_EOS) {
			pthread_mutex_lock(&self->mutex);
			bool eos_flag = self->eos_flag;
			if (!eos_flag)
				self->state = RUNNING;
			pthread_mutex_unlock(&self->mutex);

			if (!eos_flag) {
				if (self->base->cbs.flush != NULL)
					self->base->cbs.flush(
						self->base,
						self->base->userdata);
			}
		}
		break;
	case WAITING_FOR_STOP: {
		pthread_mutex_lock(&self->mutex);
		bool stop_flag = self->stop_flag;
		if (!stop_flag)
			self->state = RUNNING;
		pthread_mutex_unlock(&self->mutex);
		if (!stop_flag) {
			if (self->base->cbs.stop != NULL)
				self->base->cbs.stop(self->base,
						     self->base->userdata);
		}
		break;
	}
	case WAITING_FOR_FLUSH: {
		pthread_mutex_lock(&self->mutex);
		bool flush_flag = self->flush_flag;
		if (!flush_flag)
			self->state = RUNNING;
		pthread_mutex_unlock(&self->mutex);
		if (!flush_flag) {
			mbuf_raw_video_frame_queue_flush(self->input_queue);
			mbuf_raw_video_frame_queue_flush(self->output_queue);
			if (self->base->cbs.flush != NULL)
				self->base->cbs.flush(self->base,
						      self->base->userdata);
		}
		break;
	}
	}
}


static int get_supported_input_formats(const struct vdef_raw_format **formats)
{
	(void)pthread_once(&supported_formats_is_init,
			   initialize_supported_formats);
	*formats = supported_formats;
	return NB_SUPPORTED_FORMATS;
}


static int flush(struct vscale_scaler *base, bool discard)
{
	struct vscale_libyuv *self = base->derived;

	if (discard) {
		pthread_mutex_lock(&self->mutex);
		self->flush_flag = true;
		self->state = WAITING_FOR_FLUSH;
		pthread_cond_signal(&self->cond);
		pthread_mutex_unlock(&self->mutex);
	} else {
		pthread_mutex_lock(&self->mutex);
		self->eos_flag = true;
		self->state = WAITING_FOR_EOS;
		pthread_cond_signal(&self->cond);
		pthread_mutex_unlock(&self->mutex);
	}

	return 0;
}


static int stop(struct vscale_scaler *base)
{
	struct vscale_libyuv *self = base->derived;

	pthread_mutex_lock(&self->mutex);
	self->stop_flag = true;
	self->state = WAITING_FOR_STOP;
	pthread_cond_signal(&self->cond);
	pthread_mutex_unlock(&self->mutex);

	return 0;
}


static int destroy(struct vscale_scaler *base)
{
	struct vscale_libyuv *self = base->derived;
	int ret = 0;

	if (self->thread_launched) {
		stop(base);
		ret = pthread_join(self->thread, NULL);
		if (ret != 0)
			VSCALE_LOG_ERRNO("pthread_join", -ret);
	}

	pthread_mutex_destroy(&self->mutex);
	pthread_cond_destroy(&self->cond);
	if (self->output_event != NULL) {
		if (pomp_evt_is_attached(self->output_event, base->loop)) {
			ret = pomp_evt_detach_from_loop(self->output_event,
							base->loop);
			if (ret < 0)
				VSCALE_LOG_ERRNO("pomp_evt_detach_from_loop",
						 -ret);
		}

		pomp_evt_destroy(self->output_event);
	}
	if (self->error_event != NULL) {
		if (pomp_evt_is_attached(self->error_event, base->loop)) {
			ret = pomp_evt_detach_from_loop(self->error_event,
							base->loop);
			if (ret < 0)
				VSCALE_LOG_ERRNO("pomp_evt_detach_from_loop",
						 -ret);
		}

		pomp_evt_destroy(self->error_event);
	}

	if (self->input_queue != 0) {
		ret = mbuf_raw_video_frame_queue_flush(self->input_queue);
		if (ret < 0)
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_flush",
					 -ret);
		ret = mbuf_raw_video_frame_queue_destroy(self->input_queue);
		if (ret < 0)
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_destroy",
					 -ret);
	}
	if (self->output_queue != 0) {
		ret = mbuf_raw_video_frame_queue_flush(self->output_queue);
		if (ret < 0)
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_flush",
					 -ret);
		ret = mbuf_raw_video_frame_queue_destroy(self->output_queue);
		if (ret < 0)
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_destroy",
					 -ret);
	}

	if (self->tmp_mem != NULL)
		mbuf_mem_unref(self->tmp_mem);

	free(self);
	return 0;
}


static bool input_filter(struct mbuf_raw_video_frame *frame, void *userdata)
{
	bool accept;
	struct vscale_libyuv *self = userdata;

	if (self->state != RUNNING)
		return false;

	accept = vscale_default_input_filter(frame, self->base);

	if (accept) {
		pthread_mutex_lock(&self->mutex);
		pthread_cond_signal(&self->cond);
		pthread_mutex_unlock(&self->mutex);
	}

	return accept;
}


static int do_scale(const struct vdef_raw_format *format,
		    const void *src_planes[3],
		    const size_t src_strides[3],
		    int src_width,
		    int src_height,
		    uint8_t *dst,
		    int dst_width,
		    int dst_height,
		    enum FilterMode mode)
{
	if (vdef_raw_format_cmp(format, &vdef_i420)) {
		return I420Scale(src_planes[0],
				 src_strides[0],
				 src_planes[1],
				 src_strides[1],
				 src_planes[2],
				 src_strides[2],
				 src_width,
				 src_height,
				 dst,
				 dst_width,
				 dst + dst_width * dst_height,
				 dst_width / 2,
				 dst + (dst_width * dst_height * 5) / 4,
				 dst_width / 2,
				 dst_width,
				 dst_height,
				 mode);
	} else if (vdef_raw_format_cmp(format, &vdef_nv12) ||
		   vdef_raw_format_cmp(format, &vdef_nv21)) {
		return NV12Scale(src_planes[0],
				 src_strides[0],
				 src_planes[1],
				 src_strides[1],
				 src_width,
				 src_height,
				 dst,
				 dst_width,
				 dst + dst_width * dst_height,
				 dst_width,
				 dst_width,
				 dst_height,
				 mode);
	}
	return -ENOSYS;
}


static int do_to_raw(const struct vdef_raw_format *format,
		     const uint8_t *src_y,
		     int src_stride_y,
		     const uint8_t *src_u,
		     int src_stride_u,
		     const uint8_t *src_v,
		     int src_stride_v,
		     uint8_t *dst,
		     int dst_stride,
		     int width,
		     int height)
{
	if (vdef_raw_format_cmp(format, &vdef_i420)) {
		return I420ToRAW(src_y,
				 src_stride_y,
				 src_u,
				 src_stride_u,
				 src_v,
				 src_stride_v,
				 dst,
				 dst_stride,
				 width,
				 height);
	} else if (vdef_raw_format_cmp(format, &vdef_nv12)) {
		return NV12ToRAW(src_y,
				 src_stride_y,
				 src_u,
				 src_stride_u,
				 dst,
				 dst_stride,
				 width,
				 height);
	} else if (vdef_raw_format_cmp(format, &vdef_nv21)) {
		return NV21ToRAW(src_y,
				 src_stride_y,
				 src_u,
				 src_stride_u,
				 dst,
				 dst_stride,
				 width,
				 height);
	}
	return -ENOSYS;
}


static void scale_frame(struct vscale_libyuv *self,
			struct mbuf_raw_video_frame *frame)
{
	struct vdef_raw_frame frame_info;
	unsigned int plane_count;
	unsigned int out_plane_count;
	const void *planes[3] = {0};
	int plane_ratio = 1;
	size_t offset = 0;
	struct mbuf_mem *mem = NULL;
	size_t len;
	struct timespec cur_ts;
	uint64_t ts_us;
	void *mem_data;
	uint8_t *dst;
	struct mbuf_raw_video_frame *out_frame = NULL;
	struct vdef_raw_frame out_frame_info;
	const unsigned int w = self->base->config.output.info.resolution.width;
	const unsigned int h = self->base->config.output.info.resolution.height;
	struct vdef_raw_format out_fmt;
	size_t mem_size;
	int res;
	uint8_t *scale_dst = NULL;
	const uint8_t *conv_src_y = NULL;
	const uint8_t *conv_src_u = NULL;
	const uint8_t *conv_src_v = NULL;
	size_t conv_src_stride_y = 0;
	size_t conv_src_stride_u = 0;
	size_t conv_src_stride_v = 0;
	bool scaling_needed = false;

	res = mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_get_frame_info", -res);
		goto end;
	}

	out_fmt = frame_info.format;
	if (vdef_is_raw_format_valid(
		    &self->base->config.output.preferred_format)) {
		out_fmt = self->base->config.output.preferred_format;
	}
	out_plane_count = vdef_get_raw_frame_plane_count(&out_fmt);
	out_frame_info = frame_info;

	out_frame_info.info.resolution.width = w;
	out_frame_info.info.resolution.height = h;
	memset(out_frame_info.plane_stride,
	       0,
	       sizeof(out_frame_info.plane_stride));
	out_frame_info.plane_stride[0] = w;
	out_frame_info.format = out_fmt;

	if (vdef_raw_format_cmp(&out_fmt, &vdef_i420)) {
		out_frame_info.plane_stride[1] = w / 2;
		out_frame_info.plane_stride[2] = w / 2;
	} else if (vdef_raw_format_cmp(&out_fmt, &vdef_nv12)) {
		out_frame_info.plane_stride[1] = w;
	} else if (vdef_raw_format_cmp(&out_fmt, &vdef_nv21)) {
		out_frame_info.plane_stride[1] = w;
	} else if (vdef_raw_format_cmp(&out_fmt, &vdef_rgb)) {
		out_frame_info.plane_stride[0] = w * 3;
	}
	res = mbuf_raw_video_frame_new(&out_frame_info, &out_frame);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_new", -res);
		goto end;
	}

	time_get_monotonic(&cur_ts);
	time_timespec_to_us(&cur_ts, &ts_us);
	res = mbuf_raw_video_frame_add_ancillary_buffer(
		out_frame,
		VSCALE_ANCILLARY_KEY_DEQUEUE_TIME,
		&ts_us,
		sizeof(ts_us));
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer",
				 -res);
		goto end;
	}

	mem_size = (vdef_raw_format_cmp(&out_fmt, &vdef_rgb))
			   ? (w * h * 3)
			   : ((w * h * 3) / 2);
	res = mbuf_mem_generic_new(mem_size, &mem);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_mem_generic_new", -res);
		goto end;
	}

	res = mbuf_mem_get_data(mem, &mem_data, &len);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_mem_get_data", -res);
		goto end;
	}
	dst = mem_data;

	plane_count = vdef_get_raw_frame_plane_count(&frame_info.format);

	for (unsigned int i = 0; i < plane_count; i++) {
		res = mbuf_raw_video_frame_get_plane(
			frame, i, &planes[i], &len);
		if (res < 0) {
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_get_plane",
					 -res);
			goto end;
		}
	}

	self->base->counters.pushed++;

	conv_src_y = (const uint8_t *)planes[0];
	conv_src_u = (const uint8_t *)planes[1];
	conv_src_v = (const uint8_t *)planes[2];
	conv_src_stride_y = frame_info.plane_stride[0];
	conv_src_stride_u = frame_info.plane_stride[1];
	conv_src_stride_v = frame_info.plane_stride[2];
	scaling_needed =
		!vdef_dim_cmp(&self->base->config.output.info.resolution,
			      &frame_info.info.resolution);

	if (vdef_raw_format_cmp(&frame_info.format, &out_fmt)) {
		/* Same format: scale directly to output */
		scale_dst = dst;
		plane_ratio =
			(vdef_raw_format_cmp(&out_fmt, &vdef_i420)) ? 4 : 2;
	} else if (vdef_raw_format_cmp(&out_fmt, &vdef_rgb)) {
		/* RGB output: scale to intermediate buffer if needed */
		if (scaling_needed) {
			size_t tmp_size = (w * h * 3) / 2;
			if (self->tmp_mem == NULL ||
			    self->tmp_mem_size < tmp_size) {
				if (self->tmp_mem != NULL)
					mbuf_mem_unref(self->tmp_mem);
				res = mbuf_mem_generic_new(tmp_size,
							   &self->tmp_mem);
				if (res < 0) {
					self->tmp_mem_size = 0;
					VSCALE_LOG_ERRNO("mbuf_mem_generic_new",
							 -res);
					goto end;
				}
				self->tmp_mem_size = tmp_size;
			}

			uint8_t *tmp_yuv_data = NULL;
			res = mbuf_mem_get_data(
				self->tmp_mem, (void **)&tmp_yuv_data, &len);
			if (res < 0) {
				VSCALE_LOG_ERRNO("mbuf_mem_get_data", -res);
				goto end;
			}
			scale_dst = tmp_yuv_data;
		}
	} else {
		res = -ENOSYS;
		VSCALE_LOGE("unsupported conversion path");
		goto end;
	}

	/* Perform scaling if required */
	if (scale_dst != NULL) {
		res = do_scale(&frame_info.format,
			       planes,
			       frame_info.plane_stride,
			       frame_info.info.resolution.width,
			       frame_info.info.resolution.height,
			       scale_dst,
			       w,
			       h,
			       self->libyuv_mode);
		if (res < 0) {
			VSCALE_LOG_ERRNO("do_scale", -res);
			goto end;
		}

		/* If we scaled to intermediate buffer, it becomes the source
		 * for conversion */
		if (scale_dst != dst) {
			conv_src_y = scale_dst;
			conv_src_u = scale_dst + w * h;
			conv_src_v = scale_dst + (w * h * 5) / 4;
			conv_src_stride_y = w;
			conv_src_stride_u =
				(vdef_raw_format_cmp(&frame_info.format,
						     &vdef_i420))
					? w / 2
					: w;
			conv_src_stride_v = w / 2;
		}
	}

	/* Perform conversion to RGB if required */
	if (vdef_raw_format_cmp(&out_fmt, &vdef_rgb)) {
		res = do_to_raw(&frame_info.format,
				conv_src_y,
				(int)conv_src_stride_y,
				conv_src_u,
				(int)conv_src_stride_u,
				conv_src_v,
				(int)conv_src_stride_v,
				dst,
				w * 3,
				w,
				h);
		if (res < 0) {
			VSCALE_LOG_ERRNO("do_to_raw", -res);
			goto end;
		}
	}

	self->base->counters.pulled++;

	for (unsigned int i = 0; i < out_plane_count; i++) {
		size_t len = (vdef_raw_format_cmp(&out_fmt, &vdef_rgb))
				     ? (w * h * 3)
				     : (i ? (w * h) / plane_ratio : (w * h));
		res = mbuf_raw_video_frame_set_plane(
			out_frame, i, mem, offset, len);
		if (res < 0) {
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_set_plane",
					 -res);
			goto end;
		}
		offset += len;
	}

	res = mbuf_raw_video_frame_foreach_ancillary_data(
		frame, mbuf_raw_video_frame_ancillary_data_copier, out_frame);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_foreach_ancillary_data",
				 -res);
		goto end;
	}

	struct vmeta_frame *metadata;
	res = mbuf_raw_video_frame_get_metadata(frame, &metadata);
	if (res == 0) {
		res = mbuf_raw_video_frame_set_metadata(out_frame, metadata);
		vmeta_frame_unref(metadata);
		if (res < 0) {
			VSCALE_LOG_ERRNO("mbuf_raw_video_frame_get_metadata",
					 -res);
			goto end;
		}
	} else if (res == -ENOENT) {
		/* No metadata, nothing to do */
	} else {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_get_metadata", -res);
		goto end;
	}

	time_get_monotonic(&cur_ts);
	time_timespec_to_us(&cur_ts, &ts_us);
	res = mbuf_raw_video_frame_add_ancillary_buffer(
		out_frame,
		VSCALE_ANCILLARY_KEY_OUTPUT_TIME,
		&ts_us,
		sizeof(ts_us));
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer",
				 -res);
		goto end;
	}

	res = mbuf_raw_video_frame_finalize(out_frame);
	if (res < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer",
				 -res);
		goto end;
	}

end:
	if (res == 0) {
		mbuf_raw_video_frame_queue_push(self->output_queue, out_frame);
		pomp_evt_signal(self->output_event);
	} else {
		pomp_evt_signal(self->error_event);
	}

	for (int i = 0; i < 3; i++) {
		if (planes[i])
			mbuf_raw_video_frame_release_plane(frame, i, planes[i]);
	}
	mbuf_raw_video_frame_unref(frame);
	if (out_frame)
		mbuf_raw_video_frame_unref(out_frame);
	if (mem)
		mbuf_mem_unref(mem);
}


static void *work_routine(void *userdata)
{
	struct vscale_libyuv *self = userdata;

#if defined(__APPLE__)
#	if !TARGET_OS_IPHONE
	int err = pthread_setname_np("vscale_libyuv");
	if (err != 0)
		VSCALE_LOG_ERRNO("pthread_setname_np", err);
#	endif
#else
	int err = pthread_setname_np(pthread_self(), "vscale_libyuv");
	if (err != 0)
		VSCALE_LOG_ERRNO("pthread_setname_np", err);
#endif

	pthread_mutex_lock(&self->mutex);
	while (true) {
		if (self->stop_flag) {
			self->stop_flag = false;
			pomp_evt_signal(self->output_event);
			pthread_mutex_unlock(&self->mutex);
			break;
		}

		if (self->flush_flag) {
			self->flush_flag = false;
			pomp_evt_signal(self->output_event);
			pthread_cond_wait(&self->cond, &self->mutex);
			continue;
		}

		struct mbuf_raw_video_frame *frame;
		int res = mbuf_raw_video_frame_queue_pop(self->input_queue,
							 &frame);
		if (res < 0) {
			if (res == -EAGAIN) {
				if (self->eos_flag) {
					self->eos_flag = false;
					pomp_evt_signal(self->output_event);
				}
			} else {
				VSCALE_LOG_ERRNO("mbuf_raw_video_frame_pop",
						 -res);
			}
			pthread_cond_wait(&self->cond, &self->mutex);
		} else {
			pthread_mutex_unlock(&self->mutex);
			scale_frame(self, frame);
			pthread_mutex_lock(&self->mutex);
		}
	}

	return NULL;
}


static int create(struct vscale_scaler *base)
{
	struct vscale_libyuv *self;
	int ret;

	self = calloc(1, sizeof(*self));
	if (self == NULL) {
		ret = -ENOMEM;
		VSCALE_LOG_ERRNO("calloc", -ret);
		return ret;
	}
	self->base = base;
	base->derived = self;

	VSCALE_LOGI("libyuv version=%d", LIBYUV_VERSION);

	pthread_mutex_init(&self->mutex, NULL);
	pthread_cond_init(&self->cond, NULL);
	self->state = RUNNING;

	ret = mbuf_raw_video_frame_queue_new_with_args(
		&(struct mbuf_raw_video_frame_queue_args){
			.filter = input_filter,
			.filter_userdata = self,
		},
		&self->input_queue);
	if (ret < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_new_with_args",
				 -ret);
		goto err;
	}

	ret = mbuf_raw_video_frame_queue_new(&self->output_queue);
	if (ret < 0) {
		VSCALE_LOG_ERRNO("mbuf_raw_video_frame_queue_new", -ret);
		goto err;
	}

	self->output_event = pomp_evt_new();
	if (self->output_event == NULL) {
		ret = -ENOMEM;
		VSCALE_LOG_ERRNO("pomp_evt_new", -ret);
		goto err;
	}

	ret = pomp_evt_attach_to_loop(
		self->output_event, base->loop, &output_evt_cb, self);
	if (ret < 0) {
		VSCALE_LOG_ERRNO("pomp_evt_attach_to_loop", -ret);
		goto err;
	}

	self->error_event = pomp_evt_new();
	if (self->error_event == NULL) {
		ret = -ENOMEM;
		VSCALE_LOG_ERRNO("pomp_evt_new", -ret);
		goto err;
	}

	ret = pomp_evt_attach_to_loop(
		self->error_event, base->loop, &error_evt_cb, self);
	if (ret < 0) {
		VSCALE_LOG_ERRNO("pomp_evt_attach_to_loop", -ret);
		goto err;
	}

	ret = pthread_create(&self->thread, NULL, &work_routine, self);
	if (ret != 0) {
		ret = -ret;
		VSCALE_LOG_ERRNO("pthread_create", ret);
		goto err;
	}

	self->thread_launched = true;

	self->libyuv_mode = HANDLED_FILTER_MODES[base->config.filter_mode];

	return 0;
err:
	destroy(self->base);
	base->derived = NULL;
	return ret;
}


static struct mbuf_pool *get_input_buffer_pool(const struct vscale_scaler *base)
{
	return NULL;
}


static struct mbuf_raw_video_frame_queue *
get_input_buffer_queue(const struct vscale_scaler *base)
{
	struct vscale_libyuv *scaler = base->derived;

	return scaler->input_queue;
}


VSCALE_API const struct vscale_ops vscale_libyuv_ops = {
	.get_supported_input_formats = get_supported_input_formats,
	.create = create,
	.flush = flush,
	.stop = stop,
	.destroy = destroy,
	.get_input_buffer_pool = get_input_buffer_pool,
	.get_input_buffer_queue = get_input_buffer_queue,
};
