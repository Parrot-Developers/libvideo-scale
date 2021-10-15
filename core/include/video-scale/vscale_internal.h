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

#ifndef _VSCALE_INTERNAL_H
#define _VSCALE_INTERNAL_H

#include <inttypes.h>

#include <video-scale/vscale_core.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* To be used for all public API */
#ifdef VSCALE_API_EXPORTS
#	ifdef _WIN32
#		define VSCALE_INTERNAL_API __declspec(dllexport)
#	else /* !_WIN32 */
#		define VSCALE_INTERNAL_API                                    \
			__attribute__((visibility("default")))
#	endif /* !_WIN32 */
#else /* !VSCALE_API_EXPORTS */
#	define VSCALE_INTERNAL_API
#endif /* !VSCALE_API_EXPORTS */


struct vscale_ops {
	/**
	 * Get the supported input buffer data formats for the implementation.
	 * Each implementation supports at least one input format, and
	 * optionally more. All input buffers need to be in one of the supported
	 * formats, otherwise they will be discarded. The returned formats array
	 * is a static array whose size is the return value of this function.
	 * If this function returns an error (negative errno value), then the
	 * value of *formats is undefined.
	 * @param formats: pointer to the supported formats list (output)
	 * @return the size of the formats array, or a negative errno on error.
	 */
	int (*get_supported_input_formats)(
		const struct vdef_raw_format **formats);

	/**
	 * Create an scaler implementation instance.
	 * When no longer needed, the instance must be freed using the
	 * destroy() function.
	 * @param base: base instance
	 * @return 0 on success, negative errno value in case of error
	 */
	int (*create)(struct vscale_scaler *base);

	/**
	 * Flush the scaler implementation.
	 * This function flushes all queues and optionally discards all buffers
	 * retained by the scaler. If the buffers are not discarded, the
	 * frame output callback is called for each frame when the scaling
	 * is complete. The function is asynchronous and returns immediately.
	 * When flushing is complete the flush callback function is called if
	 * defined. After flushing the scaler new input buffers can still
	 * be queued.
	 * @param base: base instance
	 * @param discard: if false, all pending buffers are output, otherwise
	 *                 they are discarded
	 * @return 0 on success, negative errno value in case of error
	 */
	int (*flush)(struct vscale_scaler *base, bool discard);

	/**
	 * Stop the scaler implementation.
	 * This function stops any running threads. The function is asynchronous
	 * and returns immediately. When stopping is complete the stop callback
	 * function is called if defined. After stopping the scaler no new
	 * input buffers can be queued and the scaler instance must be freed
	 * using the destroy() function.
	 * @param base: base instance
	 * @return 0 on success, negative errno value in case of error
	 */
	int (*stop)(struct vscale_scaler *base);

	/**
	 * Free an scaler implementation instance.
	 * This function frees all resources associated with a scaler
	 * implementation instance.
	 * @note this function blocks until all internal threads (if any)
	 * can be joined
	 * @param base: base instance
	 * @return 0 on success, negative errno value in case of error
	 */
	int (*destroy)(struct vscale_scaler *base);

	/**
	 * Get the input buffer pool.
	 * The input buffer pool is defined only for implementations that
	 * require using input buffers from the scaler's own pool. This
	 * function must be called prior to scaling and if the returned
	 * value is not NULL the input buffer pool should be used to get input
	 * buffers. If the input buffers provided are not originating from the
	 * pool, they will be copied resulting in a loss of performance.
	 * @param base: base instance
	 * @return a pointer on the input buffer pool on success, NULL in case
	 * of error of if no pool is used
	 */
	struct mbuf_pool *(*get_input_buffer_pool)(
		const struct vscale_scaler *base);

	/**
	 * Get the input buffer queue.
	 * This function must be called prior to scaling and the input
	 * buffer queue must be used to push input buffers for scaling.
	 * @param base: base instance
	 * @return a pointer on the input buffer queue on success, NULL in case
	 * of error
	 */
	struct mbuf_raw_video_frame_queue *(*get_input_buffer_queue)(
		const struct vscale_scaler *base);
};


struct vscale_scaler {
	void *derived;
	const struct vscale_ops *ops;
	struct pomp_loop *loop;
	struct vscale_cbs cbs;
	void *userdata;
	struct vscale_config config;
	uint64_t last_timestamp;
};

/**
 * Default filter for the input frame queue.
 * This function is intended to be used as a standalone input filter.
 * It will call vscale_default_input_filter_internal(), and then
 * vscale_default_input_filter_internal_confirm_frame() if the former returned
 * true.
 *
 * @param frame: The frame to filter.
 * @param userdata: The venc_encoder structure.
 *
 * @return true if the frame passes the checks, false otherwise
 */
VSCALE_API bool vscale_default_input_filter(struct mbuf_raw_video_frame *frame,
					    void *userdata);

/**
 * Default filter for the input frame queue.
 * This filter does the following checks:
 * - frame is in a supported format
 * - frame info matches input config
 * - frame timestamp is strictly monotonic
 * This version is intended to be used by custom filters, to avoid calls to
 * mbuf_raw_video_frame_get_frame_info() or get_supported_input_formats().
 *
 * @warning This function does NOT check input validity. Arguments must not be
 * NULL, except for supported_formats if nb_supported_formats is zero.
 *
 * @param scaler: The base video scaler.
 * @param frame: The frame to filter.
 * @param frame_info: The associated vdef_raw_frame.
 * @param supported_formats: The formats supported by the implementation.
 * @param nb_supported_formats: The size of the supported_formats array.
 *
 * @return true if the frame passes the checks, false otherwise
 */
VSCALE_API bool vscale_default_input_filter_internal(
	struct vscale_scaler *scaler,
	struct mbuf_raw_video_frame *frame,
	struct vdef_raw_frame *frame_info,
	const struct vdef_raw_format *supported_formats,
	unsigned int nb_supported_formats);

/**
 * Filter update function.
 * This function should be called at the end of a custom filter. It registers
 * that the frame was accepted. This function saves the frame timestamp for
 * monotonic checks, and sets the VSCALE_ANCILLARY_KEY_INPUT_TIME ancillary data
 * on the frame.
 *
 * @param scaler: The base video scaler.
 * @param frame: The accepted frame.
 * @param frame_info: The associated vdef_raw_frame.
 */
VSCALE_API void vscale_default_input_filter_internal_confirm_frame(
	struct vscale_scaler *scaler,
	struct mbuf_raw_video_frame *frame,
	struct vdef_raw_frame *frame_info);

VSCALE_API struct vscale_config_impl *
vscale_config_get_specific(struct vscale_config *config,
			   enum vscale_scaler_implem implem);


#endif /* !_VSCALE_CORE_H */
