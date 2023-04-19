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

#define ULOG_TAG vscale_prog
#include <ulog.h>
ULOG_DECLARE_TAG(ULOG_TAG);

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <futils/futils.h>
#include <libpomp.h>
#include <media-buffers/mbuf_mem_generic.h>
#include <media-buffers/mbuf_raw_video_frame.h>
#include <video-raw/vraw.h>
#include <video-scale/vscale.h>


struct vscale_prog {
	bool stopped;
	bool finishing;
	bool input_finished;

	struct vscale_scaler *scaler;

	struct {
		struct vraw_reader *reader;
		int count;
	} in;

	struct {
		struct vraw_writer *writer;
		const char *file;
		int count;
		unsigned int width;
		unsigned int height;
	} out;
};


atomic_bool s_stopping;
struct pomp_loop *s_loop;
struct vscale_prog *s_prog;


static void finish_idle(void *userdata)
{
	int res;
	struct vscale_prog *self = userdata;

	ULOG_ERRNO_RETURN_IF(self == NULL, EINVAL);

	if (self->finishing)
		return;

	if ((atomic_load(&s_stopping)) || (self->input_finished)) {
		self->finishing = true;

		/* Flush the scaler */
		res = vscale_flush(self->scaler, atomic_load(&s_stopping));
		if (res < 0)
			ULOG_ERRNO("vscale_flush", -res);
	}
}


static void scale_frame_idle(void *userdata)
{
	int res;
	struct vscale_prog *self = userdata;

	if (self == NULL || self->finishing)
		return;

	while (!atomic_load(&s_stopping) && !self->input_finished) {
		struct mbuf_mem *mem = 0;
		void *data;
		size_t size;
		struct vraw_frame raw_frame;
		struct mbuf_raw_video_frame *frame = 0;
		size_t plane_size[VDEF_RAW_MAX_PLANE_COUNT];
		int plane_count;
		struct mbuf_pool *pool = NULL;

		if (self->in.count == 0) {
			self->input_finished = true;
			break;
		}

		pool = vscale_get_input_buffer_pool(self->scaler);
		if (pool) {
			res = mbuf_pool_get(pool, &mem);
			if (res < 0)
				goto next;
		} else {
			ssize_t size =
				vraw_reader_get_min_buf_size(self->in.reader);
			if (size < 0) {
				res = size;
				ULOG_ERRNO("vraw_reader_get_min_buf_size",
					   -res);
				goto next;
			}
			res = mbuf_mem_generic_new(size, &mem);
			if (res < 0) {
				ULOG_ERRNO("mbuf_mem_generic_new", -res);
				goto next;
			}
		}
		res = mbuf_mem_get_data(mem, &data, &size);
		if (res < 0) {
			ULOG_ERRNO("mbuf_mem_get_data", -res);
			goto next;
		}

		res = vraw_reader_frame_read(
			self->in.reader, data, size, &raw_frame);
		if (res == -ENOENT) {
			self->input_finished = true;
			mbuf_mem_unref(mem);
			break;
		} else if (res < 0) {
			ULOG_ERRNO("vraw_reader_frame_read", -res);
			goto next;
		}

		res = mbuf_raw_video_frame_new(&raw_frame.frame, &frame);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_new", -res);
			goto next;
		}

		res = vdef_calc_raw_frame_size(&raw_frame.frame.format,
					       &raw_frame.frame.info.resolution,
					       NULL,
					       NULL,
					       NULL,
					       NULL,
					       plane_size,
					       NULL);
		if (res < 0) {
			ULOG_ERRNO("vdef_calc_raw_frame_size", -res);
			goto next;
		}

		plane_count =
			vdef_get_raw_frame_plane_count(&raw_frame.frame.format);
		for (int i = 0; i < plane_count; i++) {
			res = mbuf_raw_video_frame_set_plane(
				frame,
				i,
				mem,
				raw_frame.cdata[i] - (uint8_t *)data,
				plane_size[i]);
			if (res < 0) {
				ULOG_ERRNO("mbuf_raw_video_frame_set_plane",
					   -res);
				goto next;
			}
		}

		res = mbuf_raw_video_frame_finalize(frame);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_finalize", -res);
			goto next;
		}

		res = mbuf_raw_video_frame_queue_push(
			vscale_get_input_buffer_queue(self->scaler), frame);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_queue_push", -res);
			goto next;
		}

		if (self->in.count > 0)
			self->in.count -= 1;

		/* clang-format off */
