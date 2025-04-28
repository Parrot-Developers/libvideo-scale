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
#ifdef __cplusplus
#	include <atomic>
/* codecheck_ignore[SPACING] */
using std::atomic_uint;
#else
#	include <stdatomic.h>
#endif

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


/* Specific logging functions : log the instance ID before the log message */
#define VSCALE_LOG_INT(_pri, _fmt, ...)                                        \
	do {                                                                   \
		char *prefix = (self != NULL && self->base != NULL)            \
				       ? self->base->scaler_name               \
				       : "";                                   \
		ULOG_PRI(_pri,                                                 \
			 "%s%s" _fmt,                                          \
			 prefix != NULL ? prefix : "",                         \
			 prefix != NULL ? ": " : "",                           \
			 ##__VA_ARGS__);                                       \
	} while (0)
#define VSCALE_LOGD(_fmt, ...) VSCALE_LOG_INT(ULOG_DEBUG, _fmt, ##__VA_ARGS__)
#define VSCALE_LOGI(_fmt, ...) VSCALE_LOG_INT(ULOG_INFO, _fmt, ##__VA_ARGS__)
#define VSCALE_LOGW(_fmt, ...) VSCALE_LOG_INT(ULOG_WARN, _fmt, ##__VA_ARGS__)
#define VSCALE_LOGE(_fmt, ...) VSCALE_LOG_INT(ULOG_ERR, _fmt, ##__VA_ARGS__)
#define VSCALE_LOG_ERRNO(_fmt, _err, ...)                                      \
	do {                                                                   \
		char *prefix = (self != NULL && self->base != NULL)            \
				       ? self->base->scaler_name               \
				       : "";                                   \
		ULOGE_ERRNO((_err),                                            \
			    "%s%s" _fmt,                                       \
			    prefix != NULL ? prefix : "",                      \
			    prefix != NULL ? ": " : "",                        \
			    ##__VA_ARGS__);                                    \
	} while (0)
#define VSCALE_LOGW_ERRNO(_fmt, _err, ...)                                     \
	do {                                                                   \
		char *prefix = (self != NULL && self->base != NULL)            \
				       ? self->base->scaler_name               \
				       : "";                                   \
		ULOGW_ERRNO((_err),                                            \
			    "%s%s" _fmt,                                       \
			    prefix != NULL ? prefix : "",                      \
			    prefix != NULL ? ": " : "",                        \
			    ##__VA_ARGS__);                                    \
	} while (0)
#define VSCALE_LOG_ERRNO_RETURN_IF(_cond, _err)                                \
	do {                                                                   \
		if (ULOG_UNLIKELY(_cond)) {                                    \
			VSCALE_LOG_ERRNO("", (_err));                          \
			return;                                                \
		}                                                              \
	} while (0)
#define VSCALE_LOG_ERRNO_RETURN_ERR_IF(_cond, _err)                            \
	do {                                                                   \
		if (ULOG_UNLIKELY(_cond)) {                                    \
			int __pdraw_errno__err = (_err);                       \
			VSCALE_LOG_ERRNO("", (__pdraw_errno__err));            \
			return -(__pdraw_errno__err);                          \
		}                                                              \
	} while (0)
#define VSCALE_LOG_ERRNO_RETURN_VAL_IF(_cond, _err, _val)                      \
	do {                                                                   \
		if (ULOG_UNLIKELY(_cond)) {                                    \
			VSCALE_LOG_ERRNO("", (_err));                          \
			/* codecheck_ignore[RETURN_PARENTHESES] */             \
			return (_val);                                         \
		}                                                              \
	} while (0)


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

	/**
	 * Get the input buffer constraints (optional).
	 * The caller must provide a constraints structure to fill.
	 * If the implementation does not have any input buffers constraints,
	 * the pointer can be NULL.
	 * @param format: input format used within the scaler
	 * @param constraints: pointer to a vscale_input_buffer_constraints
	 * structure (output)
	 * @return 0 on success, negative errno value in case of error
	 */
	int (*get_input_buffer_constraints)(
		const struct vdef_raw_format *format,
		struct vscale_input_buffer_constraints *constraints);
};


struct vscale_scaler {
	/* Reserved */
	struct vscale_scaler *base;
	void *derived;
	const struct vscale_ops *ops;
	struct pomp_loop *loop;
	struct vscale_cbs cbs;
	void *userdata;
	struct vscale_config config;
	uint64_t last_timestamp;

	int scaler_id;
	char *scaler_name;

	struct {
		/* Frames that have passed the input filter */
		atomic_uint in;
		/* Frames that have been pushed to the scaler */
		atomic_uint pushed;
		/* Frames that have been pulled from the scaler */
		atomic_uint pulled;
		/* Frames that have been output (frame_output) */
		atomic_uint out;
	} counters;
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
