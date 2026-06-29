/*
Screen Effect Tools - [SET] Rim Light filter (single pass).
Ported from img_effect_tools.

Directional light along the alpha edge of a 2D source. The alpha gradient gives
the contour normal; rim brightness is dot(normal, light_dir). Blink modulates
intensity on the C side.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include "../set-fx.h"
#include <math.h>

struct rim_data {
	obs_source_t *context;
	gs_effect_t *effect;

	struct vec4 color;
	float intensity;
	float opacity;
	float rim_width;
	float softness;
	float angle_rad;
	float blink_freq;
	float blink_depth;
	float elapsed;

	gs_eparam_t *p_color;
	gs_eparam_t *p_intensity;
	gs_eparam_t *p_opacity;
	gs_eparam_t *p_width;
	gs_eparam_t *p_softness;
	gs_eparam_t *p_dir;
	gs_eparam_t *p_resolution;
};

static const char *rim_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("RimLight.Name");
}

static void rim_update(void *data, obs_data_t *settings)
{
	struct rim_data *f = data;
	uint32_t c = (uint32_t)obs_data_get_int(settings, "light_color");
	imgfx_color_to_vec4(c, &f->color, true);
	f->intensity = (float)obs_data_get_double(settings, "intensity");
	f->opacity = (float)obs_data_get_double(settings, "opacity");
	f->rim_width = (float)obs_data_get_double(settings, "rim_width");
	f->softness = (float)obs_data_get_double(settings, "softness");
	float deg = (float)obs_data_get_double(settings, "light_angle");
	f->angle_rad = deg * (float)M_PI / 180.0f;
	f->blink_freq = (float)obs_data_get_double(settings, "blink_freq");
	f->blink_depth = (float)obs_data_get_double(settings, "blink_depth");
}

static void *rim_create(obs_data_t *settings, obs_source_t *source)
{
	struct rim_data *f = bzalloc(sizeof(*f));
	f->context = source;

	obs_enter_graphics();
	f->effect = imgfx_load_effect("rim_light.effect");
	obs_leave_graphics();

	if (f->effect) {
		f->p_color =
			gs_effect_get_param_by_name(f->effect, "light_color");
		f->p_intensity =
			gs_effect_get_param_by_name(f->effect, "intensity");
		f->p_opacity =
			gs_effect_get_param_by_name(f->effect, "opacity");
		f->p_width =
			gs_effect_get_param_by_name(f->effect, "rim_width");
		f->p_softness =
			gs_effect_get_param_by_name(f->effect, "softness");
		f->p_dir = gs_effect_get_param_by_name(f->effect, "light_dir");
		f->p_resolution =
			gs_effect_get_param_by_name(f->effect, "resolution");
	}

	rim_update(f, settings);
	return f;
}

static void rim_destroy(void *data)
{
	struct rim_data *f = data;
	if (f->effect) {
		obs_enter_graphics();
		gs_effect_destroy(f->effect);
		obs_leave_graphics();
	}
	bfree(f);
}

static void rim_video_tick(void *data, float seconds)
{
	struct rim_data *f = data;
	f->elapsed += seconds;
}

static void rim_video_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct rim_data *f = data;

	if (!f->effect)
		return;

	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t cx = target ? obs_source_get_base_width(target) : 0;
	uint32_t cy = target ? obs_source_get_base_height(target) : 0;

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	float mod = imgfx_blink(f->elapsed, f->blink_freq, f->blink_depth);

	struct vec2 dir;
	vec2_set(&dir, cosf(f->angle_rad), sinf(f->angle_rad));

	struct vec2 res;
	vec2_set(&res, cx ? (float)cx : 1.0f, cy ? (float)cy : 1.0f);

	gs_effect_set_vec4(f->p_color, &f->color);
	gs_effect_set_float(f->p_intensity, f->intensity * mod);
	gs_effect_set_float(f->p_opacity, f->opacity);
	gs_effect_set_float(f->p_width, f->rim_width);
	gs_effect_set_float(f->p_softness, f->softness);
	gs_effect_set_vec2(f->p_dir, &dir);
	gs_effect_set_vec2(f->p_resolution, &res);

	obs_source_process_filter_end(f->context, f->effect, cx, cy);
}

static obs_properties_t *rim_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_color(p, "light_color",
				 obs_module_text("RimLight.Color"));
	obs_properties_add_float_slider(p, "intensity",
					obs_module_text("RimLight.Intensity"),
					0.0, 4.0, 0.01);
	obs_properties_add_float_slider(p, "opacity",
					obs_module_text("RimLight.Opacity"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(p, "rim_width",
					obs_module_text("RimLight.Width"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(p, "softness",
					obs_module_text("RimLight.Softness"),
					0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "light_angle",
					obs_module_text("RimLight.Angle"), 0.0,
					360.0, 1.0);
	obs_properties_add_float_slider(p, "blink_freq",
					obs_module_text("Common.BlinkFreq"),
					0.0, 20.0, 0.1);
	obs_properties_add_float_slider(p, "blink_depth",
					obs_module_text("Common.BlinkDepth"),
					0.0, 1.0, 0.01);
	return p;
}

static void rim_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "light_color", 0xFFFFFFFF);
	obs_data_set_default_double(settings, "intensity", 1.0);
	obs_data_set_default_double(settings, "opacity", 1.0);
	obs_data_set_default_double(settings, "rim_width", 0.2);
	obs_data_set_default_double(settings, "softness", 0.5);
	obs_data_set_default_double(settings, "light_angle", 90.0);
	obs_data_set_default_double(settings, "blink_freq", 0.0);
	obs_data_set_default_double(settings, "blink_depth", 0.0);
}

static struct obs_source_info set_rim_light_info = {
	.id = "set_rim_light",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = rim_get_name,
	.create = rim_create,
	.destroy = rim_destroy,
	.update = rim_update,
	.video_tick = rim_video_tick,
	.video_render = rim_video_render,
	.get_properties = rim_get_properties,
	.get_defaults = rim_get_defaults,
};

void sfx_register_set_rim_light(void)
{
	obs_register_source(&set_rim_light_info);
}
