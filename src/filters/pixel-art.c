#include "../sfx-common.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

/* color_count enum values */
#define CC_16   16
#define CC_32   32
#define CC_256  256
#define CC_FULL 0

/* palette enum values (must match shader pal_lookup) */
#define PAL_CLASSIC16 0
#define PAL_GAMEBOY   1
#define PAL_PC98      2
#define PAL_MONO      3

struct pixel_art_filter {
	obs_source_t *context;
	gs_effect_t *fx;

	float strength;
	int pixel_size;
	int color_count;
	int palette;
	int palette_count;
	float dither_threshold;
	float dither_strength;
};

static const char *pixel_art_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("PixelArt");
}

static int palette_entry_count(int palette)
{
	switch (palette) {
	case PAL_GAMEBOY:
		return 4;
	case PAL_MONO:
		return 2;
	default:
		return 16;
	}
}

static void pixel_art_update(void *data, obs_data_t *s)
{
	struct pixel_art_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->pixel_size = (int)obs_data_get_int(s, "pixel_size");
	f->color_count = (int)obs_data_get_int(s, "color_count");
	f->palette = (int)obs_data_get_int(s, "palette");
	f->palette_count = palette_entry_count(f->palette);
	f->dither_threshold = (float)obs_data_get_double(s, "dither_threshold") / 100.0f;
	f->dither_strength = (float)obs_data_get_double(s, "dither_strength") / 100.0f;
}

static void *pixel_art_create(obs_data_t *settings, obs_source_t *context)
{
	struct pixel_art_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/pixel_art.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR,
				"failed to load pixel_art.effect (%s)", path);
		bfree(path);
	}
	obs_leave_graphics();
	pixel_art_update(f, settings);
	return f;
}

static void pixel_art_destroy(void *data)
{
	struct pixel_art_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	obs_leave_graphics();
	bfree(f);
}

static void pixel_art_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct pixel_art_filter *f = data;
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
	sfx_set_float(f->fx, "pixel_size", (float)f->pixel_size);
	sfx_set_int(f->fx, "color_count", f->color_count);
	sfx_set_int(f->fx, "palette", f->palette);
	sfx_set_int(f->fx, "palette_count", f->palette_count);
	sfx_set_float(f->fx, "dither_threshold", f->dither_threshold);
	sfx_set_float(f->fx, "dither_strength", f->dither_strength);
	sfx_set_vec2(f->fx, "uv_size", (float)w, (float)h);

	obs_source_process_filter_end(f->context, f->fx, 0, 0);
}

/* Reveal palette / dither controls only in 16-colour mode. */
static bool on_color_count_changed(void *priv, obs_properties_t *props,
				   obs_property_t *prop, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(prop);
	int cc = (int)obs_data_get_int(settings, "color_count");
	bool is16 = cc == CC_16;
	bool quantized = cc != CC_FULL; /* 16 / 32 / 256 all dither */
	obs_property_t *pal = obs_properties_get(props, "palette");
	obs_property_t *dt = obs_properties_get(props, "dither_threshold");
	obs_property_t *ds = obs_properties_get(props, "dither_strength");
	if (pal)
		obs_property_set_visible(pal, is16);
	if (dt)
		obs_property_set_visible(dt, is16);
	if (ds)
		obs_property_set_visible(ds, quantized);
	return true;
}

static obs_properties_t *pixel_art_properties(void *data)
{
	struct pixel_art_filter *f = data;
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_int_slider(basic, "pixel_size",
		obs_module_text("PixelArt.PixelSize"), 1, 64, 1);

	obs_property_t *cc = obs_properties_add_list(basic, "color_count",
		obs_module_text("PixelArt.ColorCount"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(cc, obs_module_text("PixelArt.CC16"), CC_16);
	obs_property_list_add_int(cc, obs_module_text("PixelArt.CC32"), CC_32);
	obs_property_list_add_int(cc, obs_module_text("PixelArt.CC256"), CC_256);
	obs_property_list_add_int(cc, obs_module_text("PixelArt.CCFull"), CC_FULL);
	obs_property_set_modified_callback2(cc, on_color_count_changed, NULL);

	obs_property_t *pal = obs_properties_add_list(basic, "palette",
		obs_module_text("PixelArt.Palette"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(pal, obs_module_text("PixelArt.PalClassic16"),
				  PAL_CLASSIC16);
	obs_property_list_add_int(pal, obs_module_text("PixelArt.PalGameBoy"),
				  PAL_GAMEBOY);
	obs_property_list_add_int(pal, obs_module_text("PixelArt.PalPC98"),
				  PAL_PC98);
	obs_property_list_add_int(pal, obs_module_text("PixelArt.PalMono"),
				  PAL_MONO);

	obs_properties_add_float_slider(basic, "dither_threshold",
		obs_module_text("PixelArt.DitherThreshold"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(adv, "dither_strength",
		obs_module_text("PixelArt.DitherStrength"), 0.0, 100.0, 1.0);

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);

	/* Initial visibility based on current mode. */
	int cur = f ? f->color_count : CC_256;
	bool is16 = cur == CC_16;
	bool quantized = cur != CC_FULL;
	obs_property_set_visible(obs_properties_get(basic, "palette"), is16);
	obs_property_set_visible(obs_properties_get(basic, "dither_threshold"),
				 is16);
	obs_property_set_visible(obs_properties_get(adv, "dither_strength"),
				 quantized);
	return p;
}

static void pixel_art_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_int(s, "pixel_size", 8);
	obs_data_set_default_int(s, "color_count", CC_256);
	obs_data_set_default_int(s, "palette", PAL_CLASSIC16);
	obs_data_set_default_double(s, "dither_threshold", 35.0);
	obs_data_set_default_double(s, "dither_strength", 80.0);
}

struct obs_source_info sfx_pixel_art_info = {
	.id             = "sfx_pixel_art",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = pixel_art_get_name,
	.create         = pixel_art_create,
	.destroy        = pixel_art_destroy,
	.update         = pixel_art_update,
	.video_render   = pixel_art_render,
	.get_properties = pixel_art_properties,
	.get_defaults   = pixel_art_defaults,
};

void sfx_register_pixel_art(void)
{
	obs_register_source(&sfx_pixel_art_info);
}
