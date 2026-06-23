#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Render the filter's input (the source it is attached to) into `tr` at cx*cy
 * and return the resulting texture. Returns NULL on failure. Must be called
 * from the filter's video_render with the graphics context active. This also
 * consumes the filter's begin/end pair, so after calling it the caller is free
 * to draw its own passes into the current target. */
gs_texture_t *sfx_capture_input(obs_source_t *context, gs_texrender_t *tr,
				uint32_t cx, uint32_t cy);

/* A reusable separable-Gaussian blur over two ping-pong render targets. The
 * shared blur.effect is loaded lazily on first use. All calls must run with the
 * graphics context active. */
struct sfx_blur {
	gs_effect_t *fx; /* shared blur.effect (owned) */
	gs_texrender_t *a;
	gs_texrender_t *b;
};

void sfx_blur_init(struct sfx_blur *blur);
void sfx_blur_free(struct sfx_blur *blur);

/* Blur `src` (size w*h) with `iterations` separable passes and return the
 * blurred texture (owned by the helper's ping-pong targets, valid until the
 * next blur call). `iterations` <= 0 returns `src` unchanged. */
gs_texture_t *sfx_blur_run(struct sfx_blur *blur, gs_texture_t *src, uint32_t w,
			   uint32_t h, int iterations);

#ifdef __cplusplus
}
#endif
