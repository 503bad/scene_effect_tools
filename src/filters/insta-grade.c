#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <math.h>
#include <plugin-support.h>

/* preset ids */
#define PR_TEAL_ORANGE 0
#define PR_FADE        1
#define PR_WARM        2
#define PR_COOL        3
#define PR_VINTAGE     4
#define PR_CUSTOM      5

struct insta_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	float elapsed;

	float strength;
	float temperature;
	float tint;
	float saturation;
	float vibrance;
	float contrast;
	float fade;
	uint32_t highlight_color;
	uint32_t shadow_color;
	float vignette;
	float grain;
};

static const char *insta_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("InstaGrade");
}

static void insta_update(void *data, obs_data_t *s)
{
	struct insta_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->temperature = (float)obs_data_get_double(s, "temperature") / 100.0f;
	f->tint = (float)obs_data_get_double(s, "tint") / 100.0f;
	f->saturation = (float)obs_data_get_double(s, "saturation") / 100.0f;
	f->vibrance = (float)obs_data_get_double(s, "vibrance") / 100.0f;
	f->contrast = (float)obs_data_get_double(s, "contrast") / 100.0f;
	f->fade = (float)obs_data_get_double(s, "fade") / 100.0f;
	f->highlight_color = (uint32_t)obs_data_get_int(s, "highlight_color");
	f->shadow_color = (uint32_t)obs_data_get_int(s, "shadow_color");
	f->vignette = (float)obs_data_get_double(s, "vignette") / 100.0f;
	f->grain = (float)obs_data_get_double(s, "grain") / 100.0f;
}

static void insta_tick(void *data, float seconds)
{
	struct insta_filter *f = data;
	f->elapsed += seconds;
	if (f->elapsed > 3600.0f)
		f->elapsed = 0.0f;
}

static void *insta_create(obs_data_t *settings, obs_source_t *context)
{
	struct insta_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/insta_grade.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR,
				"failed to load insta_grade.effect (%s)", path);
		bfree(path);
	}
	obs_leave_graphics();
	insta_update(f, settings);
	return f;
}

