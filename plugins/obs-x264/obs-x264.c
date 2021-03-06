/******************************************************************************
    Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

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

#include <util/dstr.h>
#include <util/darray.h>
#include <obs.h>
#include <x264.h>

struct obs_x264 {
	obs_encoder_t   encoder;

	x264_param_t    params;
	x264_t          *context;

	DARRAY(uint8_t) packet_data;

	uint8_t         *extra_data;
	uint8_t         *sei;

	size_t          extra_data_size;
	size_t          sei_size;
};


/* ------------------------------------------------------------------------- */

static const char *obs_x264_getname(const char *locale)
{
	/* TODO locale lookup */
	UNUSED_PARAMETER(locale);
	return "x264";
}

static void obs_x264_stop(void *data);

static void clear_data(struct obs_x264 *obsx264)
{
	if (obsx264->context) {
		x264_encoder_close(obsx264->context);
		bfree(obsx264->sei);
		bfree(obsx264->extra_data);

		obsx264->context    = NULL;
		obsx264->sei        = NULL;
		obsx264->extra_data = NULL;
	}
}

static void obs_x264_destroy(void *data)
{
	struct obs_x264 *obsx264 = data;

	if (obsx264) {
		clear_data(obsx264);
		da_free(obsx264->packet_data);
		bfree(obsx264);
	}
}

static void obs_x264_defaults(obs_data_t settings)
{
	obs_data_set_default_int   (settings, "bitrate",     1000);
	obs_data_set_default_int   (settings, "buffer_size", 1000);
	obs_data_set_default_int   (settings, "keyint_sec",  0);
	obs_data_set_default_int   (settings, "crf",         23);
	obs_data_set_default_bool  (settings, "cbr",         false);

	obs_data_set_default_string(settings, "preset",      "veryfast");
	obs_data_set_default_string(settings, "profile",     "");
	obs_data_set_default_string(settings, "tune",        "");
	obs_data_set_default_string(settings, "x264opts",    "");
}

static inline void add_strings(obs_property_t list, const char *const *strings)
{
	while (*strings) {
		obs_property_list_add_string(list, *strings, *strings);
		strings++;
	}
}

