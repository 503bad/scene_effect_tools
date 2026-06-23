#include "../sfx-common.h"
#include "../sfx-render.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct crunch_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	gs_texrender_t *input;
	gs_texrender_t *held;
	bool have_held;
	float hold_timer;

	float strength;
	float downscale;
	int block_size;
	float compression;
	int color_depth;
	float chroma_subsample;
	float block_noise;
	float frame_hold;
	float elapsed;
};

static const char *crunch_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Crunch");
}

static void crunch_update(void *data, obs_data_t *s)
{
	struct crunch_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->downscale = (float)obs_data_get_double(s, "downscale") / 100.0f;
	f->block_size = (int)obs_data_get_int(s, "block_size");
	f->compression = (float)obs_data_get_double(s, "compression") / 100.0f;
	f->color_depth = (int)obs_data_get_int(s, "color_depth");
	f->chroma_subsample = (float)obs_data_get_double(s, "chroma_subsample") / 100.0f;
	f->block_noise = (float)obs_data_get_double(s, "block_noise") / 100.0f;
	f->frame_hold = (float)obs_data_get_double(s, "frame_hold") / 100.0f;
}

static void crunch_tick(void *data, float seconds)
{
	struct crunch_filter *f = data;
	f->elapsed += seconds;
	if (f->elapsed > 3600.0f)
		f->elapsed = 0.0f;
	f->hold_timer += seconds;
}

static void *crunch_create(obs_data_t *settings, obs_source_t *context)
{
	struct crunch_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/crunch.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load crunch.effect (%s)",
				path);
		bfree(path);
	}
	f->input = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->held = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
	crunch_update(f, settings);
	return f;
}

static void crunch_destroy(void *data)
{
	struct crunch_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	if (f->input)
		gs_texrender_destroy(f->input);
	if (f->held)
		gs_texrender_destroy(f->held);
	obs_leave_graphics();
	bfree(f);
}

/* Copy `src` into the held texrender (a simple passthrough draw). */
static gs_texture_t *copy_to_held(struct crunch_filter *f, gs_texture_t *src,
				  uint32_t cx, uint32_t cy)
{
	gs_texrender_reset(f->held);
	if (!gs_texrender_begin(f->held, cx, cy))
		return src;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	sfx_set_texture(def, "image", src);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(src, 0, cx, cy);
	gs_blend_state_pop();
	gs_texrender_end(f->held);
	f->have_held = true;
	return gs_texrender_get_texture(f->held);
}

static void crunch_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct crunch_filter *f = data;
	if (!f->fx) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t cx, cy;
	sfx_target_size(f->context, &cx, &cy);
	if (cx == 0 || cy == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	gs_texture_t *input = sfx_capture_input(f->context, f->input, cx, cy);
	if (!input) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	/* Frame-rate hold: only refresh the held copy at a reduced rate. */
	gs_texture_t *src = input;
	if (f->frame_hold > 0.0f) {
		float interval = f->frame_hold * 0.2f; /* up to 5 fps */
		if (!f->have_held || f->hold_timer >= interval) {
			src = copy_to_held(f, input, cx, cy);
			f->hold_timer = 0.0f;
		} else {
			src = gs_texrender_get_texture(f->held);
			if (!src)
				src = input;
		}
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_enable_depth_test(false);
	sfx_set_texture(f->fx, "image", src);
	sfx_set_float(f->fx, "strength", f->strength);
	sfx_set_float(f->fx, "downscale", f->downscale);
	sfx_set_float(f->fx, "block_size", (float)f->block_size);
	sfx_set_float(f->fx, "compression", f->compression);
	sfx_set_float(f->fx, "color_depth", (float)f->color_depth);
	sfx_set_float(f->fx, "chroma_subsample", f->chroma_subsample);
	sfx_set_float(f->fx, "block_noise", f->block_noise);
	sfx_set_vec2(f->fx, "uv_size", (float)cx, (float)cy);
	while (gs_effect_loop(f->fx, "Draw"))
		gs_draw_sprite(src, 0, cx, cy);
	gs_blend_state_pop();
}

static obs_properties_t *crunch_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "downscale",
		obs_module_text("Crunch.Downscale"), 0.0, 100.0, 1.0);
	obs_properties_add_int_slider(basic, "block_size",
		obs_module_text("Crunch.BlockSize"), 2, 32, 1);
	obs_properties_add_float_slider(basic, "compression",
		obs_module_text("Crunch.Compression"), 0.0, 100.0, 1.0);
	obs_properties_add_int_slider(basic, "color_depth",
		obs_module_text("Crunch.ColorDepth"), 2, 8, 1);
	obs_properties_add_float_slider(basic, "chroma_subsample",
		obs_module_text("Crunch.ChromaSubsample"), 0.0, 100.0, 1.0);

	obs_properties_add_float_slider(adv, "block_noise",
		obs_module_text("Crunch.BlockNoise"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "frame_hold",
		obs_module_text("Crunch.FrameHold"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void crunch_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "downscale", 60.0);
	obs_data_set_default_int(s, "block_size", 8);
	obs_data_set_default_double(s, "compression", 70.0);
	obs_data_set_default_int(s, "color_depth", 5);
	obs_data_set_default_double(s, "chroma_subsample", 50.0);
	obs_data_set_default_double(s, "block_noise", 40.0);
	obs_data_set_default_double(s, "frame_hold", 0.0);
}

struct obs_source_info sfx_crunch_info = {
	.id             = "sfx_crunch",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = crunch_get_name,
	.create         = crunch_create,
	.destroy        = crunch_destroy,
	.update         = crunch_update,
	.video_tick     = crunch_tick,
	.video_render   = crunch_render,
	.get_properties = crunch_properties,
	.get_defaults   = crunch_defaults,
};

void sfx_register_crunch(void)
{
	obs_register_source(&sfx_crunch_info);
}
