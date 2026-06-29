/*
Screen Effect Tools - [SET] Echo / Afterimage filter (temporal, persistent buffer).
Ported from img_effect_tools.

Accumulates past frames into a decaying trail. Unlike the spatial filters this
keeps a persistent accumulation buffer (ping-pong texrenders) that is NOT reset
each frame - only re-created on resize / context loss. The live frame is drawn
over the tinted, fading echo.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include "../set-fx.h"
#include <math.h>

struct echo_data {
	obs_source_t *context;
	gs_effect_t *effect;

	float lifetime;   /* trail life in seconds (decay rate)        */
	struct vec4 overlay; /* color overlay (rgba)                   */
	float frequency;  /* echo capture rate (per second)            */
	float opacity;    /* max trail density 0..1                    */

	float dt;          /* seconds elapsed last tick                */
	float inject_timer; /* accumulates toward the capture interval */
	bool inject;        /* capture a new echo this frame           */

	gs_texrender_t *tr_cur;
	gs_texrender_t *accum_read;
	gs_texrender_t *accum_write;

	uint32_t last_cx, last_cy;
	bool primed;

	gs_eparam_t *p_prev;
	gs_eparam_t *p_decay;
	gs_eparam_t *p_opacity;
	gs_eparam_t *p_inject;
	gs_eparam_t *p_echo;
	gs_eparam_t *p_overlay;
};

static const char *echo_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Echo.Name");
}

static void echo_update(void *data, obs_data_t *settings)
{
	struct echo_data *f = data;
	f->lifetime = (float)obs_data_get_double(settings, "lifetime");
	uint32_t c = (uint32_t)obs_data_get_int(settings, "color_overlay");
	imgfx_color_to_vec4(c, &f->overlay, false);
	f->frequency = (float)obs_data_get_double(settings, "frequency");
	f->opacity = (float)obs_data_get_double(settings, "opacity");
}

static void *echo_create(obs_data_t *settings, obs_source_t *source)
{
	struct echo_data *f = bzalloc(sizeof(*f));
	f->context = source;

	obs_enter_graphics();
	f->effect = imgfx_load_effect("echo.effect");
	f->tr_cur = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->accum_read = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->accum_write = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

	if (f->effect) {
		f->p_prev = gs_effect_get_param_by_name(f->effect, "prev");
		f->p_decay = gs_effect_get_param_by_name(f->effect, "decay");
		f->p_opacity =
			gs_effect_get_param_by_name(f->effect, "opacity");
		f->p_inject = gs_effect_get_param_by_name(f->effect, "inject");
		f->p_echo = gs_effect_get_param_by_name(f->effect, "echo");
		f->p_overlay =
			gs_effect_get_param_by_name(f->effect, "color_overlay");
	}

	echo_update(f, settings);
	return f;
}

static void echo_destroy(void *data)
{
	struct echo_data *f = data;
	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	gs_texrender_destroy(f->tr_cur);
	gs_texrender_destroy(f->accum_read);
	gs_texrender_destroy(f->accum_write);
	obs_leave_graphics();
	bfree(f);
}

static void echo_video_tick(void *data, float seconds)
{
	struct echo_data *f = data;
	f->dt = seconds;
	f->inject_timer += seconds;

	float interval = 1.0f / (f->frequency > 0.0f ? f->frequency : 30.0f);
	if (f->inject_timer >= interval) {
		f->inject = true;
		/* keep the remainder so the average rate stays accurate */
		f->inject_timer = fmodf(f->inject_timer, interval);
	} else {
		f->inject = false;
	}
}

/* Clear a texrender to fully transparent at cx*cy so its texture is valid. */
static void echo_clear_buffer(gs_texrender_t *tr, uint32_t cx, uint32_t cy)
{
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, cx, cy))
		return;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	gs_texrender_end(tr);
}

static void echo_video_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct echo_data *f = data;

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

	/* (Re)initialise the persistent buffers on first use or resize. */
	if (!f->primed || cx != f->last_cx || cy != f->last_cy) {
		echo_clear_buffer(f->accum_read, cx, cy);
		echo_clear_buffer(f->accum_write, cx, cy);
		f->last_cx = cx;
		f->last_cy = cy;
		f->primed = true;
	}

	gs_texture_t *cur = imgfx_capture_input(f->context, f->tr_cur, cx, cy);
	if (!cur)
		return; /* capture already balanced its own begin/end */

	gs_texture_t *prev = gs_texrender_get_texture(f->accum_read);
	if (!prev) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	float decay = expf(-f->dt / (f->lifetime > 1e-3f ? f->lifetime : 1e-3f));

	/* --- Accumulate: write the new trail into accum_write ------------ */
	gs_texrender_reset(f->accum_write);
	if (gs_texrender_begin(f->accum_write, cx, cy)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		gs_effect_set_texture(
			gs_effect_get_param_by_name(f->effect, "image"), cur);
		gs_effect_set_texture(f->p_prev, prev);
		gs_effect_set_float(f->p_decay, decay);
		gs_effect_set_float(f->p_opacity, f->opacity);
		gs_effect_set_float(f->p_inject, f->inject ? 1.0f : 0.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		while (gs_effect_loop(f->effect, "Accum"))
			gs_draw_sprite(cur, 0, cx, cy);
		gs_blend_state_pop();

		gs_texrender_end(f->accum_write);
	}

	gs_texture_t *trail = gs_texrender_get_texture(f->accum_write);

	/* --- Combine: draw the live frame over the tinted trail ---------- */
	gs_effect_set_texture(
		gs_effect_get_param_by_name(f->effect, "image"), cur);
	gs_effect_set_texture(f->p_echo, trail ? trail : cur);
	gs_effect_set_vec4(f->p_overlay, &f->overlay);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(f->effect, "Combine"))
		gs_draw_sprite(cur, 0, cx, cy);
	gs_blend_state_pop();

	/* Swap so this frame's trail becomes next frame's history. */
	gs_texrender_t *tmp = f->accum_read;
	f->accum_read = f->accum_write;
	f->accum_write = tmp;
}

static obs_properties_t *echo_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, "lifetime",
					obs_module_text("Echo.Lifetime"), 0.0,
					2.0, 0.01);
	obs_properties_add_color_alpha(p, "color_overlay",
				       obs_module_text("Echo.Overlay"));
	obs_properties_add_float_slider(p, "frequency",
					obs_module_text("Echo.Frequency"), 1.0,
					60.0, 1.0);
	obs_properties_add_float_slider(p, "opacity",
					obs_module_text("Echo.Opacity"), 0.0,
					1.0, 0.01);
	return p;
}

static void echo_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "lifetime", 0.5);
	obs_data_set_default_int(settings, "color_overlay", 0x00FFFFFF);
	obs_data_set_default_double(settings, "frequency", 30.0);
	obs_data_set_default_double(settings, "opacity", 0.6);
}

static struct obs_source_info set_echo_info = {
	.id = "set_echo",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = echo_get_name,
	.create = echo_create,
	.destroy = echo_destroy,
	.update = echo_update,
	.video_tick = echo_video_tick,
	.video_render = echo_video_render,
	.get_properties = echo_get_properties,
	.get_defaults = echo_get_defaults,
};

void sfx_register_set_echo(void)
{
	obs_register_source(&set_echo_info);
}