static obs_properties_t obs_x264_props(const char *locale)
{
	/* TODO: locale */

	obs_properties_t props = obs_properties_create(locale);
	obs_property_t list;

	obs_properties_add_int(props, "bitrate", "Bitrate", 50, 100000, 1);
	obs_properties_add_int(props, "buffer_size", "Buffer Size", 50, 100000,
			1);
	obs_properties_add_int(props,
			"keyint_sec", "Keyframe interval (seconds, 0=auto)",
			0, 20, 1);

	list = obs_properties_add_list(props,
			"preset", "CPU Usage Preset (encoder speed)",
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	add_strings(list, x264_preset_names);

	list = obs_properties_add_list(props, "profile", "Profile",
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, "baseline", "baseline");
	obs_property_list_add_string(list, "main", "main");
	obs_property_list_add_string(list, "high", "high");

	list = obs_properties_add_list(props, "tune", "Tune",
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	add_strings(list, x264_tune_names);

	obs_properties_add_text(props, "x264opts",
			"x264 encoder options (separated by ':')",
			OBS_TEXT_DEFAULT);

	return props;
}

static bool getparam(const char *param, char **name, const char **value)
{
	const char *assign;

	if (!param || !*param || (*param == '='))
		return false;

	assign = strchr(param, '=');
	if (!assign || !*assign || !*(assign+1))
		return false;

	*name  = bstrdup_n(param, assign-param);
	*value = assign+1;
	return true;
}

static void override_base_param(const char *param,
		char **preset, char **profile, char **tune)
{
	char       *name;
	const char *val;

	if (getparam(param, &name, &val)) {
		if (astrcmpi(name, "preset") == 0) {
			bfree(*preset);
			*preset = bstrdup(val);

		} else if (astrcmpi(name, "profile") == 0) {
			bfree(*profile);
			*profile = bstrdup(val);

		} else if (astrcmpi(name, "tune") == 0) {
			bfree(*tune);
			*tune = bstrdup(val);
		}

		bfree(name);
	}
}

static inline void override_base_params(char **params,
		char **preset, char **profile, char **tune)
{
	while (*params)
		override_base_param(*(params++), preset, profile, tune);
}

static inline void set_param(struct obs_x264 *obsx264, const char *param)
{
	char       *name;
	const char *val;

	if (getparam(param, &name, &val)) {
		if (x264_param_parse(&obsx264->params, name, val) != 0)
			blog(LOG_WARNING, "x264 param: %s failed", param);

		bfree(name);
	}
}

static inline void apply_x264_profile(struct obs_x264 *obsx264,
		const char *profile)
{
	if (!obsx264->context && profile) {
		int ret = x264_param_apply_profile(&obsx264->params, profile);
		if (ret != 0)
			blog(LOG_WARNING, "Failed to set x264 "
					"profile '%s'", profile);
	}
}

static bool reset_x264_params(struct obs_x264 *obsx264,
		const char *preset, const char *tune)
{
	return x264_param_default_preset(&obsx264->params, preset, tune) == 0;
}

static void log_x264(void *param, int level, const char *format, va_list args)
{
	blogva(LOG_INFO, format, args);

	UNUSED_PARAMETER(param);
	UNUSED_PARAMETER(level);
}

static void update_params(struct obs_x264 *obsx264, obs_data_t settings,
		char **params)
{
	video_t video = obs_encoder_video(obsx264->encoder);
	const struct video_output_info *voi = video_output_getinfo(video);

	int bitrate      = (int)obs_data_getint(settings, "bitrate");
	int buffer_size  = (int)obs_data_getint(settings, "buffer_size");
	int keyint_sec   = (int)obs_data_getint(settings, "keyint_sec");
	int crf          = (int)obs_data_getint(settings, "crf");
	bool cbr         = obs_data_getbool(settings, "cbr");

	if (keyint_sec)
		obsx264->params.i_keyint_max =
			keyint_sec * voi->fps_num / voi->fps_den;

	obsx264->params.b_vfr_input          = false;
	obsx264->params.rc.i_vbv_max_bitrate = bitrate;
	obsx264->params.rc.i_vbv_buffer_size = buffer_size;
	obsx264->params.rc.i_bitrate         = bitrate;
	obsx264->params.i_width              = voi->width;
	obsx264->params.i_height             = voi->height;
	obsx264->params.i_fps_num            = voi->fps_num;
	obsx264->params.i_fps_den            = voi->fps_den;
	obsx264->params.pf_log               = log_x264;
	obsx264->params.i_log_level          = X264_LOG_WARNING;

	/* use the new filler method for CBR to allow real-time adjusting of
	 * the bitrate */
	if (cbr) {
		obsx264->params.rc.b_filler      = true;
		obsx264->params.rc.f_rf_constant = 0.0f;
		obsx264->params.rc.i_rc_method   = X264_RC_ABR;
	} else {
		obsx264->params.rc.i_rc_method   = X264_RC_CRF;
		obsx264->params.rc.f_rf_constant = (float)crf;
	}

	if (voi->format == VIDEO_FORMAT_NV12)
		obsx264->params.i_csp = X264_CSP_NV12;
	else if (voi->format == VIDEO_FORMAT_I420)
		obsx264->params.i_csp = X264_CSP_I420;
	else
		obsx264->params.i_csp = X264_CSP_NV12;

	while (*params)
		set_param(obsx264, *(params++));
}

static bool update_settings(struct obs_x264 *obsx264, obs_data_t settings)
{
	char *preset     = bstrdup(obs_data_getstring(settings, "preset"));
	char *profile    = bstrdup(obs_data_getstring(settings, "profile"));
	char *tune       = bstrdup(obs_data_getstring(settings, "tune"));
	const char *opts = obs_data_getstring(settings, "x264opts");

	char **paramlist;
	bool success = true;

	paramlist = strlist_split(opts, ':', false);

	if (!obsx264->context) {
		override_base_params(paramlist, &preset, &tune, &profile);
		success = reset_x264_params(obsx264, preset, tune);
	}

	if (success) {
		update_params(obsx264, settings, paramlist);

		if (!obsx264->context)
			apply_x264_profile(obsx264, profile);
	}

	obsx264->params.b_repeat_headers = false;

	strlist_free(paramlist);
	bfree(preset);
	bfree(profile);
	bfree(tune);

	return success;
}

static bool obs_x264_update(void *data, obs_data_t settings)
{
	struct obs_x264 *obsx264 = data;
	bool success = update_settings(obsx264, settings);
	int ret;

	if (success) {
		ret = x264_encoder_reconfig(obsx264->context, &obsx264->params);
		if (ret != 0)
			blog(LOG_WARNING, "Failed to reconfigure x264: %d",
					ret);
		return ret == 0;
	}

	return false;
}

static void load_headers(struct obs_x264 *obsx264)
{
	x264_nal_t      *nals;
	int             nal_count;
	DARRAY(uint8_t) header;
	DARRAY(uint8_t) sei;

	da_init(header);
	da_init(sei);

	x264_encoder_headers(obsx264->context, &nals, &nal_count);

	for (int i = 0; i < nal_count; i++) {
		x264_nal_t *nal = nals+i;

		if (nal->i_type == NAL_SEI)
			da_push_back_array(sei, nal->p_payload, nal->i_payload);
		else
			da_push_back_array(header, nal->p_payload,
					nal->i_payload);
	}

	obsx264->extra_data      = header.array;
	obsx264->extra_data_size = header.num;
	obsx264->sei             = sei.array;
	obsx264->sei_size        = sei.num;
}

static void *obs_x264_create(obs_data_t settings, obs_encoder_t encoder)
{
	struct obs_x264 *obsx264 = bzalloc(sizeof(struct obs_x264));
	obsx264->encoder = encoder;

	if (update_settings(obsx264, settings)) {
		obsx264->context = x264_encoder_open(&obsx264->params);

		if (obsx264->context == NULL)
			blog(LOG_WARNING, "x264 failed to load");
		else
			load_headers(obsx264);
	} else {
		blog(LOG_WARNING, "bad settings specified for x264");
	}

	if (!obsx264->context) {
		bfree(obsx264);
		return NULL;
	}

	return obsx264;
}

static void parse_packet(struct obs_x264 *obsx264,
		struct encoder_packet *packet, x264_nal_t *nals,
		int nal_count, x264_picture_t *pic_out)
{
	if (!nal_count) return;

	da_resize(obsx264->packet_data, 0);

	for (int i = 0; i < nal_count; i++) {
		x264_nal_t *nal = nals+i;
		da_push_back_array(obsx264->packet_data, nal->p_payload,
				nal->i_payload);
	}

	packet->data          = obsx264->packet_data.array;
	packet->size          = obsx264->packet_data.num;
	packet->type          = OBS_ENCODER_VIDEO;
	packet->pts           = pic_out->i_pts;
	packet->dts           = pic_out->i_dts;
	packet->keyframe      = pic_out->b_keyframe != 0;
}

static inline void init_pic_data(struct obs_x264 *obsx264, x264_picture_t *pic,
		struct encoder_frame *frame)
{
	x264_picture_init(pic);

	pic->i_pts = frame->pts;
	pic->img.i_csp = obsx264->params.i_csp;

	if (obsx264->params.i_csp == X264_CSP_NV12)
		pic->img.i_plane = 2;
	else if (obsx264->params.i_csp == X264_CSP_I420)
		pic->img.i_plane = 3;

	for (int i = 0; i < pic->img.i_plane; i++) {
		pic->img.i_stride[i] = (int)frame->linesize[i];
		pic->img.plane[i]    = frame->data[i];
	}
}

static bool obs_x264_encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{
	struct obs_x264 *obsx264 = data;
	x264_nal_t      *nals;
	int             nal_count;
	int             ret;
	x264_picture_t  pic, pic_out;

	if (!frame || !packet || !received_packet)
		return false;

	if (frame)
		init_pic_data(obsx264, &pic, frame);

	ret = x264_encoder_encode(obsx264->context, &nals, &nal_count,
			(frame ? &pic : NULL), &pic_out);
	if (ret < 0) {
		blog(LOG_WARNING, "x264 encode failed");
		return false;
	}

	*received_packet = (nal_count != 0);
	parse_packet(obsx264, packet, nals, nal_count, &pic_out);

	return true;
}

static bool obs_x264_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	struct obs_x264 *obsx264 = data;

	if (!obsx264->context)
		return false;

	*extra_data = obsx264->extra_data;
	*size       = obsx264->extra_data_size;
	return true;
}

static bool obs_x264_sei(void *data, uint8_t **sei, size_t *size)
{
	struct obs_x264 *obsx264 = data;

	if (!obsx264->context)
		return false;

	*sei  = obsx264->sei;
	*size = obsx264->sei_size;
	return true;
}

static bool obs_x264_video_info(void *data, struct video_scale_info *info)
{
	struct obs_x264 *obsx264 = data;
	video_t video = obs_encoder_video(obsx264->encoder);
	const struct video_output_info *vid_info = video_output_getinfo(video);

	if (vid_info->format == VIDEO_FORMAT_I420 ||
	    vid_info->format == VIDEO_FORMAT_NV12)
		return false;

	info->format     = VIDEO_FORMAT_NV12;
	info->width      = vid_info->width;
	info->height     = vid_info->height;
	info->range      = VIDEO_RANGE_DEFAULT;
	info->colorspace = VIDEO_CS_DEFAULT;
	return true;
}

struct obs_encoder_info obs_x264_encoder = {
	.id         = "obs_x264",
	.type       = OBS_ENCODER_VIDEO,
	.codec      = "h264",
	.getname    = obs_x264_getname,
	.create     = obs_x264_create,
	.destroy    = obs_x264_destroy,
	.encode     = obs_x264_encode,
	.properties = obs_x264_props,
	.defaults   = obs_x264_defaults,
	.update     = obs_x264_update,
	.extra_data = obs_x264_extra_data,
	.sei_data   = obs_x264_sei,
	.video_info = obs_x264_video_info
};
