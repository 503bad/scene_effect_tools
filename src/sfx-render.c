#include "sfx-render.h"
#include "sfx-common.h"

#include <graphics/vec4.h>
#include <plugin-support.h>

gs_texture_t *sfx_capture_input(obs_source_t *context, gs_texrender_t *tr,
				uint32_t cx, uint32_t cy)
{
	if (!tr || cx == 0 || cy == 0)
		return NULL;

	gs_effect_t *pass = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	if (!obs_source_process_filter_begin(context, GS_RGBA,
					     OBS_NO_DIRECT_RENDERING))
		return NULL;

	gs_texrender_reset(tr);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	bool ok = gs_texrender_begin(tr, cx, cy);
	if (ok) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
		obs_source_process_filter_end(context, pass, cx, cy);
		gs_texrender_end(tr);
	} else {
		/* Balance the begin even when the target could not be opened. */
		obs_source_process_filter_end(context, pass, cx, cy);
	}

	gs_blend_state_pop();

	return ok ? gs_texrender_get_texture(tr) : NULL;
}

void sfx_blur_init(struct sfx_blur *blur)
{
	if (!blur)
		return;
	blur->a = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	blur->b = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	char *path = obs_module_file("effects/blur.effect");
	if (path) {
		blur->fx = gs_effect_create_from_file(path, NULL);
		if (!blur->fx)
			obs_log(LOG_ERROR, "failed to load blur.effect (%s)",
				path);
		bfree(path);
	}
}

void sfx_blur_free(struct sfx_blur *blur)
{
	if (!blur)
		return;
	if (blur->fx)
		gs_effect_destroy(blur->fx);
	if (blur->a)
		gs_texrender_destroy(blur->a);
	if (blur->b)
		gs_texrender_destroy(blur->b);
	blur->fx = NULL;
	blur->a = NULL;
	blur->b = NULL;
}

/* One directional blur pass: sample `tex` (w*h) along (dx,dy) texels into `tr`. */
static gs_texture_t *blur_pass(struct sfx_blur *blur, gs_texrender_t *tr,
			       gs_texture_t *tex, uint32_t w, uint32_t h,
			       float dx, float dy)
{
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, w, h))
		return tex;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	sfx_set_texture(blur->fx, "image", tex);
	sfx_set_vec2(blur->fx, "dir", dx, dy);

	while (gs_effect_loop(blur->fx, "Draw"))
		gs_draw_sprite(tex, 0, w, h);

	gs_blend_state_pop();
	gs_texrender_end(tr);

	return gs_texrender_get_texture(tr);
}

gs_texture_t *sfx_blur_run(struct sfx_blur *blur, gs_texture_t *src, uint32_t w,
			   uint32_t h, int iterations)
{
	if (!blur || !blur->fx || !src || iterations <= 0 || w == 0 || h == 0)
		return src;

	float tx = 1.0f / (float)w;
	float ty = 1.0f / (float)h;

	gs_texture_t *cur = src;
	for (int i = 0; i < iterations; ++i) {
		cur = blur_pass(blur, blur->a, cur, w, h, tx, 0.0f);
		cur = blur_pass(blur, blur->b, cur, w, h, 0.0f, ty);
	}
	return cur;
}
