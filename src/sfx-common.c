#include "sfx-common.h"

#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

void sfx_set_float(gs_effect_t *e, const char *name, float v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p)
		gs_effect_set_float(p, v);
}

void sfx_set_int(gs_effect_t *e, const char *name, int v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p)
		gs_effect_set_int(p, v);
}

void sfx_set_bool(gs_effect_t *e, const char *name, bool v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p)
		gs_effect_set_bool(p, v);
}

void sfx_set_vec2(gs_effect_t *e, const char *name, float x, float y)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p) {
		struct vec2 v;
		vec2_set(&v, x, y);
		gs_effect_set_vec2(p, &v);
	}
}

void sfx_set_vec3(gs_effect_t *e, const char *name, float x, float y, float z)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p) {
		struct vec3 v;
		vec3_set(&v, x, y, z);
		gs_effect_set_vec3(p, &v);
	}
}

void sfx_set_color(gs_effect_t *e, const char *name, uint32_t color)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p) {
		float rgba[4];
		sfx_unpack_color(color, rgba);
		struct vec4 v;
		vec4_set(&v, rgba[0], rgba[1], rgba[2], rgba[3]);
		gs_effect_set_vec4(p, &v);
	}
}

void sfx_set_texture(gs_effect_t *e, const char *name, gs_texture_t *tex)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p)
		gs_effect_set_texture(p, tex);
}

void sfx_target_size(obs_source_t *context, uint32_t *w, uint32_t *h)
{
	obs_source_t *target = obs_filter_get_target(context);
	*w = target ? obs_source_get_base_width(target) : 0;
	*h = target ? obs_source_get_base_height(target) : 0;
}