static void insta_destroy(void *data)
{
	struct insta_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void insta_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct insta_filter *f = data;
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
	sfx_set_float(f->fx, "temperature", f->temperature);
	sfx_set_float(f->fx, "tint", f->tint);
	sfx_set_float(f->fx, "saturation", f->saturation);
	sfx_set_float(f->fx, "vibrance", f->vibrance);
	sfx_set_float(f->fx, "contrast", f->contrast);
	sfx_set_float(f->fx, "fade", f->fade);
	sfx_set_color(f->fx, "highlight_color", f->highlight_color);
	sfx_set_color(f->fx, "shadow_color", f->shadow_color);
	sfx_set_float(f->fx, "vignette", f->vignette);
	sfx_set_float(f->fx, "grain", f->grain);
	sfx_set_float(f->fx, "time", f->elapsed);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

/* Apply a preset's parameter bundle into the live settings. */
struct insta_preset {
	double temp, tint, sat, vib, contrast, fade, vignette, grain;
	long long hi, lo; /* OBS 0xAABBGGRR */
};

static void apply_preset(obs_data_t *s, int preset)
{
	struct insta_preset pr;

	switch (preset) {
	case PR_FADE: {
		struct insta_preset v = {5, 0, -10, 10, -10, 35, 10, 8,
					 0xFFC8DCF0, 0xFF503820};
		pr = v;
		break;
	}
	case PR_WARM: {
		struct insta_preset v = {25, 2, 5, 15, 5, 20, 12, 10,
					 0xFF6EC8FF, 0xFF402814};
		pr = v;
		break;
	}
	case PR_COOL: {
		struct insta_preset v = {-25, 5, 5, 10, 8, 8, 12, 5,
					 0xFFF0D8B0, 0xFF604020};
		pr = v;
		break;
	}
	case PR_VINTAGE: {
		struct insta_preset v = {15, 4, -15, 5, 0, 30, 30, 20,
					 0xFF50A0E0, 0xFF604830};
		pr = v;
		break;
	}
	case PR_TEAL_ORANGE:
	default: {
		struct insta_preset v = {10, 0, 0, 25, 15, 15, 18, 8,
					 0xFF3CA0E6, 0xFF806028};
		pr = v;
		break;
	}
	}

	obs_data_set_double(s, "temperature", pr.temp);
	obs_data_set_double(s, "tint", pr.tint);
	obs_data_set_double(s, "saturation", pr.sat);
	obs_data_set_double(s, "vibrance", pr.vib);
	obs_data_set_double(s, "contrast", pr.contrast);
	obs_data_set_double(s, "fade", pr.fade);
	obs_data_set_double(s, "vignette", pr.vignette);
	obs_data_set_double(s, "grain", pr.grain);
	obs_data_set_int(s, "highlight_color", pr.hi);
	obs_data_set_int(s, "shadow_color", pr.lo);
}

static bool on_preset_changed(void *priv, obs_properties_t *props,
			      obs_property_t *prop, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(prop);
	int preset = (int)obs_data_get_int(settings, "preset");
	if (preset != PR_CUSTOM)
		apply_preset(settings, preset);
	/* Refresh so the parametric sliders reflect the preset values. */
	UNUSED_PARAMETER(props);
	return preset != PR_CUSTOM;
}

static obs_properties_t *insta_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_property_t *pr = obs_properties_add_list(basic, "preset",
		obs_module_text("InstaGrade.Preset"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.TealOrange"),
				  PR_TEAL_ORANGE);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.Fade"), PR_FADE);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.Warm"), PR_WARM);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.Cool"), PR_COOL);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.Vintage"),
				  PR_VINTAGE);
	obs_property_list_add_int(pr, obs_module_text("InstaGrade.Custom"),
				  PR_CUSTOM);
	obs_property_set_modified_callback2(pr, on_preset_changed, NULL);

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "temperature",
		obs_module_text("InstaGrade.Temperature"), -100.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "tint",
		obs_module_text("InstaGrade.Tint"), -100.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "saturation",
		obs_module_text("InstaGrade.Saturation"), -100.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "vibrance",
		obs_module_text("InstaGrade.Vibrance"), -100.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "contrast",
		obs_module_text("InstaGrade.Contrast"), -100.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "fade",
		obs_module_text("InstaGrade.Fade2"), 0.0, 100.0, 1.0);

	obs_properties_add_color(adv, "highlight_color",
		obs_module_text("InstaGrade.HighlightColor"));
	obs_properties_add_color(adv, "shadow_color",
		obs_module_text("InstaGrade.ShadowColor"));
	obs_properties_add_float_slider(adv, "vignette",
		obs_module_text("Vignette"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "grain",
		obs_module_text("InstaGrade.Grain"), 0.0, 100.0, 1.0);
	obs_properties_add_path(adv, "lut_path",
		obs_module_text("InstaGrade.LutPath"), OBS_PATH_FILE,
		"Cube LUT (*.cube);;All Files (*.*)", NULL);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void insta_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, "preset", PR_TEAL_ORANGE);
	obs_data_set_default_double(s, "strength", 80.0);
	obs_data_set_default_double(s, "temperature", 10.0);
	obs_data_set_default_double(s, "tint", 0.0);
	obs_data_set_default_double(s, "saturation", 0.0);
	obs_data_set_default_double(s, "vibrance", 20.0);
	obs_data_set_default_double(s, "contrast", 10.0);
	obs_data_set_default_double(s, "fade", 20.0);
	obs_data_set_default_int(s, "highlight_color", 0xFF3CA0E6); /* orange */
	obs_data_set_default_int(s, "shadow_color", 0xFF806028);    /* teal */
	obs_data_set_default_double(s, "vignette", 15.0);
	obs_data_set_default_double(s, "grain", 10.0);
}

struct obs_source_info sfx_insta_grade_info = {
	.id             = "sfx_insta_grade",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = insta_get_name,
	.create         = insta_create,
	.destroy        = insta_destroy,
	.update         = insta_update,
	.video_tick     = insta_tick,
	.video_render   = insta_render,
	.get_properties = insta_properties,
	.get_defaults   = insta_defaults,
};

void sfx_register_insta_grade(void)
{
	obs_register_source(&sfx_insta_grade_info);
}
