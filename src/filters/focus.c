#include "../sfx-common.h"
#include "../sfx-render.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/bmem.h>
#include <plugin-support.h>

struct focus_filter {
	obs_source_t *context;
	gs_effect_t *fx;
	struct sfx_blur blur;
	gs_texrender_t *input;

	float strength;
	float center_x;
	float center_y;
	float inner_radius;
	float falloff;
	float curve;
	float max_blur;
	int shape; /* 0 circle, 1 ellipse */
	float aspect;
	int quality;
};

static const char *focus_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Focus");
}

static void focus_update(void *data, obs_data_t *s)
{
	struct focus_filter *f = data;
	f->strength = (float)obs_data_get_double(s, "strength") / 100.0f;
	f->center_x = (float)obs_data_get_double(s, "center_x") / 100.0f;
	f->center_y = (float)obs_data_get_double(s, "center_y") / 100.0f;
	f->inner_radius = (float)obs_data_get_double(s, "inner_radius") / 100.0f;
	f->falloff = (float)obs_data_get_double(s, "falloff") / 100.0f;
	f->curve = (float)obs_data_get_double(s, "curve");
	f->max_blur = (float)obs_data_get_double(s, "max_blur") / 100.0f;
	f->shape = (int)obs_data_get_int(s, "shape");
	f->aspect = (float)obs_data_get_double(s, "aspect");
	f->quality = (int)obs_data_get_int(s, "quality");
}

static void *focus_create(obs_data_t *settings, obs_source_t *context)
{
	struct focus_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	obs_enter_graphics();
	char *path = obs_module_file("effects/focus.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load focus.effect (%s)",
				path);
		bfree(path);
	}
	f->input = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	sfx_blur_init(&f->blur);
	obs_leave_graphics();
	focus_update(f, settings);
	return f;
}

static void focus_destroy(void *data)
{
	struct focus_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	if (f->input)
		gs_texrender_destroy(f->input);
	sfx_blur_free(&f->blur);
	obs_leave_graphics();
	bfree(f);
}

static void focus_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct focus_filter *f = data;
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
	int base = f->quality == 0 ? 1 : (f->quality == 2 ? 4 : 2);
	int iterations = base + (int)(f->max_blur * 6.0f);
	gs_texture_t *blurred = sfx_blur_run(&f->blur, input, bw, bh,
					     iterations);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_enable_depth_test(false);
	sfx_set_texture(f->fx, "image", input);
	sfx_set_texture(f->fx, "blur_tex", blurred);
	sfx_set_float(f->fx, "strength", f->strength);
	sfx_set_vec2(f->fx, "center", f->center_x, f->center_y);
	sfx_set_float(f->fx, "inner_radius", f->inner_radius);
	sfx_set_float(f->fx, "falloff", f->falloff);
	sfx_set_float(f->fx, "curve", f->curve);
	sfx_set_float(f->fx, "max_blur", f->max_blur);
	sfx_set_float(f->fx, "shape", (float)f->shape);
	sfx_set_float(f->fx, "aspect", f->aspect);
	sfx_set_vec2(f->fx, "uv_size", (float)cx, (float)cy);
	while (gs_effect_loop(f->fx, "Draw"))
		gs_draw_sprite(input, 0, cx, cy);
	gs_blend_state_pop();
}

static obs_properties_t *focus_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "strength",
		obs_module_text("Strength"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "center_x",
		obs_module_text("Focus.CenterX"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "center_y",
		obs_module_text("Focus.CenterY"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "inner_radius",
		obs_module_text("Focus.InnerRadius"), 0.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "falloff",
		obs_module_text("Focus.Falloff"), 1.0, 100.0, 1.0);
	obs_properties_add_float_slider(basic, "curve",
		obs_module_text("Focus.Curve"), 0.2, 5.0, 0.1);
	obs_properties_add_float_slider(basic, "max_blur",
		obs_module_text("Focus.MaxBlur"), 0.0, 100.0, 1.0);

	obs_property_t *sh = obs_properties_add_list(adv, "shape",
		obs_module_text("Focus.Shape"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(sh, obs_module_text("Focus.ShapeCircle"), 0);
	obs_property_list_add_int(sh, obs_module_text("Focus.ShapeEllipse"), 1);
	obs_properties_add_float_slider(adv, "aspect",
		obs_module_text("Focus.Aspect"), 0.2, 5.0, 0.1);
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

static void focus_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "strength", 100.0);
	obs_data_set_default_double(s, "center_x", 50.0);
	obs_data_set_default_double(s, "center_y", 50.0);
	obs_data_set_default_double(s, "inner_radius", 20.0);
	obs_data_set_default_double(s, "falloff", 40.0);
	obs_data_set_default_double(s, "curve", 1.5);
	obs_data_set_default_double(s, "max_blur", 70.0);
	obs_data_set_default_int(s, "shape", 0);
	obs_data_set_default_double(s, "aspect", 1.0);
	obs_data_set_default_int(s, "quality", 1);
}

struct obs_source_info sfx_focus_info = {
	.id             = "sfx_focus",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = focus_get_name,
	.create         = focus_create,
	.destroy        = focus_destroy,
	.update         = focus_update,
	.video_render   = focus_render,
	.get_properties = focus_properties,
	.get_defaults   = focus_defaults,
};

void sfx_register_focus(void)
{
	obs_register_source(&sfx_focus_info);
}
