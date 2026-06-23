#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct vhs_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	float elapsed;

	float strength;
	float chroma_bleed;
	float noise;
	float scanline;
	float tracking_amount;
	float tracking_speed;
	float glitch_freq;
	float glitch_intensity;
	float desaturate;
};

static const char *vhs_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("VHS");
}

static void vhs_update(void *data, obs_data_t *s)
{
	struct vhs_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->chroma_bleed = (float)obs_data_get_double(s, "chroma_bleed") / 100.0f;
	f->noise = (float)obs_data_get_double(s, "noise") / 100.0f;
	f->scanline = (float)obs_data_get_double(s, "scanline") / 100.0f;
	f->tracking_amount = (float)obs_data_get_double(s, "tracking_amount") / 100.0f;
	f->tracking_speed = (float)obs_data_get_double(s, "tracking_speed") / 100.0f;
	f->glitch_freq = (float)obs_data_get_double(s, "glitch_freq") / 100.0f;
	f->glitch_intensity = (float)obs_data_get_double(s, "glitch_intensity") / 100.0f;
	f->desaturate = (float)obs_data_get_double(s, "desaturate") / 100.0f;
}

static void vhs_tick(void *data, float seconds)
{
	struct vhs_filter *f = data;
	f->elapsed += seconds;
	if (f->elapsed > 3600.0f)
		f->elapsed = 0.0f;
}

static void *vhs_create(obs_data_t *settings, obs_source_t *context)
{
	struct vhs_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/vhs.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load vhs.effect (%s)",
				path);
		bfree(path);
	}
	obs_leave_graphics();
	vhs_update(f, settings);
	return f;
}

static void vhs_destroy(void *data)
{
	struct vhs_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void vhs_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct vhs_filter *f = data;
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
	sfx_set_float(f->fx, "chroma_bleed", f->chroma_bleed);
	sfx_set_float(f->fx, "noise", f->noise);
	sfx_set_float(f->fx, "scanline", f->scanline);
	sfx_set_float(f->fx, "tracking_amount", f->tracking_amount);
	sfx_set_float(f->fx, "tracking_speed", f->tracking_speed);
	sfx_set_float(f->fx, "glitch_freq", f->glitch_freq);
	sfx_set_float(f->fx, "glitch_intensity", f->glitch_intensity);
	sfx_set_float(f->fx, "desaturate", f->desaturate);
	sfx_set_float(f->fx, "time", f->elapsed);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

static obs_properties_t *vhs_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "chroma_bleed",
		obs_module_text("VHS.ChromaBleed"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "noise",
		obs_module_text("VHS.Noise"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "scanline",
		obs_module_text("VHS.Scanline"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "tracking_amount",
		obs_module_text("VHS.TrackingAmount"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "glitch_freq",
		obs_module_text("VHS.GlitchFreq"), 0.0, 100.0, 1.0);

	obs_properties_add_float_slider(adv, "tracking_speed",
		obs_module_text("VHS.TrackingSpeed"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "glitch_intensity",
		obs_module_text("VHS.GlitchIntensity"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "desaturate",
		obs_module_text("VHS.Desaturate"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void vhs_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "chroma_bleed", 50.0);
	obs_data_set_default_double(s, "noise", 40.0);
	obs_data_set_default_double(s, "scanline", 35.0);
	obs_data_set_default_double(s, "tracking_amount", 30.0);
	obs_data_set_default_double(s, "tracking_speed", 20.0);
	obs_data_set_default_double(s, "glitch_freq", 15.0);
	obs_data_set_default_double(s, "glitch_intensity", 40.0);
	obs_data_set_default_double(s, "desaturate", 15.0);
}

struct obs_source_info sfx_vhs_info = {
	.id             = "sfx_vhs",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = vhs_get_name,
	.create         = vhs_create,
	.destroy        = vhs_destroy,
	.update         = vhs_update,
	.video_tick     = vhs_tick,
	.video_render   = vhs_render,
	.get_properties = vhs_properties,
	.get_defaults   = vhs_defaults,
};

void sfx_register_vhs(void)
{
	obs_register_source(&sfx_vhs_info);
}
