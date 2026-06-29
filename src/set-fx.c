/*
Screen Effect Tools - shared helpers for the [SET] image-FX filters.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "set-fx.h"
#include <plugin-support.h>
#include <math.h>

static inline float srgb_to_linear(float c)
{
	return (c <= 0.04045f) ? (c / 12.92f)
			       : powf((c + 0.055f) / 1.055f, 2.4f);
}

void imgfx_color_to_vec4(uint32_t abgr, struct vec4 *out, bool srgb_decode)
{
	float r = (float)((abgr) & 0xff) / 255.0f;
	float g = (float)((abgr >> 8) & 0xff) / 255.0f;
	float b = (float)((abgr >> 16) & 0xff) / 255.0f;
	float a = (float)((abgr >> 24) & 0xff) / 255.0f;

	if (srgb_decode) {
		r = srgb_to_linear(r);
		g = srgb_to_linear(g);
		b = srgb_to_linear(b);
	}

	out->x = r;
	out->y = g;
	out->z = b;
	out->w = a;
}

float imgfx_blink(float elapsed, float freq_hz, float depth)
{
	if (depth <= 0.0f || freq_hz <= 0.0f)
		return 1.0f;

	float phase = sinf(elapsed * freq_hz * 2.0f * (float)M_PI);
	float wave = 0.5f + 0.5f * phase; /* 0..1 */
	return 1.0f - depth * (1.0f - wave); /* (1-depth)..1 */
}

gs_effect_t *imgfx_load_effect(const char *file)
{
	char relpath[256];
	snprintf(relpath, sizeof(relpath), "effects/%s", file);

	char *path = obs_module_file(relpath);
	if (!path) {
		obs_log(LOG_ERROR, "could not resolve data path for %s", relpath);
		return NULL;
	}

	char *err = NULL;
	gs_effect_t *effect = gs_effect_create_from_file(path, &err);
	if (!effect) {
		obs_log(LOG_ERROR, "failed to load %s: %s", relpath,
			err ? err : "(unknown error)");
	}

	bfree(path);
	bfree(err);
	return effect;
}

gs_texture_t *imgfx_capture_input(obs_source_t *context, gs_texrender_t *dst,
				  uint32_t cx, uint32_t cy)
{
	if (cx == 0 || cy == 0)
		return NULL;

	/* Render this filter's upstream input into our own texrender using the
	 * standard process_filter begin/end pair (this is what gives us a
	 * sampleable copy that respects the source's own rendering). The pass
	 * MUST be balanced: every successful begin gets exactly one end. */
	if (!obs_source_process_filter_begin(context, GS_RGBA,
					     OBS_NO_DIRECT_RENDERING))
		return NULL;

	gs_effect_t *pass = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_texrender_reset(dst);

	gs_texture_t *tex = NULL;
	if (gs_texrender_begin(dst, cx, cy)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_process_filter_end(context, pass, cx, cy);
		gs_blend_state_pop();

		gs_texrender_end(dst);
		tex = gs_texrender_get_texture(dst);
	} else {
		/* balance the begin even on failure so OBS state stays sane */
		obs_source_process_filter_end(context, pass, cx, cy);
	}
	return tex;
}

gs_texture_t *imgfx_render_to(gs_texrender_t *dst, gs_texture_t *src,
			      gs_effect_t *effect, const char *tech,
			      uint32_t cx, uint32_t cy)
{
	if (!src || cx == 0 || cy == 0)
		return NULL;

	gs_texrender_reset(dst);
	if (!gs_texrender_begin(dst, cx, cy))
		return NULL;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, src);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	while (gs_effect_loop(effect, tech))
		gs_draw_sprite(src, 0, cx, cy);

	gs_blend_state_pop();
	gs_texrender_end(dst);
	return gs_texrender_get_texture(dst);
}
