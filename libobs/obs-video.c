/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs.h"
#include "obs-internal.h"
#include "graphics/vec4.h"
#include "media-io/format-conversion.h"

static void tick_sources(uint64_t cur_time, uint64_t *last_time)
{
	size_t i;
	uint64_t delta_time;
	float seconds;

	if (!last_time)
		*last_time = cur_time - video_getframetime(obs->video.video);
	delta_time = cur_time - *last_time;
	seconds = (float)((double)delta_time / 1000000000.0);

	for (i = 0; i < obs->data.sources.num; i++)
		obs_source_video_tick(obs->data.sources.array[i], seconds);

	*last_time = cur_time;
}

static inline void render_display_begin(struct obs_display *display)
{
	struct vec4 clear_color;
	uint32_t width, height;

	gs_load_swapchain(display ? display->swap : NULL);

	gs_getsize(&width, &height);

	gs_beginscene();
	vec4_set(&clear_color, 0.3f, 0.0f, 0.0f, 1.0f);
	gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL,
			&clear_color, 1.0f, 0);

	gs_enable_depthtest(false);
	/* gs_enable_blending(false); */
	gs_setcullmode(GS_NEITHER);

	gs_ortho(0.0f, (float)obs->video.base_width,
			0.0f, (float)obs->video.base_height, -100.0f, 100.0f);
	gs_setviewport(0, 0, width, height);
}

static inline void render_display_end(struct obs_display *display)
{
	gs_endscene();
	gs_present();
}

static void render_display(struct obs_display *display)
{
	render_display_begin(display);

	for (size_t i = 0; i < MAX_CHANNELS; i++) {
		struct obs_source **p_source;

		p_source = (display) ? display->channels+i :
		                       obs->data.channels+i;

		if (*p_source) {
			if ((*p_source)->removed) {
				obs_source_release(*p_source);
				*p_source = NULL;
			} else {
				obs_source_video_render(*p_source);
			}
		}
	}

	render_display_end(display);
}

static inline void render_displays(void)
{
	if (!obs->data.valid)
		return;

	gs_entercontext(obs_graphics());

	/* render extra displays/swaps */
	pthread_mutex_lock(&obs->data.displays_mutex);

	for (size_t i = 0; i < obs->data.displays.num; i++)
		render_display(obs->data.displays.array[i]);

	pthread_mutex_unlock(&obs->data.displays_mutex);

	/* render main display */
	render_display(NULL);

	gs_leavecontext();
}

static inline void set_render_size(uint32_t width, uint32_t height)
{
	gs_enable_depthtest(false);
	gs_setcullmode(GS_NEITHER);

	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	gs_setviewport(0, 0, width, height);
}

static inline void render_channels(void)
{
	struct obs_core_data *data = &obs->data;

	for (size_t i = 0; i < MAX_CHANNELS; i++) {
		struct obs_source *source = data->channels[i];
		if (source)
			obs_source_video_render(source);
	}
}

static inline void unmap_last_surface(struct obs_core_video *video)
{
	if (video->mapped_surface) {
		stagesurface_unmap(video->mapped_surface);
		video->mapped_surface = NULL;
	}
}

static inline void render_main_texture(struct obs_core_video *video,
		int cur_texture, int prev_texture)
{
	struct vec4 clear_color;
	vec4_set(&clear_color, 0.3f, 0.0f, 0.0f, 1.0f);

	gs_setrendertarget(video->render_textures[cur_texture], NULL);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);

	set_render_size(video->base_width, video->base_height);
	render_channels();

	video->textures_rendered[cur_texture] = true;
}

static inline void render_output_texture(struct obs_core_video *video,
		int cur_texture, int prev_texture)
{
	texture_t   texture = video->render_textures[prev_texture];
	texture_t   target  = video->output_textures[cur_texture];
	uint32_t    width   = texture_getwidth(target);
	uint32_t    height  = texture_getheight(target);

	/* TODO: replace with actual downscalers or unpackers */
	effect_t    effect  = video->default_effect;
	technique_t tech    = effect_gettechnique(effect, "DrawMatrix");
	eparam_t    image   = effect_getparambyname(effect, "image");
	eparam_t    matrix  = effect_getparambyname(effect, "color_matrix");
	size_t      passes, i;

	if (!video->textures_rendered[prev_texture])
		return;

	gs_setrendertarget(target, NULL);
	set_render_size(width, height);

	/* TODO: replace with programmable code */
	const float mat_val[16] =
	{
		-0.100644f, -0.338572f,  0.439216f,  0.501961f,
		 0.182586f,  0.614231f,  0.062007f,  0.062745f,
		 0.439216f, -0.398942f, -0.040274f,  0.501961f,
		 0.000000f,  0.000000f,  0.000000f,  1.000000f
	};

	effect_setval(effect, matrix, mat_val, sizeof(mat_val));
	effect_settexture(effect, image, texture);

	passes = technique_begin(tech);
	for (i = 0; i < passes; i++) {
		technique_beginpass(tech, i);
		gs_draw_sprite(texture, 0, width, height);
		technique_endpass(tech);
	}
	technique_end(tech);

	video->textures_output[cur_texture] = true;
}

