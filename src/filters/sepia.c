#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct sepia_filter {
	obs_source_t *context;
	gs_effect_t *fx;

	float strength;
	uint32_t tone;
	float contrast;
	float vignette;
};

static const char *sepia_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Sepia");
}

static void sepia_update(void *data, obs_data_t *s)
{
	struct sepia_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->tone = (uint32_t)obs_data_get_int(s, "tone_color");
	f->contrast = (float)obs_data_get_double(s, "contrast") / 100.0f;
	f->vignette = (float)obs_data_get_double(s, "vignette") / 100.0f;
}

static void *sepia_create(obs_data_t *settings, obs_source_t *context)
{
	struct sepia_filter *f = bzalloc(sizeof(*f));
	f->context = context;

	obs_enter_graphics();
	char *path = obs_module_file("effects/sepia.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load sepia.effect (%s)",
				path);
		bfree(path);
	}
	obs_leave_graphics();

	sepia_update(f, settings);
	return f;
}

static void sepia_destroy(void *data)
{
	struct sepia_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void sepia_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct sepia_filter *f = data;

	if (!f->fx) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t w, h;
	sfx_target_size(f->context, &w, &h);

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	sfx_set_float(f->fx, "strength", f->strength);
	sfx_set_color(f->fx, "tone_color", f->tone);
	sfx_set_float(f->fx, "contrast", f->contrast);
	sfx_set_float(f->fx, "vignette", f->vignette);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

static obs_properties_t *sepia_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_color(basic, "tone_color",
		obs_module_text("Sepia.ToneColor"));

	obs_properties_add_float_slider(adv, "contrast",
		obs_module_text("Sepia.Contrast"), -50.0, 50.0, 1.0);
	obs_properties_add_float_slider(adv, "vignette",
		obs_module_text("Vignette"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void sepia_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_int(s, "tone_color", 0xFF144270); /* #704214 */
	obs_data_set_default_double(s, "contrast", 10.0);
	obs_data_set_default_double(s, "vignette", 25.0);
}

struct obs_source_info sfx_sepia_info = {
	.id             = "sfx_sepia",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = sepia_get_name,
	.create         = sepia_create,
	.destroy        = sepia_destroy,
	.update         = sepia_update,
	.video_render   = sepia_render,
	.get_properties = sepia_properties,
	.get_defaults   = sepia_defaults,
};

void sfx_register_sepia(void)
{
	obs_register_source(&sfx_sepia_info);
}
