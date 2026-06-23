#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct cartoon_filter {
	obs_source_t *context;
	gs_effect_t *fx;

	float strength;
	float edge_strength;
	float edge_width;
	int color_levels;
	float smoothing;
	float saturation;
};

static const char *cartoon_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Cartoon");
}

static void cartoon_update(void *data, obs_data_t *s)
{
	struct cartoon_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->edge_strength = (float)obs_data_get_double(s, "edge_strength") / 100.0f;
	f->edge_width = (float)obs_data_get_double(s, "edge_width");
	f->color_levels = (int)obs_data_get_int(s, "color_levels");
	f->smoothing = (float)obs_data_get_double(s, "smoothing") / 100.0f;
	f->saturation = (float)obs_data_get_double(s, "saturation") / 100.0f;
}

static void *cartoon_create(obs_data_t *settings, obs_source_t *context)
{
	struct cartoon_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/cartoon.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load cartoon.effect (%s)",
				path);
		bfree(path);
	}
	obs_leave_graphics();
	cartoon_update(f, settings);
	return f;
}

static void cartoon_destroy(void *data)
{
	struct cartoon_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void cartoon_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct cartoon_filter *f = data;
	if (!f->fx) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t w, h;
	sfx_target_size(f->context, &w, &h);
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	sfx_set_float(f->fx, "strength", f->strength);
	sfx_set_float(f->fx, "edge_strength", f->edge_strength);
	sfx_set_float(f->fx, "edge_width", f->edge_width);
	sfx_set_float(f->fx, "color_levels", (float)f->color_levels);
	sfx_set_float(f->fx, "smoothing", f->smoothing);
	sfx_set_float(f->fx, "saturation", f->saturation);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

static obs_properties_t *cartoon_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "edge_strength",
		obs_module_text("Cartoon.EdgeStrength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "edge_width",
		obs_module_text("Cartoon.EdgeWidth"), 0.5, 4.0, 0.1);
	obs_properties_add_int_slider(basic, "color_levels",
		obs_module_text("Cartoon.ColorLevels"), 2, 32, 1);

	obs_properties_add_float_slider(adv, "smoothing",
		obs_module_text("Cartoon.Smoothing"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "saturation",
		obs_module_text("Cartoon.Saturation"), -50.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void cartoon_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "edge_strength", 60.0);
	obs_data_set_default_double(s, "edge_width", 1.5);
	obs_data_set_default_int(s, "color_levels", 8);
	obs_data_set_default_double(s, "smoothing", 40.0);
	obs_data_set_default_double(s, "saturation", 20.0);
}

struct obs_source_info sfx_cartoon_info = {
	.id             = "sfx_cartoon",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = cartoon_get_name,
	.create         = cartoon_create,
	.destroy        = cartoon_destroy,
	.update         = cartoon_update,
	.video_render   = cartoon_render,
	.get_properties = cartoon_properties,
	.get_defaults   = cartoon_defaults,
};

void sfx_register_cartoon(void)
{
	obs_register_source(&sfx_cartoon_info);
}
