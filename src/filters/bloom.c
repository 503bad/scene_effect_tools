#include "../sfx-common.h"
#include "../sfx-render.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct bloom_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	struct sfx_blur blur;
	gs_texrender_t *input;
	gs_texrender_t *bright;

	float intensity;
	float threshold;
	float knee;
	float radius;
	uint32_t tint;
	int quality; /* 0 low, 1 med, 2 high */
};

static const char *bloom_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Bloom");
}

static void bloom_update(void *data, obs_data_t *s)
{
	struct bloom_filter *f = data;
	f->intensity = (float)obs_data_get_double(s, "intensity") / 100.0f;
	f->threshold = (float)obs_data_get_double(s, "threshold") / 100.0f;
	f->knee = (float)obs_data_get_double(s, "knee") / 100.0f;
	f->radius = (float)obs_data_get_double(s, "radius") / 100.0f;
	f->tint = (uint32_t)obs_data_get_int(s, "tint");
	f->quality = (int)obs_data_get_int(s, "quality");
}

static void *bloom_create(obs_data_t *settings, obs_source_t *context)
{
	struct bloom_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/bloom.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load bloom.effect (%s)",
				path);
		bfree(path);
	}
	f->input = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->bright = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	sfx_blur_init(&f->blur);
	obs_leave_graphics();
	bloom_update(f, settings);
	return f;
}

static void bloom_destroy(void *data)
{
	struct bloom_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	if (f->input)
		gs_texrender_destroy(f->input);
	if (f->bright)
		gs_texrender_destroy(f->bright);
	sfx_blur_free(&f->blur);
	obs_leave_graphics();
	bfree(f);
}

/* Render the bright-pass extraction of `src` into f->bright at bw*bh. */
static gs_texture_t *render_brightpass(struct bloom_filter *f,
				       gs_texture_t *src, uint32_t bw,
				       uint32_t bh)
{
	gs_texrender_reset(f->bright);
	if (!gs_texrender_begin(f->bright, bw, bh))
		return NULL;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)bw, 0.0f, (float)bh, -100.0f, 100.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	sfx_set_texture(f->fx, "image", src);
	sfx_set_float(f->fx, "threshold", f->threshold);
	sfx_set_float(f->fx, "knee", f->knee);
	while (gs_effect_loop(f->fx, "BrightPass"))
		gs_draw_sprite(src, 0, bw, bh);
	gs_blend_state_pop();

	gs_texrender_end(f->bright);
	return gs_texrender_get_texture(f->bright);
}

static void bloom_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct bloom_filter *f = data;
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

	/* Bright-pass + blur at half resolution. */
	uint32_t bw = cx / 2 > 0 ? cx / 2 : 1;
	uint32_t bh = cy / 2 > 0 ? cy / 2 : 1;
	gs_texture_t *bright = render_brightpass(f, input, bw, bh);

	int base = f->quality == 0 ? 1 : (f->quality == 2 ? 4 : 2);
	int iterations = base + (int)(f->radius * 6.0f);
	gs_texture_t *blurred = bright
				? sfx_blur_run(&f->blur, bright, bw, bh,
					       iterations)
				: NULL;

	/* Final additive combine onto the output. */
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_enable_depth_test(false);
	sfx_set_texture(f->fx, "image", input);
	sfx_set_texture(f->fx, "bloom_tex", blurred ? blurred : input);
	sfx_set_float(f->fx, "intensity", blurred ? f->intensity : 0.0f);
	sfx_set_color(f->fx, "tint", f->tint);
	while (gs_effect_loop(f->fx, "Combine"))
		gs_draw_sprite(input, 0, cx, cy);
	gs_blend_state_pop();
}

static obs_properties_t *bloom_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "intensity",
		obs_module_text("Bloom.Intensity"), 0.0, 200.0, 1.0);
	obs_properties_add_float_slider(basic, "threshold",
		obs_module_text("Bloom.Threshold"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "knee",
		obs_module_text("Bloom.Knee"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "radius",
		obs_module_text("Bloom.Radius"), 0.0, 100.0, 1.0);

	obs_properties_add_color(adv, "tint", obs_module_text("Bloom.Tint"));
	obs_property_t *q = obs_properties_add_list(adv, "quality",
		obs_module_text("Quality"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(q, obs_module_text("QualityLow"), 0);
	obs_property_list_add_int(q, obs_module_text("QualityMed"), 1);
	obs_property_list_add_int(q, obs_module_text("QualityHigh"), 2);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void bloom_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "intensity", 80.0);
	obs_data_set_default_double(s, "threshold", 70.0);
	obs_data_set_default_double(s, "knee", 50.0);
	obs_data_set_default_double(s, "radius", 50.0);
	obs_data_set_default_int(s, "tint", 0xFFFFFFFF);
	obs_data_set_default_int(s, "quality", 1);
}

struct obs_source_info sfx_bloom_info = {
	.id             = "sfx_bloom",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = bloom_get_name,
	.create         = bloom_create,
	.destroy        = bloom_destroy,
	.update         = bloom_update,
	.video_render   = bloom_render,
	.get_properties = bloom_properties,
	.get_defaults   = bloom_defaults,
};

void sfx_register_bloom(void)
{
	obs_register_source(&sfx_bloom_info);
}
