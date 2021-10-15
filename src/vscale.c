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

#define ULOG_TAG vscale
#include "vscale_priv.h"
ULOG_DECLARE_TAG(ULOG_TAG);


static const struct vscale_ops *implem_ops(enum vscale_scaler_implem implem)
{
	switch (implem) {

#ifdef BUILD_LIBVIDEO_SCALE_LIBYUV
	case VSCALE_SCALER_IMPLEM_LIBYUV:
		return &vscale_libyuv_ops;
#endif /* BUILD_LIBVIDEO_SCALE_LIBYUV */

#ifdef BUILD_LIBVIDEO_SCALE_HISI
	case VSCALE_SCALER_IMPLEM_HISI:
		return &vscale_hisi_ops;
#endif /* BUILD_LIBVIDEO_SCALE_HISI */

	default:
		return NULL;
	}
}


static int vscale_get_implem(enum vscale_scaler_implem *implem)
{
	ULOG_ERRNO_RETURN_ERR_IF(implem == NULL, EINVAL);

#ifdef BUILD_LIBVIDEO_SCALE_LIBYUV
	if ((*implem == VSCALE_SCALER_IMPLEM_AUTO) ||
	    (*implem == VSCALE_SCALER_IMPLEM_LIBYUV)) {
		*implem = VSCALE_SCALER_IMPLEM_LIBYUV;
		return 0;
	}
#endif /* BUILD_LIBVIDEO_SCALE_LIBYUV */

#ifdef BUILD_LIBVIDEO_SCALE_HISI
	if ((*implem == VSCALE_SCALER_IMPLEM_AUTO) ||
	    (*implem == VSCALE_SCALER_IMPLEM_HISI)) {
		*implem = VSCALE_SCALER_IMPLEM_HISI;
		return 0;
	}
#endif /* BUILD_LIBVIDEO_SCALE_HISI */

	return -ENOSYS;
}


int vscale_get_supported_input_formats(enum vscale_scaler_implem implem,
				       const struct vdef_raw_format **formats)
{
	int ret;

	ULOG_ERRNO_RETURN_ERR_IF(!formats, EINVAL);

	ret = vscale_get_implem(&implem);
	ULOG_ERRNO_RETURN_VAL_IF(ret < 0, -ret, 0);

	return implem_ops(implem)->get_supported_input_formats(formats);
}


int vscale_new(struct pomp_loop *loop,
	       const struct vscale_config *config,
	       const struct vscale_cbs *cbs,
	       void *userdata,
	       struct vscale_scaler **ret_obj)
{
	int ret;
	struct vscale_scaler *self;

	ULOG_ERRNO_RETURN_ERR_IF(loop == NULL, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(config == NULL, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(cbs == NULL, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(cbs->frame_output == NULL, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(ret_obj == NULL, EINVAL);

	self = calloc(1, sizeof(*self));
	if (self == NULL) {
		ret = -ENOMEM;
		ULOG_ERRNO("calloc", -ret);
		return ret;
	}
	self->loop = loop;
	self->cbs = *cbs;
	self->userdata = userdata;
	self->config = *config;
	self->last_timestamp = UINT64_MAX;
	if (config->name) {
		self->config.name = strdup(config->name);
		if (self->config.name == NULL) {
			ret = -ENOMEM;
			ULOG_ERRNO("strdup", -ret);
			goto error;
		}
	}

	ret = vscale_get_implem(&self->config.implem);
	if (ret < 0) {
		if (ret == -ENOSYS)
			ULOGE("%s: no implementation found", __func__);
		goto error;
	}

	self->ops = implem_ops(self->config.implem);
	if (self->ops->get_supported_input_formats == NULL ||
	    self->ops->create == NULL || self->ops->flush == NULL ||
	    self->ops->stop == NULL || self->ops->destroy == NULL ||
	    self->ops->get_input_buffer_pool == NULL ||
	    self->ops->get_input_buffer_queue == NULL) {
		ULOGE("%s: incomplete implementation", __func__);
		ret = -EPROTO;
		goto error;
	}

	if (vdef_dim_is_null(&self->config.input.info.resolution) ||
	    vdef_dim_is_null(&self->config.output.info.resolution)) {
		ULOGE("invalid input or output dimensions: %ux%u -> %ux%u",
		      self->config.input.info.resolution.width,
		      self->config.input.info.resolution.height,
		      self->config.output.info.resolution.width,
		      self->config.output.info.resolution.height);
		ret = -EINVAL;
		goto error;
	}

	ret = self->ops->create(self);
	if (ret < 0)
		goto error;

	*ret_obj = self;
	return 0;

error:
	vscale_destroy(self);
	*ret_obj = NULL;
	return ret;
}


int vscale_flush(struct vscale_scaler *self, bool discard)
{
	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);

	return self->ops->flush(self, discard);
}


int vscale_stop(struct vscale_scaler *self)
{
	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);

	return self->ops->stop(self);
}


int vscale_destroy(struct vscale_scaler *self)
{
	int ret = 0;

	ULOG_ERRNO_RETURN_ERR_IF(self == NULL, EINVAL);

	if (self->derived)
		ret = self->ops->destroy(self);

	if (ret == 0) {
		free((void *)self->config.name);
		free(self);
	}

	return ret;
}


struct mbuf_pool *vscale_get_input_buffer_pool(struct vscale_scaler *self)
{
	ULOG_ERRNO_RETURN_VAL_IF(self == NULL, EINVAL, NULL);

	return self->ops->get_input_buffer_pool(self);
}


struct mbuf_raw_video_frame_queue *
vscale_get_input_buffer_queue(struct vscale_scaler *self)
{
	ULOG_ERRNO_RETURN_VAL_IF(self == NULL, EINVAL, NULL);

	return self->ops->get_input_buffer_queue(self);
}


enum vscale_scaler_implem vscale_get_used_implem(struct vscale_scaler *self)
{
	ULOG_ERRNO_RETURN_VAL_IF(
		self == NULL, EINVAL, VSCALE_SCALER_IMPLEM_AUTO);

	return self->config.implem;
}
