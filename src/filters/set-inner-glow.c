/*
Screen Effect Tools - [SET] Inner Glow filter (multi-pass).
Ported from img_effect_tools.

Like Outer Glow, but the light hugs the inside of the subject's alpha edge and
fades inward, so it never spills outside the source bounds.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include "../set-fx.h"

struct inner_glow_data {
	obs_source_t *context;
	gs_effect_t *effect;

	struct vec4 color;
	float opacity;
	float size_ratio; /* 0..1 of the source's larger dimension */
	float spread;
	float blink_freq;
	float blink_depth;
	float elapsed;

	gs_texrender_t *tr_in;
	gs_texrender_t *tr_a;
	gs_texrender_t *tr_b;

	gs_eparam_t *p_glow_color;
	gs_eparam_t *p_spread;
	gs_eparam_t *p_blur_dir;
	gs_eparam_t *p_opacity;
	gs_eparam_t *p_glow_tex;
};

static const char *inner_glow_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("InnerGlow.Name");
}

static void inner_glow_update(void *data, obs_data_t *settings)
{
	struct inner_glow_data *f = data;
	uint32_t c = (uint32_t)obs_data_get_int(settings, "glow_color");
	imgfx_color_to_vec4(c, &f->color, true);
	f->opacity = (float)obs_data_get_double(settings, "opacity");
	f->size_ratio = (float)obs_data_get_double(settings, "size_ratio");
	f->spread = (float)obs_data_get_double(settings, "spread");
	f->blink_freq = (float)obs_data_get_double(settings, "blink_freq");
	f->blink_depth = (float)obs_data_get_double(settings, "blink_depth");
}

static void *inner_glow_create(obs_data_t *settings, obs_source_t *source)
{
	struct inner_glow_data *f = bzalloc(sizeof(*f));
	f->context = source;

	obs_enter_graphics();
	f->effect = imgfx_load_effect("inner_glow.effect");
	f->tr_in = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->tr_a = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->tr_b = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

	if (f->effect) {
		f->p_glow_color =
			gs_effect_get_param_by_name(f->effect, "glow_color");
		f->p_spread =
			gs_effect_get_param_by_name(f->effect, "spread");
		f->p_blur_dir =
			gs_effect_get_param_by_name(f->effect, "blur_dir");
		f->p_opacity =
			gs_effect_get_param_by_name(f->effect, "opacity");
		f->p_glow_tex =
			gs_effect_get_param_by_name(f->effect, "glow_tex");
	}

	inner_glow_update(f, settings);
	return f;
}

static void inner_glow_destroy(void *data)
{
	struct inner_glow_data *f = data;
	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	gs_texrender_destroy(f->tr_in);
	gs_texrender_destroy(f->tr_a);
	gs_texrender_destroy(f->tr_b);
	obs_leave_graphics();
	bfree(f);
}

static void inner_glow_video_tick(void *data, float seconds)
{
	struct inner_glow_data *f = data;
	f->elapsed += seconds;
}

static void inner_glow_video_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct inner_glow_data *f = data;

	obs_source_t *target = obs_filter_get_target(f->context);
	obs_source_t *parent = obs_filter_get_parent(f->context);
	if (!f->effect || !target || !parent) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);
	if (cx == 0 || cy == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	gs_texture_t *input =
		imgfx_capture_input(f->context, f->tr_in, cx, cy);
	if (!input)
		return;

	const float mod = imgfx_blink(f->elapsed, f->blink_freq, f->blink_depth);

	/* 1) extract the source alpha */
	gs_texture_t *extracted =
		imgfx_render_to(f->tr_a, input, f->effect, "Extract", cx, cy);

	/* 2) separable blur of the alpha. Size is a ratio of the larger source
	 * dimension, so 1.0 reaches across the whole image. */
	float maxDim = (float)(cx > cy ? cx : cy);
	float range_px = f->size_ratio * maxDim;
	struct vec2 dir;
	vec2_set(&dir, range_px / (float)cx, 0.0f);
	gs_effect_set_vec2(f->p_blur_dir, &dir);
	gs_texture_t *blur_h = imgfx_render_to(f->tr_b, extracted, f->effect,
					       "Blur", cx, cy);

	vec2_set(&dir, 0.0f, range_px / (float)cy);
	gs_effect_set_vec2(f->p_blur_dir, &dir);
	gs_texture_t *blurred = imgfx_render_to(f->tr_a, blur_h, f->effect,
						"Blur", cx, cy);

	/* 3) composite the inward band over the original input */
	gs_effect_set_vec4(f->p_glow_color, &f->color);
	gs_effect_set_float(f->p_spread, f->spread);
	gs_effect_set_texture(f->p_glow_tex, blurred);
	gs_effect_set_float(f->p_opacity, f->opacity * mod);
	gs_effect_set_texture(
		gs_effect_get_param_by_name(f->effect, "image"), input);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(f->effect, "Composite"))
		gs_draw_sprite(input, 0, cx, cy);
	gs_blend_state_pop();
}

static obs_properties_t *inner_glow_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_color(p, "glow_color",
				 obs_module_text("InnerGlow.Color"));
	obs_properties_add_float_slider(p, "size_ratio",
					obs_module_text("InnerGlow.Size"), 0.0,
					1.0, 0.005);
	obs_properties_add_float_slider(p, "spread",
					obs_module_text("InnerGlow.Spread"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(p, "opacity",
					obs_module_text("InnerGlow.Opacity"),
					0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "blink_freq",
					obs_module_text("Common.BlinkFreq"),
					0.0, 20.0, 0.1);
	obs_properties_add_float_slider(p, "blink_depth",
					obs_module_text("Common.BlinkDepth"),
					0.0, 1.0, 0.01);
	return p;
}

static void inner_glow_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "glow_color", 0xFFFFFFFF);
	obs_data_set_default_double(settings, "opacity", 0.8);
	obs_data_set_default_double(settings, "size_ratio", 0.1);
	obs_data_set_default_double(settings, "spread", 0.1);
	obs_data_set_default_double(settings, "blink_freq", 0.0);
	obs_data_set_default_double(settings, "blink_depth", 0.0);
}

static struct obs_source_info set_inner_glow_info = {
	.id = "set_inner_glow",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = inner_glow_get_name,
	.create = inner_glow_create,
	.destroy = inner_glow_destroy,
	.update = inner_glow_update,
	.video_tick = inner_glow_video_tick,
	.video_render = inner_glow_video_render,
	.get_properties = inner_glow_get_properties,
	.get_defaults = inner_glow_get_defaults,
};

void sfx_register_set_inner_glow(void)
{
	obs_register_source(&set_inner_glow_info);
}
