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

#include <libyuv/convert.h>
#include <libyuv/convert_from.h>
#include <libyuv/scale.h>

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

	uint8_t *src_uv;
	uint8_t *dst_uv;
};


#define NB_SUPPORTED_FORMATS 2
static struct vdef_raw_format supported_formats[NB_SUPPORTED_FORMATS];
static pthread_once_t supported_formats_is_init = PTHREAD_ONCE_INIT;
static void initialize_supported_formats(void)
{
	supported_formats[0] = vdef_i420;
	supported_formats[1] = vdef_nv12;
}


static const enum FilterMode HANDLED_FILTER_MODES[] = {
	[VSCALE_FILTER_MODE_AUTO] = kFilterBilinear,
	[VSCALE_FILTER_MODE_NONE] = kFilterNone,
	[VSCALE_FILTER_MODE_LINEAR] = kFilterLinear,
	[VSCALE_FILTER_MODE_BILINEAR] = kFilterBilinear,
	[VSCALE_FILTER_MODE_BOX] = kFilterBox,
};


static int time_monotonic_us(uint64_t *usec)
{
	struct timespec ts;
	int ret;

	ret = time_get_monotonic(&ts);
	if (ret < 0) {
		ULOG_ERRNO("time_get_monotonic", -ret);
		return ret;
	}
	ret = time_timespec_to_us(&ts, usec);
	if (ret < 0) {
		ULOG_ERRNO("time_timespec_to_us", -ret);
		return ret;
	}
	return 0;
}


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
					ULOG_ERRNO(
						"mbuf_raw_video_frame_queue_pop",
						-res);
				break;
			}

			self->base->cbs.frame_output(
				self->base, 0, frame, self->base->userdata);
			mbuf_raw_video_frame_unref(frame);
		}

		if (self->state == WAITING_FOR_EOS) {
			pthread_mutex_lock(&self->mutex);
			bool eos_flag = self->eos_flag;
			pthread_mutex_unlock(&self->mutex);

			if (!eos_flag) {
				self->state = RUNNING;
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
		pthread_mutex_unlock(&self->mutex);
		if (!stop_flag) {
			self->state = RUNNING;
			if (self->base->cbs.stop != NULL)
				self->base->cbs.stop(self->base,
						     self->base->userdata);
		}
		break;
	}
	case WAITING_FOR_FLUSH: {
		pthread_mutex_lock(&self->mutex);
		bool flush_flag = self->flush_flag;
		pthread_mutex_unlock(&self->mutex);
		if (!flush_flag) {
			self->state = RUNNING;
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
		pthread_cond_signal(&self->cond);
		pthread_mutex_unlock(&self->mutex);

		self->state = WAITING_FOR_FLUSH;
	} else {
		pthread_mutex_lock(&self->mutex);
		self->eos_flag = true;
		pthread_cond_signal(&self->cond);
		pthread_mutex_unlock(&self->mutex);

		self->state = WAITING_FOR_EOS;
	}

	return 0;
}


static int stop(struct vscale_scaler *base)
{
	struct vscale_libyuv *self = base->derived;

	pthread_mutex_lock(&self->mutex);
	self->stop_flag = true;
	pthread_cond_signal(&self->cond);
	pthread_mutex_unlock(&self->mutex);

	self->state = WAITING_FOR_STOP;

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
			ULOG_ERRNO("pthread_join", -ret);
	}

	pthread_mutex_destroy(&self->mutex);
	pthread_cond_destroy(&self->cond);
	free(self->dst_uv);
	free(self->src_uv);
	if (self->output_event != NULL) {
		if (pomp_evt_is_attached(self->output_event, base->loop)) {
			ret = pomp_evt_detach_from_loop(self->output_event,
							base->loop);
			if (ret < 0)
				ULOG_ERRNO("pomp_evt_detach_from_loop", -ret);
		}

		pomp_evt_destroy(self->output_event);
	}
	if (self->error_event != NULL) {
		if (pomp_evt_is_attached(self->error_event, base->loop)) {
			ret = pomp_evt_detach_from_loop(self->error_event,
							base->loop);
			if (ret < 0)
				ULOG_ERRNO("pomp_evt_detach_from_loop", -ret);
		}

		pomp_evt_destroy(self->error_event);
	}

	if (self->input_queue != 0) {
		ret = mbuf_raw_video_frame_queue_flush(self->input_queue);
		if (ret < 0)
			ULOG_ERRNO("mbuf_raw_video_frame_queue_flush", -ret);
		ret = mbuf_raw_video_frame_queue_destroy(self->input_queue);
		if (ret < 0)
			ULOG_ERRNO("mbuf_raw_video_frame_queue_destroy", -ret);
	}
	if (self->output_queue != 0) {
		ret = mbuf_raw_video_frame_queue_flush(self->output_queue);
		if (ret < 0)
			ULOG_ERRNO("mbuf_raw_video_frame_queue_flush", -ret);
		ret = mbuf_raw_video_frame_queue_destroy(self->output_queue);
		if (ret < 0)
			ULOG_ERRNO("mbuf_raw_video_frame_queue_destroy", -ret);
	}

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


static void scale_frame(struct vscale_libyuv *self,
			struct mbuf_raw_video_frame *frame)
{
	struct vdef_raw_frame frame_info;
	unsigned int plane_count;
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
	unsigned int w;
	unsigned int h;

	int res = mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_get_frame_info", -res);
		goto end;
	}

	out_frame_info = frame_info;

	w = self->base->config.output.info.resolution.width;
	h = self->base->config.output.info.resolution.height;
	out_frame_info.info.resolution.width = w;
	out_frame_info.info.resolution.height = h;
	out_frame_info.plane_stride[0] = w;

	if (vdef_raw_format_cmp(&frame_info.format, &vdef_i420)) {
		out_frame_info.plane_stride[1] = w / 2;
		out_frame_info.plane_stride[2] = w / 2;
	} else if (vdef_raw_format_cmp(&frame_info.format, &vdef_nv12)) {
		out_frame_info.plane_stride[1] = w;
	}
	res = mbuf_raw_video_frame_new(&out_frame_info, &out_frame);
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_new", -res);
		goto end;
		return;
	}

	time_get_monotonic(&cur_ts);
	time_timespec_to_us(&cur_ts, &ts_us);
	res = mbuf_raw_video_frame_add_ancillary_buffer(
		out_frame,
		VSCALE_ANCILLARY_KEY_DEQUEUE_TIME,
		&ts_us,
		sizeof(ts_us));
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer", -res);
		goto end;
	}

	res = mbuf_mem_generic_new((w * h * 3) / 2, &mem);
	if (res < 0) {
		ULOG_ERRNO("mbuf_mem_generic_new", -res);
		goto end;
	}

	res = mbuf_mem_get_data(mem, &mem_data, &len);
	if (res < 0) {
		ULOG_ERRNO("mbuf_mem_get_data", -res);
		goto end;
	}
	dst = mem_data;

	plane_count = vdef_get_raw_frame_plane_count(&frame_info.format);

	for (unsigned int i = 0; i < plane_count; i++) {
		res = mbuf_raw_video_frame_get_plane(
			frame, i, &planes[i], &len);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_get_plane", -res);
			goto end;
		}
	}

	if (vdef_raw_format_cmp(&frame_info.format, &vdef_i420)) {
		plane_ratio = 4;

		res = I420Scale(planes[0],
				frame_info.plane_stride[0],
				planes[1],
				frame_info.plane_stride[1],
				planes[2],
				frame_info.plane_stride[2],
				frame_info.info.resolution.width,
				frame_info.info.resolution.height,
				dst,
				w,
				dst + w * h,
				w / 2,
				dst + (w * h * 5) / 4,
				w / 2,
				w,
				h,
				self->libyuv_mode);

		if (res < 0) {
			ULOG_ERRNO("I420Scale", -res);
			goto end;
		}
	} else if (vdef_raw_format_cmp(&frame_info.format, &vdef_nv12)) {
		plane_ratio = 2;
		unsigned int src_w = frame_info.info.resolution.width;
		unsigned int src_h = frame_info.info.resolution.height;

		res = NV12ToI420(NULL,
				 0,
				 planes[1],
				 frame_info.plane_stride[1],
				 NULL,
				 0,
				 self->src_uv,
				 src_w / 2,
				 self->src_uv + (src_w * src_h) / 4,
				 src_w / 2,
				 src_w,
				 src_h);

		if (res < 0) {
			ULOG_ERRNO("NV12ToI420", -res);
			goto end;
		}

		res = I420Scale(planes[0],
				frame_info.plane_stride[0],
				self->src_uv,
				src_w / 2,
				self->src_uv + (src_w * src_h) / 4,
				src_w / 2,
				src_w,
				src_h,
				dst,
				w,
				self->dst_uv,
				w / 2,
				self->dst_uv + (w * h) / 4,
				w / 2,
				w,
				h,
				self->libyuv_mode);

		if (res < 0) {
			ULOG_ERRNO("I420Scale", -res);
			goto end;
		}

		res = I420ToNV21(NULL,
				 0,
				 self->dst_uv,
				 w / 2,
				 self->dst_uv + (w * h) / 4,
				 w / 2,
				 NULL,
				 0,
				 dst + w * h,
				 w,
				 w,
				 h);

		if (res < 0) {
			ULOG_ERRNO("I420ToNV12", -res);
			goto end;
		}
	}

	for (unsigned int i = 0; i < plane_count; i++) {
		size_t len = i ? (w * h) / plane_ratio : (w * h);
		res = mbuf_raw_video_frame_set_plane(
			out_frame, i, mem, offset, len);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_set_plane", -res);
			goto end;
		}
		offset += len;
	}

	res = mbuf_raw_video_frame_foreach_ancillary_data(
		frame, mbuf_raw_video_frame_ancillary_data_copier, out_frame);
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_foreach_ancillary_data", -res);
		goto end;
	}

	struct vmeta_frame *metadata;
	res = mbuf_raw_video_frame_get_metadata(frame, &metadata);
	if (res == 0) {
		res = mbuf_raw_video_frame_set_metadata(out_frame, metadata);
		vmeta_frame_unref(metadata);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_get_metadata", -res);
			goto end;
		}
	} else if (res == -ENOENT) {
		/* No metadata, nothing to do */
		res = 0;
	} else {
		ULOG_ERRNO("mbuf_raw_video_frame_get_metadata", -res);
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
		ULOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer", -res);
		goto end;
	}

	res = mbuf_raw_video_frame_finalize(out_frame);
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_add_ancillary_buffer", -res);
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
				ULOG_ERRNO("mbuf_raw_video_frame_pop", -res);
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
		ULOG_ERRNO("calloc", -ret);
		return ret;
	}
	self->base = base;
	base->derived = self;

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
		ULOG_ERRNO("mbuf_raw_video_frame_queue_new_with_args", -ret);
		goto err;
	}

	ret = mbuf_raw_video_frame_queue_new(&self->output_queue);
	if (ret < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_queue_new", -ret);
		goto err;
	}

	self->output_event = pomp_evt_new();
	if (self->output_event == NULL) {
		ret = -ENOMEM;
		ULOG_ERRNO("pomp_evt_new", -ret);
		goto err;
	}

	ret = pomp_evt_attach_to_loop(
		self->output_event, base->loop, &output_evt_cb, self);
	if (ret < 0) {
		ULOG_ERRNO("pomp_evt_attach_to_loop", -ret);
		goto err;
	}

	self->error_event = pomp_evt_new();
	if (self->error_event == NULL) {
		ret = -ENOMEM;
		ULOG_ERRNO("pomp_evt_new", -ret);
		goto err;
	}

	ret = pomp_evt_attach_to_loop(
		self->error_event, base->loop, &error_evt_cb, self);
	if (ret < 0) {
		ULOG_ERRNO("pomp_evt_attach_to_loop", -ret);
		goto err;
	}

	if (vdef_raw_format_cmp(&base->config.input.format, &vdef_nv12)) {
		self->src_uv =
			malloc((base->config.input.info.resolution.width *
				base->config.input.info.resolution.height) /
			       2);
		if (self->src_uv == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		self->dst_uv =
			malloc((base->config.output.info.resolution.width *
				base->config.output.info.resolution.height) /
			       2);
		if (self->dst_uv == NULL) {
			ret = -ENOMEM;
			goto err;
		}
	}

	ret = pthread_create(&self->thread, NULL, &work_routine, self);
	if (ret != 0) {
		ret = -ret;
		ULOG_ERRNO("pthread_create", ret);
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