next:
		/* clang-format on */
		if (frame)
			mbuf_raw_video_frame_unref(frame);
		if (mem)
			mbuf_mem_unref(mem);
		if (res < 0)
			break;
	}

	if (self->input_finished) {
		res = pomp_loop_idle_add(s_loop, &finish_idle, self);
		if (res < 0)
			ULOG_ERRNO("pomp_loop_idle_add", -res);
	} else if (!atomic_load(&s_stopping)) {
		res = pomp_loop_idle_add(s_loop, &scale_frame_idle, self);
		if (res < 0)
			ULOG_ERRNO("pomp_loop_idle_add", -res);
	}
}


static int is_suffix(const char *suffix, const char *s)
{
	size_t suffix_len = strlen(suffix);
	size_t s_len = strlen(s);

	return s_len >= suffix_len &&
	       (strcasecmp(suffix, &s[s_len - suffix_len]) == 0);
}


static uint64_t get_timestamp(struct mbuf_raw_video_frame *frame,
			      const char *key)
{
	int res;
	struct mbuf_ancillary_data *data;
	uint64_t ts = 0;
	const void *raw_data;
	size_t len;

	res = mbuf_raw_video_frame_get_ancillary_data(frame, key, &data);
	if (res < 0)
		return 0;

	raw_data = mbuf_ancillary_data_get_buffer(data, &len);
	if (!raw_data || len != sizeof(ts))
		goto out;
	memcpy(&ts, raw_data, sizeof(ts));

out:
	mbuf_ancillary_data_unref(data);
	return ts;
}


static void frame_output_cb(struct vscale_scaler *scaler,
			    int status,
			    struct mbuf_raw_video_frame *frame,
			    void *userdata)
{
	int res;
	struct vscale_prog *self = userdata;
	struct vdef_raw_frame frame_info;

	res = mbuf_raw_video_frame_get_frame_info(frame, &frame_info);
	if (res < 0) {
		ULOG_ERRNO("mbuf_raw_video_frame_get_frame_info", -res);
		return;
	}

	struct vraw_frame raw_frame = {
		.frame = frame_info,
	};

	int plane_count =
		vdef_get_raw_frame_plane_count(&raw_frame.frame.format);

	int i;
	for (i = 0; i < plane_count; i++) {
		size_t len;
		res = mbuf_raw_video_frame_get_plane(
			frame, i, (const void **)&raw_frame.cdata[i], &len);
		if (res < 0) {
			ULOG_ERRNO("mbuf_raw_video_frame_get_plane", -res);
			goto out;
		}
	}

	if (!self->out.writer) {
		struct vraw_writer_config writer_cfg = {
			.y4m = is_suffix(".y4m", self->out.file),
			.format = frame_info.format,
			.info = /* Codecheck */
			{
				.resolution = /* Codecheck */
				{
					.width = self->out.width,
					.height = self->out.height,
				},
			},
		};

		res = vraw_writer_new(
			self->out.file, &writer_cfg, &self->out.writer);
		if (res < 0) {
			ULOG_ERRNO("vraw_writer_new", -res);
			goto out;
		}
	}

	res = vraw_writer_frame_write(self->out.writer, &raw_frame);
	if (res < 0) {
		ULOG_ERRNO("vraw_writer_frame_write", -res);
		goto out;
	}
	self->out.count += 1;

	{
		uint64_t input_time =
			get_timestamp(frame, VSCALE_ANCILLARY_KEY_INPUT_TIME);
		uint64_t dequeue_time =
			get_timestamp(frame, VSCALE_ANCILLARY_KEY_DEQUEUE_TIME);
		uint64_t output_time =
			get_timestamp(frame, VSCALE_ANCILLARY_KEY_OUTPUT_TIME);

		ULOGI("scaled frame #%u (dequeue: %.2f ms, scale: %.2f ms"
		      " overall: %.2f ms)",
		      frame_info.info.index,
		      (float)(dequeue_time - input_time) / 1000.,
		      (float)(output_time - dequeue_time) / 1000.,
		      (float)(output_time - input_time) / 1000.);
	}
out:
	for (i--; 0 <= i; i--)
		mbuf_raw_video_frame_release_plane(
			frame, i, raw_frame.cdata[i]);

	/* handle input frame */
	scale_frame_idle(self);
}


static void flush_cb(struct vscale_scaler *scaler, void *userdata)
{
	int res;
	struct vscale_prog *self = userdata;

	ULOGI("scaler is flushed");

	res = vscale_stop(self->scaler);
	if (res < 0)
		ULOG_ERRNO("vscale_stop", -res);
}


static void stop_cb(struct vscale_scaler *scaler, void *userdata)
{
	struct vscale_prog *self = userdata;

	ULOGI("scaler is stopped");
	self->stopped = true;

	int res = pomp_loop_wakeup(s_loop);
	if (res < 0)
		ULOG_ERRNO("pomp_loop_wakeup", -res);
}


