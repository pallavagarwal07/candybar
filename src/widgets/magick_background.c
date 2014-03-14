#include "widgets.h"
#include "magick_background.h"

void*
widget_init (struct widget *widget) {
	LOG_DEBUG("init");

	struct widget_config config = widget_config_defaults;
	widget_init_config_string(widget, "image", config.image);
	widget_init_config_string(widget, "css_gradient_overlay_start", config.css_gradient_overlay_start);
	widget_init_config_string(widget, "css_gradient_overlay_end", config.css_gradient_overlay_end);
	widget_init_config_integer(widget, "blur_radius", config.blur_radius);
	widget_init_config_integer(widget, "brightness", config.brightness);
	widget_init_config_integer(widget, "height", config.height);
	widget_init_config_integer(widget, "saturation", config.saturation);

	if (!strlen(config.image)) {
		LOG_WARN("'image' config property not set, disabling widget");

		return 0;
	}

	MagickWand *m_wand = NULL;
	MagickPassFail status = MagickPass;
	int width, height;

	InitializeMagick(NULL);
	m_wand = NewMagickWand();

	status = MagickReadImage(m_wand, config.image);

	if (status != MagickPass) {
		LOG_ERR("could not read image %s", config.image);

		return 0;
	}

	width = MagickGetImageWidth(m_wand);
	height = config.height;

	/* modify image */
	MagickCropImage(m_wand, width, height, 0, 0);
	if (config.blur_radius != 0) {
		MagickBlurImage(m_wand, 0, config.blur_radius);
	}
	if ((config.brightness != 100) || (config.saturation != 100)) {
		MagickModulateImage(m_wand, config.brightness, config.saturation, 100);
	}

	/* get image jpg data and encode it to base64 */
	MagickSetCompressionQuality(m_wand, 95);
	MagickSetFormat(m_wand, "jpg");

	size_t img_len;
	unsigned char *img_data = MagickWriteImageBlob(m_wand, &img_len);
	char *img_base64 = g_base64_encode(img_data, img_len);

	json_t *json_data_object = json_object();
	json_object_set_new(json_data_object, "image", json_string(img_base64));
	json_object_set_new(json_data_object, "gradient_start", json_string(config.css_gradient_overlay_start));
	json_object_set_new(json_data_object, "gradient_end", json_string(config.css_gradient_overlay_end));

	widget_send_update(json_data_object, widget);

	g_free(img_base64);
	DestroyMagickWand(m_wand);
	DestroyMagick();

	return 0;
}