static inline void stage_output_texture(struct obs_core_video *video,
		int cur_texture, int prev_texture)
{
	texture_t   texture = video->output_textures[prev_texture];
	stagesurf_t copy    = video->copy_surfaces[cur_texture];

	unmap_last_surface(video);

	if (!video->textures_output[prev_texture])
		return;

	gs_stage_texture(copy, texture);

	video->textures_copied[cur_texture] = true;
}

static inline void render_video(struct obs_core_video *video, int cur_texture,
		int prev_texture)
{
	gs_beginscene();

	gs_enable_depthtest(false);
	gs_setcullmode(GS_NEITHER);

	render_main_texture(video, cur_texture, prev_texture);
	render_output_texture(video, cur_texture, prev_texture);
	stage_output_texture(video, cur_texture, prev_texture);

	gs_setrendertarget(NULL, NULL);

	gs_endscene();
}

/* TODO: replace with more optimal conversion */
static inline bool download_frame(struct obs_core_video *video, int cur_texture,
		int prev_texture, struct video_frame *frame)
{
	stagesurf_t surface = video->copy_surfaces[prev_texture];

	if (!video->textures_copied[prev_texture])
		return false;

	if (!stagesurface_map(surface, &frame->data[0], &frame->linesize[0]))
		return false;

	video->mapped_surface = surface;
	return true;
}

static bool convert_frame(struct obs_core_video *video,
		struct video_frame *frame,
		const struct video_output_info *info, int cur_texture)
{
	struct source_frame *new_frame = &video->convert_frames[cur_texture];

	if (info->format == VIDEO_FORMAT_I420) {
		compress_uyvx_to_i420(
				frame->data[0], frame->linesize[0],
				info->width, info->height,
				0, info->height,
				new_frame->data, new_frame->linesize);

	} else if (info->format == VIDEO_FORMAT_NV12) {
		compress_uyvx_to_nv12(
				frame->data[0], frame->linesize[0],
				info->width, info->height,
				0, info->height,
				new_frame->data, new_frame->linesize);

	} else {
		blog(LOG_WARNING, "convert_frame: unsupported texture format");
		return false;
	}

	for (size_t i = 0; i < MAX_VIDEO_PLANES; i++) {
		frame->data[i]     = new_frame->data[i];
		frame->linesize[i] = new_frame->linesize[i];
	}

	return true;
}

static inline void output_video_data(struct obs_core_video *video,
		struct video_frame *frame, int cur_texture)
{
	const struct video_output_info *info;
	info = video_output_getinfo(video->video);

	if (format_is_yuv(info->format))
		if (!convert_frame(video, frame, info, cur_texture))
			return;

	video_output_frame(video->video, frame);
}

static inline void output_frame(uint64_t timestamp)
{
	struct obs_core_video *video = &obs->video;
	int cur_texture  = video->cur_texture;
	int prev_texture = cur_texture == 0 ? NUM_TEXTURES-1 : cur_texture-1;
	struct video_frame frame;
	bool frame_ready;

	memset(&frame, 0, sizeof(struct video_frame));
	frame.timestamp = timestamp;

	gs_entercontext(obs_graphics());

	render_video(video, cur_texture, prev_texture);
	frame_ready = download_frame(video, cur_texture, prev_texture, &frame);

	gs_leavecontext();

	if (frame_ready)
		output_video_data(video, &frame, cur_texture);

	if (++video->cur_texture == NUM_TEXTURES)
		video->cur_texture = 0;
}

void *obs_video_thread(void *param)
{
	uint64_t last_time = 0;

	while (video_output_wait(obs->video.video)) {
		uint64_t cur_time = video_gettime(obs->video.video);

		tick_sources(cur_time, &last_time);

		render_displays();

		output_frame(cur_time);

	}

	return NULL;
}
