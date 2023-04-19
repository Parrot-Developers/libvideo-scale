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

#ifndef _VSCALE_CORE_H
#define _VSCALE_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <media-buffers/mbuf_raw_video_frame.h>
#include <video-defs/vdefs.h>

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
 * mbuf ancillary data key for the input timestamp.
 *
 * Content is a 64bits microseconds value on a monotonic clock
 */
#define VSCALE_ANCILLARY_KEY_INPUT_TIME "vscale.input_time"
/**
 * mbuf ancillary data key for the dequeue timestamp.
 *
 * Content is a 64bits microseconds value on a monotonic clock
 */
#define VSCALE_ANCILLARY_KEY_DEQUEUE_TIME "vscale.dequeue_time"
/**
 * mbuf ancillary data key for the output timestamp.
 *
 * Content is a 64bits microseconds value on a monotonic clock
 */
#define VSCALE_ANCILLARY_KEY_OUTPUT_TIME "vscale.output_time"


/* Forward declarations */
struct vscale_scaler;


/* Supported scaling implementations */
enum vscale_scaler_implem {
	/* Automatically select scaler */
	VSCALE_SCALER_IMPLEM_AUTO = 0,

	/* 'libyuv' scaler implementation */
	VSCALE_SCALER_IMPLEM_LIBYUV,

	/* HiSilicon scaler implementation */
	VSCALE_SCALER_IMPLEM_HISI,

	/* Qualcomm scaler implementation */
	VSCALE_SCALER_IMPLEM_QCOM,
};


/* Filtering modes */
enum vscale_filter_mode {
	/* Automatically select depending on the implementation */
	VSCALE_FILTER_MODE_AUTO = 0,

	/* Point sample, fastest */ /* TODO */
	VSCALE_FILTER_MODE_NONE,

	/* Filter Horizontally */ /* TODO */
	VSCALE_FILTER_MODE_LINEAR,

	/* Box but lower quality scaling down */ /* TODO */
	VSCALE_FILTER_MODE_BILINEAR,

	/* Highest quality */ /* TODO */
	VSCALE_FILTER_MODE_BOX,
};


/* Scaler initial configuration, implementation specific extension
 * Each implementation might provide implementation specific configuration with
 * a structure compatible with this base structure (i.e. which starts with the
 * same implem field). */
struct vscale_config_impl {
	/* Scaler implementation for this extension */
	enum vscale_scaler_implem implem;
};


/* Scaler initial configuration */
struct vscale_config {
	/* Scaler instance name (optional, can be NULL, copied internally) */
	const char *name;

	/* Scaler implementation (AUTO means no preference,
	 * use the default implementation for the platform) */
	enum vscale_scaler_implem implem;

	/* Output filtering mode (0 means no preference, use the default
	 * value, if the given mode isn't handled, will fall back to a
	 * lower filtering mode */
	enum vscale_filter_mode filter_mode;

	/* Preferred scaling thread count (0 means no preference,
	 * use the default value; 1 means no multi-threading;
	 * only relevant for CPU scaling implementations) */
	uint32_t preferred_thread_count;

	/* Input configuration */
	struct {
		/* Input buffer pool preferred minimum buffer count, used
		 * only if the implementation uses its own input buffer pool
		 * (0 means no preference, use the default value) */
		size_t preferred_min_buf_count;

		/* Input buffers data format (mandatory) */
		struct vdef_raw_format format; /* TODO */

		/* Input format information (width and height are mandatory) */
		struct vdef_format_info info;
	} input;
	struct {
		/* Output buffer pool preferred minimum buffer count, used
		 * only if the implementation uses its own output buffer pool
		 * (0 means no preference, use the default value) */
		size_t preferred_min_buf_count;

		/* Preferred output buffers data format (optional,
		 * can be zero-filled) */
		struct vdef_raw_format preferred_format;

		/* Output format information (width and height are mandatory) */
		struct vdef_format_info info;
	} output;

	/* Implementation specific extensions (optional, can be NULL)
	 * If not null, implem_cfg must match the following requirements:
	 *  - this->implem_cfg->implem == this->implem
	 *  - this->implem != VSCALE_ENCODER_IMPLEM_AUTO
	 *  - The real type of implem_cfg must be the implementation specific
	 *    structure, not struct vscale_config_impl */
	struct vscale_config_impl *implem_cfg;
};


/* Scaler input buffer constraints */
struct vscale_input_buffer_constraints {
	/* Stride alignment values: these values are used to align the width of
	 * each plane in bytes */
	unsigned int plane_stride_align[VDEF_RAW_MAX_PLANE_COUNT];

	/* Scanline alignment values: these values are used to align the height
	 * of each plane in lines */
	unsigned int plane_scanline_align[VDEF_RAW_MAX_PLANE_COUNT];

	/* Size alignment values: these values are used to align the size of
	 * each plane to the upper size in bytes */
	unsigned int plane_size_align[VDEF_RAW_MAX_PLANE_COUNT];
};


/* Scaler callback functions */
struct vscale_cbs {
	/* Frame output callback function (mandatory)
	 * The library retains ownership of the output buffer and the
	 * application must reference it if needed after returning from the
	 * callback function. The status is 0 in case of success, a negative
	 * errno otherwise. In case of error no frame is output and frame
	 * is NULL.
	 * @param scaler: scaler instance handle
	 * @param status: frame output status
	 * @param frame: scaler output frame
	 * @param userdata: user data pointer */
	void (*frame_output)(struct vscale_scaler *scaler,
			     int status,
			     struct mbuf_raw_video_frame *frame,
			     void *userdata);

	/* Flush callback function, called when flushing is complete (optional)
	 * @param scaler: scaler instance handle
	 * @param userdata: user data pointer */
	void (*flush)(struct vscale_scaler *scaler, void *userdata);

	/* Stop callback function, called when stopping is complete (optional)
	 * @param scaler: scaler instance handle
	 * @param userdata: user data pointer */
	void (*stop)(struct vscale_scaler *scaler, void *userdata);
};


/**
 * Get an enum vscale_scaler_implem value from a string.
 * Valid strings are only the suffix of the implementation name (eg. 'LIBYUV').
 * The case is ignored.
 * @param str: implementation name to convert
 * @return the enum vscale_scaler_implem value or VSCALE_SCALER_IMPLEM_AUTO
 *         if unknown
 */
VSCALE_API enum vscale_scaler_implem
vscale_scaler_implem_from_str(const char *str);


/**
 * Get a string from an enum vscale_scaler_implem value.
 * @param implem: implementation value to convert
 * @return a string description of the implementation
 */
VSCALE_API const char *
vscale_scaler_implem_to_str(enum vscale_scaler_implem implem);


/**
 * Get an enum vscale_filter_mode value from a string.
 * Valid strings are only the suffix of the filter mode name (eg. 'LINEAR').
 * The case is ignored.
 * @param str: filter mode name to convert
 * @return the enum vscale_filter_mode value or VSCALE_FILTER_MODE_AUTO
 *         if unknown
 */
VSCALE_API enum vscale_filter_mode vscale_filter_mode_from_str(const char *str);


/**
 * Get a string from an enum vscale_filter_mode value.
 * @param mode: filter mode value to convert
 * @return a string description of the filter mode
 */
VSCALE_API const char *vscale_filter_mode_to_str(enum vscale_filter_mode mode);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_VSCALE_CORE_H */
