/*
Screen Effect Tools - [SET] Outer Glow filter (multi-pass).
Ported from img_effect_tools.

Extract a mask from the source alpha, blur it (separable gaussian, H then V),
tint it, and add it back so light bleeds out past the subject's edge.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include "../set-fx.h"

struct glow_data {
	obs_source_t *context;
	gs_effect_t *effect;

	struct vec4 color;
	float opacity;
	float size_pct; /* size: glow reach as a % of the source's short side */
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

static const char *glow_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Glow.Name");
}

static void glow_update(void *data, obs_data_t *settings)
{
	struct glow_data *f = data;
	uint32_t c = (uint32_t)obs_data_get_int(settings, "glow_color");
	imgfx_color_to_vec4(c, &f->color, true);
	f->opacity = (float)obs_data_get_double(settings, "opacity");
	f->size_pct = (float)obs_data_get_double(settings, "size_pct");
	/* stored normalized 0..3; the slider is a percentage (0..300) */
	f->spread = (float)obs_data_get_double(settings, "spread_pct") * 0.01f;
	f->blink_freq = (float)obs_data_get_double(settings, "blink_freq");
	f->blink_depth = (float)obs_data_get_double(settings, "blink_depth");
}

/* Glow reach in px = size_pct of the source's short side. Relative to the image
 * so the look is the same regardless of the source's resolution. */
static float glow_range_px(struct glow_data *f, uint32_t cx, uint32_t cy)
{
	float ref = (float)(cx < cy ? cx : cy);
	return f->size_pct * 0.01f * ref;
}

/* Margin added around the source so the glow can spill past its own bounds.
 * +2.5 px keeps the very tip of the falloff inside. */
static uint32_t glow_pad_px(struct glow_data *f, uint32_t cx, uint32_t cy)
{
	return (uint32_t)(glow_range_px(f, cx, cy) + 2.5f);
}

/* The filter enlarges its output by `pad` on every side. */
static uint32_t glow_get_width(void *data)
{
	struct glow_data *f = data;
	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t cx = target ? obs_source_get_base_width(target) : 0;
	uint32_t cy = target ? obs_source_get_base_height(target) : 0;
	return cx ? cx + 2 * glow_pad_px(f, cx, cy) : cx;
}

static uint32_t glow_get_height(void *data)
{
	struct glow_data *f = data;
	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t cx = target ? obs_source_get_base_width(target) : 0;
	uint32_t cy = target ? obs_source_get_base_height(target) : 0;
	return cy ? cy + 2 * glow_pad_px(f, cx, cy) : cy;
}

static void *glow_create(obs_data_t *settings, obs_source_t *source)
{
	struct glow_data *f = bzalloc(sizeof(*f));
	f->context = source;

	obs_enter_graphics();
	f->effect = imgfx_load_effect("glow.effect");
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

	glow_update(f, settings);
	return f;
}

static void glow_destroy(void *data)
{
	struct glow_data *f = data;
	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	gs_texrender_destroy(f->tr_in);
	gs_texrender_destroy(f->tr_a);
	gs_texrender_destroy(f->tr_b);
	obs_leave_graphics();
	bfree(f);
}

static void glow_video_tick(void *data, float seconds)
{
	struct glow_data *f = data;
	f->elapsed += seconds;
}

