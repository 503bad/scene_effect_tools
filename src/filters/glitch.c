#include "../sfx-common.h"
#include "../sfx-render.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct glitch_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	struct sfx_blur blur;
	gs_texrender_t *input;
	gs_texrender_t *bright;
	float elapsed;

	float strength;
	float frequency;
	float intensity;
	float chroma_shift;
	float slice_amount;
	float bloom_intensity;
	float bloom_threshold;
	float scanline;
	uint32_t overlay_color;
	float overlay_amount;
};

static const char *glitch_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Glitch");
}

static void glitch_update(void *data, obs_data_t *s)
{
	struct glitch_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->frequency = (float)obs_data_get_double(s, "frequency") / 100.0f;
	f->intensity = (float)obs_data_get_double(s, "intensity") / 100.0f;
	f->chroma_shift = (float)obs_data_get_double(s, "chroma_shift") / 100.0f;
	f->slice_amount = (float)obs_data_get_double(s, "slice_amount") / 100.0f;
	f->bloom_intensity = (float)obs_data_get_double(s, "bloom_intensity") / 100.0f;
	f->bloom_threshold = (float)obs_data_get_double(s, "bloom_threshold") / 100.0f;
	f->scanline = (float)obs_data_get_double(s, "scanline") / 100.0f;
	f->overlay_color = (uint32_t)obs_data_get_int(s, "overlay_color");
	f->overlay_amount = (float)obs_data_get_double(s, "overlay_amount") / 100.0f;
}

static void glitch_tick(void *data, float seconds)
{
	struct glitch_filter *f = data;
	f->elapsed += seconds;
	if (f->elapsed > 3600.0f)
		f->elapsed = 0.0f;
}

static void *glitch_create(obs_data_t *settings, obs_source_t *context)
{
	struct glitch_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/glitch.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load glitch.effect (%s)",
				path);
		bfree(path);
	}
	f->input = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->bright = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	sfx_blur_init(&f->blur);
	obs_leave_graphics();
	glitch_update(f, settings);
	return f;
}

static void glitch_destroy(void *data)
{
	struct glitch_filter *f = data;
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

static gs_texture_t *render_brightpass(struct glitch_filter *f,
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
	sfx_set_float(f->fx, "bloom_threshold", f->bloom_threshold);
	while (gs_effect_loop(f->fx, "BrightPass"))
		gs_draw_sprite(src, 0, bw, bh);
	gs_blend_state_pop();
	gs_texrender_end(f->bright);
	return gs_texrender_get_texture(f->bright);
}

static void glitch_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct glitch_filter *f = data;
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

	uint32_t bw = cx / 2 > 0 ? cx / 2 : 1;
	uint32_t bh = cy / 2 > 0 ? cy / 2 : 1;
	gs_texture_t *bright = render_brightpass(f, input, bw, bh);
	gs_texture_t *blurred = bright
				? sfx_blur_run(&f->blur, bright, bw, bh, 3)
				: NULL;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_enable_depth_test(false);
	sfx_set_texture(f->fx, "image", input);
	sfx_set_texture(f->fx, "bloom_tex", blurred ? blurred : input);
	sfx_set_float(f->fx, "strength", f->strength);
	sfx_set_float(f->fx, "frequency", f->frequency);
	sfx_set_float(f->fx, "intensity", f->intensity);
	sfx_set_float(f->fx, "chroma_shift", f->chroma_shift);
	sfx_set_float(f->fx, "slice_amount", f->slice_amount);
	sfx_set_float(f->fx, "bloom_intensity",
		      blurred ? f->bloom_intensity : 0.0f);
	sfx_set_float(f->fx, "scanline", f->scanline);
	sfx_set_color(f->fx, "overlay_color", f->overlay_color);
	sfx_set_float(f->fx, "overlay_amount", f->overlay_amount);
	sfx_set_float(f->fx, "time", f->elapsed);
	sfx_set_vec2(f->fx, "uv_size", (float)cx, (float)cy);
	while (gs_effect_loop(f->fx, "Combine"))
		gs_draw_sprite(input, 0, cx, cy);
	gs_blend_state_pop();
}

static obs_properties_t *glitch_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Glitch.Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "frequency",
		obs_module_text("Glitch.Frequency"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "intensity",
		obs_module_text("Glitch.Intensity"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "chroma_shift",
		obs_module_text("Glitch.ChromaShift"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "slice_amount",
		obs_module_text("Glitch.SliceAmount"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "bloom_intensity",
		obs_module_text("Glitch.BloomIntensity"), 0.0, 200.0, 1.0);
	obs_properties_add_float_slider(basic, "scanline",
		obs_module_text("Glitch.Scanline"), 0.0, 100.0, 1.0);

	obs_properties_add_float_slider(adv, "bloom_threshold",
		obs_module_text("Glitch.BloomThreshold"), 0.0, 100.0, 1.0);
	obs_properties_add_color(adv, "overlay_color",
		obs_module_text("Glitch.OverlayColor"));
	obs_properties_add_float_slider(adv, "overlay_amount",
		obs_module_text("Glitch.OverlayAmount"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void glitch_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "frequency", 30.0);
	obs_data_set_default_double(s, "intensity", 50.0);
	obs_data_set_default_double(s, "chroma_shift", 40.0);
	obs_data_set_default_double(s, "slice_amount", 50.0);
	obs_data_set_default_double(s, "bloom_intensity", 60.0);
	obs_data_set_default_double(s, "bloom_threshold", 70.0);
	obs_data_set_default_double(s, "scanline", 30.0);
	obs_data_set_default_int(s, "overlay_color", 0xFFFF00FF); /* magenta */
	obs_data_set_default_double(s, "overlay_amount", 20.0);
}

struct obs_source_info sfx_glitch_info = {
	.id             = "sfx_glitch",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = glitch_get_name,
	.create         = glitch_create,
	.destroy        = glitch_destroy,
	.update         = glitch_update,
	.video_tick     = glitch_tick,
	.video_render   = glitch_render,
	.get_properties = glitch_properties,
	.get_defaults   = glitch_defaults,
};

void sfx_register_glitch(void)
{
	obs_register_source(&sfx_glitch_info);
}
