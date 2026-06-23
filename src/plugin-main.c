#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* Each filter lives in its own translation unit and registers itself. */
void sfx_register_sepia(void);
void sfx_register_cartoon(void);
void sfx_register_pixel_art(void);
void sfx_register_insta_grade(void);
void sfx_register_old_film(void);
void sfx_register_vhs(void);
void sfx_register_bloom(void);
void sfx_register_focus(void);
void sfx_register_crunch(void);
void sfx_register_glitch(void);

bool obs_module_load(void)
{
	sfx_register_sepia();
	sfx_register_cartoon();
	sfx_register_pixel_art();
	sfx_register_insta_grade();
	sfx_register_old_film();
	sfx_register_vhs();
	sfx_register_bloom();
	sfx_register_focus();
	sfx_register_crunch();
	sfx_register_glitch();

	obs_log(LOG_INFO, "screen-effect-tools loaded (version %s)",
		PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "screen-effect-tools unloaded");
}
