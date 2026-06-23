#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct old_film_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	float elapsed;

	float strength;
	float sepia_amount;
	float grain;
	float flicker;
	int scratch_count;
	float scratch_speed;
	float scratch_intensity;
	float vignette;
	float jitter;
};

static const char *old_film_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("OldFilm");
}

static void old_film_update(void *data, obs_data_t *s)
{
	struct old_film_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->sepia_amount = (float)obs_data_get_double(s, "sepia_amount") / 100.0f;
	f->grain = (float)obs_data_get_double(s, "grain") / 100.0f;
	f->flicker = (float)obs_data_get_double(s, "flicker") / 100.0f;
	f->scratch_count = (int)obs_data_get_int(s, "scratch_count");
	f->scratch_speed = (float)obs_data_get_double(s, "scratch_speed") / 100.0f;
	f->scratch_intensity = (float)obs_data_get_double(s, "scratch_intensity") / 100.0f;
	f->vignette = (float)obs_data_get_double(s, "vignette") / 100.0f;
	f->jitter = (float)obs_data_get_double(s, "jitter") / 100.0f;
}

static void old_film_tick(void *data, float seconds)
{
	struct old_film_filter *f = data;
	f->elapsed += seconds;
	if (f->elapsed > 3600.0f)
		f->elapsed = 0.0f;
}

static void *old_film_create(obs_data_t *settings, obs_source_t *context)
{
	struct old_film_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/old_film.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR,
				"failed to load old_film.effect (%s)", path);
		bfree(path);
	}
	obs_leave_graphics();
	old_film_update(f, settings);
	return f;
}

static void old_film_destroy(void *data)
{
	struct old_film_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void old_film_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct old_film_filter *f = data;
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
	sfx_set_float(f->fx, "sepia_amount", f->sepia_amount);
	sfx_set_float(f->fx, "grain", f->grain);
	sfx_set_float(f->fx, "flicker", f->flicker);
	sfx_set_float(f->fx, "scratch_count", (float)f->scratch_count);
	sfx_set_float(f->fx, "scratch_speed", f->scratch_speed);
	sfx_set_float(f->fx, "scratch_intensity", f->scratch_intensity);
	sfx_set_float(f->fx, "vignette", f->vignette);
	sfx_set_float(f->fx, "jitter", f->jitter);
	sfx_set_float(f->fx, "time", f->elapsed);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

static obs_properties_t *old_film_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "sepia_amount",
		obs_module_text("OldFilm.SepiaAmount"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "grain",
		obs_module_text("OldFilm.Grain"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "flicker",
		obs_module_text("OldFilm.Flicker"), 0.0, 100.0, 1.0);
	obs_properties_add_int_slider(basic, "scratch_count",
		obs_module_text("OldFilm.ScratchCount"), 0, 12, 1);

	obs_properties_add_float_slider(adv, "scratch_speed",
		obs_module_text("OldFilm.ScratchSpeed"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "scratch_intensity",
		obs_module_text("OldFilm.ScratchIntensity"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "vignette",
		obs_module_text("Vignette"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "jitter",
		obs_module_text("OldFilm.Jitter"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void old_film_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "sepia_amount", 80.0);
	obs_data_set_default_double(s, "grain", 45.0);
	obs_data_set_default_double(s, "flicker", 30.0);
	obs_data_set_default_int(s, "scratch_count", 3);
	obs_data_set_default_double(s, "scratch_speed", 25.0);
	obs_data_set_default_double(s, "scratch_intensity", 60.0);
	obs_data_set_default_double(s, "vignette", 40.0);
	obs_data_set_default_double(s, "jitter", 15.0);
}

struct obs_source_info sfx_old_film_info = {
	.id             = "sfx_old_film",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = old_film_get_name,
	.create         = old_film_create,
	.destroy        = old_film_destroy,
	.update         = old_film_update,
	.video_tick     = old_film_tick,
	.video_render   = old_film_render,
	.get_properties = old_film_properties,
	.get_defaults   = old_film_defaults,
};

void sfx_register_old_film(void)
{
	obs_register_source(&sfx_old_film_info);
}
