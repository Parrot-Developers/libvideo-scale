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

#include <strings.h>

#define ULOG_TAG vcsale_core
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include <video-scale/vscale_core.h>
#include <video-scale/vscale_internal.h>


enum vscale_scaler_implem vscale_scaler_implem_from_str(const char *str)
{
	if (strcasecmp(str, "LIBYUV") == 0) {
		return VSCALE_SCALER_IMPLEM_LIBYUV;
	} else if (strcasecmp(str, "HISI") == 0) {
		return VSCALE_SCALER_IMPLEM_HISI;
	} else if (strcasecmp(str, "QCOM") == 0) {
		return VSCALE_SCALER_IMPLEM_QCOM;
	} else {
		ULOGW("%s: unknown implementation '%s'", __func__, str);
		return VSCALE_SCALER_IMPLEM_AUTO;
	}
}


const char *vscale_scaler_implem_to_str(enum vscale_scaler_implem implem)
{
	switch (implem) {
	case VSCALE_SCALER_IMPLEM_AUTO:
		return "AUTO";
	case VSCALE_SCALER_IMPLEM_LIBYUV:
		return "LIBYUV";
	case VSCALE_SCALER_IMPLEM_HISI:
		return "HISI";
	case VSCALE_SCALER_IMPLEM_QCOM:
		return "QCOM";
	default:
		return "UNKNOWN";
	}
}


enum vscale_filter_mode vscale_filter_mode_from_str(const char *str)
{
	if (strcasecmp(str, "AUTO") == 0) {
		return VSCALE_FILTER_MODE_AUTO;
	} else if (strcasecmp(str, "NONE") == 0) {
		return VSCALE_FILTER_MODE_NONE;
	} else if (strcasecmp(str, "LINEAR") == 0) {
		return VSCALE_FILTER_MODE_LINEAR;
	} else if (strcasecmp(str, "BILINEAR") == 0) {
		return VSCALE_FILTER_MODE_BILINEAR;
	} else if (strcasecmp(str, "BOX") == 0) {
		return VSCALE_FILTER_MODE_BOX;
	} else {
		ULOGW("%s: unknown filter mode '%s'", __func__, str);
		return VSCALE_FILTER_MODE_AUTO;
	}
}


const char *vscale_filter_mode_to_str(enum vscale_filter_mode mode)
{
	switch (mode) {
	case VSCALE_FILTER_MODE_AUTO:
		return "AUTO";
	case VSCALE_FILTER_MODE_NONE:
		return "NONE";
	case VSCALE_FILTER_MODE_LINEAR:
		return "LINEAR";
	case VSCALE_FILTER_MODE_BILINEAR:
		return "BILINEAR";
	case VSCALE_FILTER_MODE_BOX:
		return "BOX";
	default:
		return "UNKNOWN";
	}
}


struct vscale_config_impl *
vscale_config_get_specific(struct vscale_config *config,
			   enum vscale_scaler_implem implem)
{
	/* Check if specific config is present */
	if (!config->implem_cfg)
		return NULL;

	/* Check if implementation is the right one */
	if (config->implem != implem) {
		ULOGI("specific config found, but implementation is %s "
		      "instead of %s. ignoring specific config",
		      vscale_scaler_implem_to_str(config->implem),
		      vscale_scaler_implem_to_str(implem));
		return NULL;
	}

	/* Check if specific config implementation matches the base one */
	if (config->implem_cfg->implem != config->implem) {
		ULOGW("specific config implem (%s) does not match"
		      " base config implem (%s). ignoring specific config",
		      vscale_scaler_implem_to_str(config->implem_cfg->implem),
		      vscale_scaler_implem_to_str(config->implem));
		return NULL;
	}

	/* All tests passed, return specific config */
	return config->implem_cfg;
}