static void sighandler(int sig)
{
	atomic_store(&s_stopping, true);

	printf("Stopping...\n");
	if (s_loop) {
		ULOGI("scaling interrupted");
		int res = pomp_loop_idle_add(s_loop, &finish_idle, s_prog);
		if (res < 0)
			ULOG_ERRNO("pomp_loop_idle_add", -res);
		res = pomp_loop_wakeup(s_loop);
		if (res < 0)
			ULOG_ERRNO("pomp_loop_wakeup", -res);
	}
	signal(SIGINT, SIG_DFL);
}


enum args_id {
	ARGS_ID_IMPLEM = 256,
};


static const char short_options[] = "hi:o:n:f:m:";


static const struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"implem", required_argument, NULL, ARGS_ID_IMPLEM},
	{"input", required_argument, NULL, 'i'},
	{"output", required_argument, NULL, 'o'},
	{"count", required_argument, NULL, 'n'},
	{"format", required_argument, NULL, 'f'},
	{"mode", required_argument, NULL, 'm'},
	{0, 0, 0, 0},
};


static void welcome(char *prog_name)
{
	printf("\n%s - Video scaling program\n"
	       "Copyright (c) 2019 Parrot Drones SAS\n\n",
	       prog_name);
}


static void usage(char *prog_name)
{
	/* clang-format off */
	printf("Usage: %s [options] <input_file> <output_file>\n\n"
	       "Options:\n"
	       "  -h | --help                        "
		       "Print this message\n"
	       "       --implem <implem_name>        "
		       "Force the implementation to use "
		       "(optional, defaults to AUTO)\n"
	       "  -i | --input <width>x<height>      "
		       "Input dimensions in pixels "
		       "(mandatory, unless input is *.y4m; "
		       "ignored in that case)\n"
	       "  -o | --output <width>x<height>     "
		       "Output dimensions in pixels (mandatory)\n"
	       "  -n | --count <n>                   "
		       "Scale at most n frames\n"
	       "  -f | --format <format>             "
		       "Data format (\"I420\", \"NV12\" or \"NV21\"; "
		       "mandatory, unless input is *.y4m; "
		       "ignored in that case)\n"
	       "  -m | --mode <mode>                 "
		       "Filtering mode (\"AUTO\", \"NONE\", \"LINEAR\", "
		       "\"BILINEAR\" or \"BOX\"; optional, defaults to AUTO)\n"
	       "\n",
	       prog_name);
	/* clang-format on */
}


static uint64_t time_us(void)
{
	struct timespec x;
	time_get_monotonic(&x);
	uint64_t y;
	time_timespec_to_us(&x, &y);

	return y;
}


