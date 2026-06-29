/*
Screen Effect Tools - shared helpers for the [SET] image-FX filters ported from
img_effect_tools (rim light, outer/inner glow, echo). These mirror the helper
API those filters were written against so the ported sources stay close to the
originals; the rest of the plugin uses the sfx_* helpers in sfx-common.h.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>
#include <graphics/vec4.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* OBS color properties return 0xAABBGGRR. Convert to a linear-ish RGBA vec4 in
 * 0..1 (straight gamma-decoded so additive glows read naturally). */
void imgfx_color_to_vec4(uint32_t abgr, struct vec4 *out, bool srgb_decode);

/* Blink modulator: returns a 0..1 factor to multiply an intensity by.
 *   depth == 0  -> always 1.0 (no blink)
 *   depth == 1  -> swings across 0..1 at freq_hz
 * elapsed is the filter's accumulated tick time in seconds. */
float imgfx_blink(float elapsed, float freq_hz, float depth);

/* Load an .effect from data/effects/<file>. Must be called inside a graphics
 * context (obs_enter_graphics). Logs on failure and returns NULL. */
gs_effect_t *imgfx_load_effect(const char *file);

/* Capture this filter's upstream input (the target with all earlier filters
 * applied) into `dst` at cx*cy. Returns the resulting texture, or NULL on
 * failure. Call inside video_render. */
gs_texture_t *imgfx_capture_input(obs_source_t *context, gs_texrender_t *dst,
				  uint32_t cx, uint32_t cy);

/* Draw `src` through `effect`/`tech` into `dst` (cx*cy). The effect's "image"
 * param is bound to `src`; bind any extra params before calling. Replaces the
 * whole target (blend ONE/ZERO). Returns the rendered texture. */
gs_texture_t *imgfx_render_to(gs_texrender_t *dst, gs_texture_t *src,
			      gs_effect_t *effect, const char *tech,
			      uint32_t cx, uint32_t cy);

#ifdef __cplusplus
}
#endif
