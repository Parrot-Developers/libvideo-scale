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

#ifndef _VSCALE_H
#define _VSCALE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libpomp.h>
#include <media-buffers/mbuf_raw_video_frame.h>
#include <video-defs/vdefs.h>
#include <video-scale/vscale_core.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* To be used for all public API */
#ifdef VSCALE_API_EXPORTS
#	ifdef _WIN32
#		define VSCALE_API __declspec(dllexport)
#	else /* !_WIN32 */
#		define VSCALE_API __attribute__((visibility("default")))
#	endif /* !_WIN32 */
#else /* !VSCALE_API_EXPORTS */
#	define VSCALE_API
#endif /* !VSCALE_API_EXPORTS */


/**
 * Get the supported input buffer data formats for the given
 * scaler implementation.
 * Each implementation supports at least one input format,
 * and optionally more. All input buffers need to be in one of
 * the supported formats, otherwise they will be discarded.
 * The returned formats array is a static array whose size is the return value
 * of this function. If this function returns an error (negative errno value),
 * then the value of *formats is undefined.
 * @param implem: decoder implementation
 * @param formats: pointer to the supported formats list (output)
 * @return the size of the formats array, or a negative errno on error.
 */
VSCALE_API int
vscale_get_supported_input_formats(enum vscale_scaler_implem implem,
				   const struct vdef_raw_format **formats);


/**
 * Create a scaler instance.
 * The configuration and callbacks structures must be filled.
 * The instance handle is returned through the ret_obj parameter.
 * When no longer needed, the instance must be freed using the
 * scaler_destroy() function.
 * @param loop: event loop to use
 * @param config: scaler configuration
 * @param cbs: scaler callback functions
 * @param userdata: callback functions user data (optional, can be null)
 * @param ret_obj: scaler instance handle (output)
 * @return 0 on success, negative errno value in case of error
 */
VSCALE_API int vscale_new(struct pomp_loop *loop,
			  const struct vscale_config *config,
			  const struct vscale_cbs *cbs,
			  void *userdata,
			  struct vscale_scaler **ret_obj);


/**
 * Flush the scaler.
 * This function flushes all queues and optionally discards all buffers
 * retained by the scaler. If the buffers are not discarded the frame
 * output callback is called for each frame when the scaling is complete.
 * The function is asynchronous and returns immediately. When flushing is
 * complete the flush callback function is called if defined. After flushing
 * the scaler new input buffers can still be queued.
 * @param self: scaler instance handle
 * @param discard: if false, all pending buffers are output, otherwise they
 *        are discarded
 * @return 0 on success, negative errno value in case of error
 */
VSCALE_API int vscale_flush(struct vscale_scaler *self, bool discard);


/**
 * Stop the scaler.
 * This function stops any running threads. The function is asynchronous
 * and returns immediately. When stopping is complete the stop callback
 * function is called if defined. After stopping the scaler no new input
 * buffers can be queued and the scaler instance must be freed using the
 * vscale_destroy() function.
 * @param self: scaler instance handle
 * @return 0 on success, negative errno value in case of error
 */
VSCALE_API int vscale_stop(struct vscale_scaler *self);


/**
 * Free a scaler instance.
 * This function frees all resources associated with a scaler instance.
 * @note this function blocks until all internal threads (if any) can be
 * joined; therefore the application should call vscale_stop() and wait for
 * the stop callback function to be called before calling vscale_destroy().
 * @param self: scaler instance handle
 * @return 0 on success, negative errno value in case of error
 */
VSCALE_API int vscale_destroy(struct vscale_scaler *self);


/**
 * Get the input buffer pool.
 * The input buffer pool is defined only for implementations that require
 * using input buffers from the scaler's own pool. This function must
 * be called prior to scaling and if the returned value is not NULL the
 * input buffer pool must be used to get input buffers. If the input
 * buffers provided are not originating from the pool, they will be copied
 * resulting in a loss of performance.
 * @param self: scaler instance handle
 * @return a pointer on the input buffer pool on success, NULL in case of
 * error of if no pool is used
 */
VSCALE_API struct mbuf_pool *
vscale_get_input_buffer_pool(struct vscale_scaler *self);


/**
 * Get the input buffer queue.
 * This function must be called prior to scaling and the input
 * buffer queue must be used to push input buffers for scaling.
 * @param self: scaler instance handle
 * @return a pointer on the input buffer queue on success, NULL in case of error
 */
VSCALE_API struct mbuf_raw_video_frame_queue *
vscale_get_input_buffer_queue(struct vscale_scaler *self);


/**
 * Get the scaler implementation used.
 * @param self: scaler instance handle
 * @return the scaler implementation used, or VSCALE_SCALER_IMPLEM_AUTO
 * in case of error
 */
VSCALE_API enum vscale_scaler_implem
vscale_get_used_implem(struct vscale_scaler *self);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_VSCALE_PRIV_H_ */