int main(int argc, char **argv)
{
	int res = 0;
	int c;
	int idx;
	int nb_formats;
	uint64_t end_time;
	uint64_t start_time;
	const char *input_file;
	struct vraw_reader_config reader_cfg;
	const struct vdef_raw_format *formats;
	struct vscale_config scaler_cfg = {0};
	struct vscale_input_buffer_constraints constraints;

	atomic_init(&s_stopping, false);

	s_prog = calloc(1, sizeof(*s_prog));
	if (s_prog == NULL) {
		ULOG_ERRNO("calloc", ENOMEM);
		exit(EXIT_FAILURE);
	}
	s_prog->in.count = -1;

	welcome(argv[0]);

	while ((c = getopt_long(
			argc, argv, short_options, long_options, &idx)) != -1) {
		switch (c) {
		case 0:
			break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);

		case ARGS_ID_IMPLEM:
			scaler_cfg.implem =
				vscale_scaler_implem_from_str(optarg);
			break;

		case 'i':
			sscanf(optarg,
			       "%ux%u",
			       &scaler_cfg.input.info.resolution.width,
			       &scaler_cfg.input.info.resolution.height);
			break;

		case 'o':
			sscanf(optarg,
			       "%ux%u",
			       &s_prog->out.width,
			       &s_prog->out.height);
			break;

		case 'n':
			sscanf(optarg, "%d", &s_prog->in.count);
			break;

		case 'f':
			if (strcmp(optarg, "I420") == 0)
				scaler_cfg.input.format = vdef_i420;
			else if (strcmp(optarg, "YV12") == 0)
				scaler_cfg.input.format = vdef_yv12;
			else if (strcmp(optarg, "NV12") == 0)
				scaler_cfg.input.format = vdef_nv12;
			else if (strcmp(optarg, "NV21") == 0)
				scaler_cfg.input.format = vdef_nv21;
			break;

		case 'm':
			scaler_cfg.filter_mode =
				vscale_filter_mode_from_str(optarg);
			break;

		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	scaler_cfg.output.info.resolution = (struct vdef_dim){
		.width = s_prog->out.width,
		.height = s_prog->out.height,
	};

	if (argc - optind < 2) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = argv[optind];
	s_prog->out.file = argv[optind + 1];

	s_loop = pomp_loop_new();
	if (!s_loop) {
		res = -ENOMEM;
		ULOG_ERRNO("pomp_loop_new", ENOMEM);
		goto out;
	}

	memset(&reader_cfg, 0, sizeof(reader_cfg));
	reader_cfg.format = scaler_cfg.input.format;
	reader_cfg.info = scaler_cfg.input.info;
	reader_cfg.y4m = is_suffix(".y4m", input_file);

	res = vscale_get_input_buffer_constraints(
		scaler_cfg.implem, &reader_cfg.format, &constraints);
	if (res < 0) {
		ULOG_ERRNO("vscale_get_input_buffer_constraints", -res);
		goto out;
	} else {
		unsigned int plane_count =
			vdef_get_raw_frame_plane_count(&reader_cfg.format);
		memcpy(reader_cfg.plane_stride_align,
		       constraints.plane_stride_align,
		       plane_count * sizeof(*constraints.plane_stride_align));
		memcpy(reader_cfg.plane_scanline_align,
		       constraints.plane_scanline_align,
		       plane_count * sizeof(*constraints.plane_scanline_align));
		memcpy(reader_cfg.plane_size_align,
		       constraints.plane_size_align,
		       plane_count * sizeof(*constraints.plane_size_align));
	}

	res = vraw_reader_new(input_file, &reader_cfg, &s_prog->in.reader);
	if (res < 0) {
		ULOG_ERRNO("vraw_reader_new", -res);
		goto out;
	}
	res = vraw_reader_get_config(s_prog->in.reader, &reader_cfg);
	if (res < 0) {
		ULOG_ERRNO("vraw_reader_get_config", -res);
		goto out;
	}
	scaler_cfg.input.format = reader_cfg.format;
	scaler_cfg.input.info.resolution = reader_cfg.info.resolution;

	printf("Scaling file '%s' to file '%s'\n"
	       "Input: %ux%u\n"
	       "Output: %ux%u\n"
	       "Filter mode: %s\n\n",
	       input_file,
	       s_prog->out.file,
	       scaler_cfg.input.info.resolution.width,
	       scaler_cfg.input.info.resolution.height,
	       scaler_cfg.output.info.resolution.width,
	       scaler_cfg.output.info.resolution.height,
	       vscale_filter_mode_to_str(scaler_cfg.filter_mode));

	nb_formats =
		vscale_get_supported_input_formats(scaler_cfg.implem, &formats);
	if (nb_formats < 0) {
		res = nb_formats;
		ULOG_ERRNO("vscale_get_supported_input_formats", -res);
		goto out;
	}
	if (!vdef_raw_format_intersect(
		    &scaler_cfg.input.format, formats, nb_formats)) {
		res = -EINVAL;
		ULOG_ERRNO(
			"unsupported format:"
			" " VDEF_RAW_FORMAT_TO_STR_FMT,
			ENOSYS,
			VDEF_RAW_FORMAT_TO_STR_ARG(&scaler_cfg.input.format));
		goto out;
	}
	res = vscale_new(s_loop,
			 &scaler_cfg,
			 &(struct vscale_cbs){
				 .frame_output = frame_output_cb,
				 .flush = flush_cb,
				 .stop = stop_cb,
			 },
			 s_prog,
			 &s_prog->scaler);
	if (res < 0) {
		ULOG_ERRNO("vscale_new", -res);
		goto out;
	}

	signal(SIGINT, sighandler);

	start_time = time_us();

	res = pomp_loop_idle_add(s_loop, &scale_frame_idle, s_prog);
	if (res < 0) {
		ULOG_ERRNO("pomp_loop_idle_add", -res);
		goto out;
	}

	while (!s_prog->stopped)
		pomp_loop_wait_and_process(s_loop, -1);

	end_time = time_us();

	printf("\nOverall time: %.2fs / %.2ffps\n",
	       (float)(end_time - start_time) / 1000000.,
	       s_prog->out.count * 1000000. / (float)(end_time - start_time));
out:
	if (s_prog) {
		vraw_writer_destroy(s_prog->out.writer);
		if (s_prog->scaler)
			vscale_destroy(s_prog->scaler);
		if (s_loop)
			pomp_loop_destroy(s_loop);
		vraw_reader_destroy(s_prog->in.reader);
		free(s_prog);
	}

	int status = (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
	printf("\n%s\n", (status == EXIT_SUCCESS) ? "Done!" : "Failed!");
	exit(status);
}
