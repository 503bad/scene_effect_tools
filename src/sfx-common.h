#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decompose an OBS 0xAABBGGRR colour into normalized rgba (0..1). */
static inline void sfx_unpack_color(uint32_t c, float rgba[4])
{
	rgba[0] = (float)((c >> 0) & 0xFF) / 255.0f;
	rgba[1] = (float)((c >> 8) & 0xFF) / 255.0f;
	rgba[2] = (float)((c >> 16) & 0xFF) / 255.0f;
	rgba[3] = (float)((c >> 24) & 0xFF) / 255.0f;
}

/* Convenience guarded effect-parameter setters. Each silently no-ops when the
 * named uniform is absent, so a shader can omit parameters it does not use. */
void sfx_set_float(gs_effect_t *e, const char *name, float v);
void sfx_set_int(gs_effect_t *e, const char *name, int v);
void sfx_set_bool(gs_effect_t *e, const char *name, bool v);
void sfx_set_vec2(gs_effect_t *e, const char *name, float x, float y);
void sfx_set_vec3(gs_effect_t *e, const char *name, float x, float y, float z);
void sfx_set_color(gs_effect_t *e, const char *name, uint32_t color);
void sfx_set_texture(gs_effect_t *e, const char *name, gs_texture_t *tex);

/* Resolve the pixel size of the filter's input (the source this filter is
 * attached to). Returns 0,0 when not yet attached. */
void sfx_target_size(obs_source_t *context, uint32_t *w, uint32_t *h);

#ifdef __cplusplus
}
#endif