static void glow_video_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct glow_data *f = data;

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
		return; /* capture already balanced its own begin/end */

	const float mod = imgfx_blink(f->elapsed, f->blink_freq, f->blink_depth);

	/* Glow reach (px) is derived from the source's short side, so it scales
	 * with the image's own resolution. */
	const float range = glow_range_px(f, cx, cy);

	/* Padded canvas: the source sits at (pad,pad); the surrounding margin is
	 * where the glow is allowed to spill. */
	const uint32_t pad = glow_pad_px(f, cx, cy);
	const uint32_t pw = cx + 2 * pad;
	const uint32_t ph = cy + 2 * pad;

	/* 1) extract mask onto the padded canvas, with the source offset by
	 * `pad`, so the blur has empty room to bleed into on every side.
	 * NB: gs_technique_end() clears every effect param's value, so each
	 * pass's params must be (re)set right before its own draw loop. */
	gs_effect_set_texture(
		gs_effect_get_param_by_name(f->effect, "image"), input);

	gs_texrender_reset(f->tr_a);
	if (gs_texrender_begin(f->tr_a, pw, ph)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)pw, 0.0f, (float)ph, -100.0f, 100.0f);

		gs_matrix_push();
		gs_matrix_translate3f((float)pad, (float)pad, 0.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		while (gs_effect_loop(f->effect, "Extract"))
			gs_draw_sprite(input, 0, cx, cy);
		gs_blend_state_pop();
		gs_matrix_pop();

		gs_texrender_end(f->tr_a);
	}
	gs_texture_t *extracted = gs_texrender_get_texture(f->tr_a);

	/* 2) separable blur across the full padded canvas */
	struct vec2 dir;
	vec2_set(&dir, range / (float)pw, 0.0f);
	gs_effect_set_vec2(f->p_blur_dir, &dir);
	gs_texture_t *blur_h = imgfx_render_to(f->tr_b, extracted, f->effect,
					       "Blur", pw, ph);

	vec2_set(&dir, 0.0f, range / (float)ph);
	gs_effect_set_vec2(f->p_blur_dir, &dir);
	gs_texture_t *glow = imgfx_render_to(f->tr_a, blur_h, f->effect, "Blur",
					     pw, ph);

	/* 3a) lay the blurred glow over the whole padded output (incl. margins).
	 * Set ALL params GlowOut uses here, immediately before its draw. */
	gs_effect_set_vec4(f->p_glow_color, &f->color);
	gs_effect_set_float(f->p_spread, f->spread);
	gs_effect_set_texture(f->p_glow_tex, glow);
	gs_effect_set_float(f->p_opacity, f->opacity * mod);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(f->effect, "GlowOut"))
		gs_draw_sprite(glow, 0, pw, ph);

	/* 3b) draw the original source on top, at the pad offset */
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(def, "image"), input);
	gs_matrix_push();
	gs_matrix_translate3f((float)pad, (float)pad, 0.0f);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(input, 0, cx, cy);
	gs_matrix_pop();
	gs_blend_state_pop();
}

static obs_properties_t *glow_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_color(p, "glow_color",
				 obs_module_text("Glow.Color"));
	obs_properties_add_float_slider(p, "size_pct",
					obs_module_text("Glow.Size"), 0.0,
					300.0, 0.1);
	obs_properties_add_float_slider(p, "spread_pct",
					obs_module_text("Glow.Spread"), 0.0,
					300.0, 0.5);
	obs_properties_add_float_slider(p, "opacity",
					obs_module_text("Glow.Opacity"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(p, "blink_freq",
					obs_module_text("Common.BlinkFreq"),
					0.0, 20.0, 0.1);
	obs_properties_add_float_slider(p, "blink_depth",
					obs_module_text("Common.BlinkDepth"),
					0.0, 1.0, 0.01);
	return p;
}

static void glow_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "glow_color", 0xFFFFFFFF);
	obs_data_set_default_double(settings, "opacity", 0.8);
	obs_data_set_default_double(settings, "size_pct", 4.0);
	obs_data_set_default_double(settings, "spread_pct", 10.0);
	obs_data_set_default_double(settings, "blink_freq", 0.0);
	obs_data_set_default_double(settings, "blink_depth", 0.0);
}

static struct obs_source_info set_glow_info = {
	.id = "set_glow",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = glow_get_name,
	.create = glow_create,
	.destroy = glow_destroy,
	.update = glow_update,
	.get_width = glow_get_width,
	.get_height = glow_get_height,
	.video_tick = glow_video_tick,
	.video_render = glow_video_render,
	.get_properties = glow_get_properties,
	.get_defaults = glow_get_defaults,
};

void sfx_register_set_glow(void)
{
	obs_register_source(&set_glow_info);
}
