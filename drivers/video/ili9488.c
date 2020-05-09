// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 STMicroelectronics - All Rights Reserved
 * Author(s): Yannick Fertre <yannick.fertre@st.com> for STMicroelectronics.
 *            Philippe Cornu <philippe.cornu@st.com> for STMicroelectronics.
 *
 */
#include <common.h>
#include <backlight.h>
#include <dm.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <asm/gpio.h>
#include <power/regulator.h>

#define ILI9488_BACKLIGHT_DEFAULT	240
#define ILI9488_BACKLIGHT_MAX		255

/* Manufacturer Command Set */
#define KD35T133_CMD_INTERFACEMODECTRL		0xb0
#define KD35T133_CMD_FRAMERATECTRL		0xb1
#define KD35T133_CMD_DISPLAYINVERSIONCTRL	0xb4
#define KD35T133_CMD_DISPLAYFUNCTIONCTRL	0xb6
#define KD35T133_CMD_POWERCONTROL1		0xc0
#define KD35T133_CMD_POWERCONTROL2		0xc1
#define KD35T133_CMD_VCOMCONTROL		0xc5
#define KD35T133_CMD_POSITIVEGAMMA		0xe0
#define KD35T133_CMD_NEGATIVEGAMMA		0xe1
#define KD35T133_CMD_SETIMAGEFUNCTION		0xe9
#define KD35T133_CMD_ADJUSTCONTROL3		0xf7

struct ili9488_panel_priv {
	struct udevice *reg;
	struct gpio_desc reset;
};

static const struct display_timing default_timing = {
	.pixelclock.typ		= 17000000,
	.hactive.typ		= 320,
	.hfront_porch.typ	= 3,
	.hback_porch.typ	= 3,
	.hsync_len.typ		= 3,
	.vactive.typ		= 480,
	.vfront_porch.typ	= 3,
	.vback_porch.typ	= 3,
	.vsync_len.typ		= 1,
};

static void ili9488_dcs_write_buf(struct udevice *dev, const void *data,
				   size_t len)
{
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	struct mipi_dsi_device *device = plat->device;

	if (mipi_dsi_dcs_write_buffer(device, data, len) < 0)
		dev_err(dev, "mipi dsi dcs write buffer failed\n");
}

static void ili9488_dcs_write_buf_hs(struct udevice *dev, const void *data,
				      size_t len)
{
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	struct mipi_dsi_device *device = plat->device;

	/* data will be sent in dsi hs mode (ie. no lpm) */
	device->mode_flags &= ~MIPI_DSI_MODE_LPM;

	if (mipi_dsi_dcs_write_buffer(device, data, len) < 0)
		dev_err(dev, "mipi dsi dcs write buffer failed\n");

	/* restore back the dsi lpm mode */
	device->mode_flags |= MIPI_DSI_MODE_LPM;
}

#define dcs_write_seq(dev, seq...)				\
({								\
	static const u8 d[] = { seq };				\
	ili9488_dcs_write_buf(dev, d, ARRAY_SIZE(d));		\
})

#define dcs_write_seq_hs(dev, seq...)				\
({								\
	static const u8 d[] = { seq };				\
	ili9488_dcs_write_buf_hs(dev, d, ARRAY_SIZE(d));	\
})

static int ili9488_init_sequence(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	struct mipi_dsi_device *device = plat->device;
	int ret;

	/* Sleep out & 250ms wait */
	dcs_write_seq(dev, 0x11);
	mdelay(100);

	dcs_write_seq(dev, KD35T133_CMD_POSITIVEGAMMA,
			  0x00, 0x13, 0x18, 0x04, 0x0f, 0x06, 0x3a, 0x56,
			  0x4d, 0x03, 0x0a, 0x06, 0x30, 0x3e, 0x0f);
	dcs_write_seq(dev, KD35T133_CMD_NEGATIVEGAMMA,
			  0x00, 0x13, 0x18, 0x01, 0x11, 0x06, 0x38, 0x34,
			  0x4d, 0x06, 0x0d, 0x0b, 0x31, 0x37, 0x0f);
	dcs_write_seq(dev, KD35T133_CMD_POWERCONTROL1, 0x18, 0x17);
	dcs_write_seq(dev, KD35T133_CMD_POWERCONTROL2, 0x41);
	dcs_write_seq(dev, KD35T133_CMD_VCOMCONTROL, 0x00, 0x1a, 0x80);
	dcs_write_seq(dev, MIPI_DCS_SET_ADDRESS_MODE, 0x48);
	dcs_write_seq(dev, MIPI_DCS_SET_PIXEL_FORMAT, 0x55);
	dcs_write_seq(dev, KD35T133_CMD_INTERFACEMODECTRL, 0x00);
	dcs_write_seq(dev, KD35T133_CMD_FRAMERATECTRL, 0xa0);
	dcs_write_seq(dev, KD35T133_CMD_DISPLAYINVERSIONCTRL, 0x02);
	dcs_write_seq(dev, KD35T133_CMD_DISPLAYFUNCTIONCTRL,
			  0x20, 0x02);
	dcs_write_seq(dev, KD35T133_CMD_SETIMAGEFUNCTION, 0x00);
	dcs_write_seq(dev, KD35T133_CMD_ADJUSTCONTROL3,
			  0xa9, 0x51, 0x2c, 0x82);
	mipi_dsi_dcs_write(device, MIPI_DCS_ENTER_INVERT_MODE, NULL, 0);

	ret =  mipi_dsi_dcs_nop(device);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(device);
	if (ret)
		return ret;

	/* Wait for sleep out exit */
	mdelay(120);

	/* Default portrait 480x800 rgb24 */
	dcs_write_seq(dev, MIPI_DCS_SET_ADDRESS_MODE, 0x00);

	ret =  mipi_dsi_dcs_set_column_address(device, 0,
					       default_timing.hactive.typ - 1);
	if (ret)
		return ret;

	ret =  mipi_dsi_dcs_set_page_address(device, 0,
					     default_timing.vactive.typ - 1);
	if (ret)
		return ret;

	/* See ili9488 driver documentation for pixel format descriptions */
	ret =  mipi_dsi_dcs_set_pixel_format(device, MIPI_DCS_PIXEL_FMT_24BIT |
					     MIPI_DCS_PIXEL_FMT_24BIT << 4);
	if (ret)
		return ret;

	/* Disable CABC feature */
	dcs_write_seq(dev, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	ret = mipi_dsi_dcs_set_display_on(device);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_nop(device);
	if (ret)
		return ret;

	/* Send Command GRAM memory write (no parameters) */
	dcs_write_seq(dev, MIPI_DCS_WRITE_MEMORY_START);

	return 0;
}

static int ili9488_panel_enable_backlight(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	struct mipi_dsi_device *device = plat->device;
	int ret;

	ret = mipi_dsi_attach(device);
	if (ret < 0)
		return ret;

	ret = ili9488_init_sequence(dev);
	if (ret)
		return ret;

	/*
	 * Power on the backlight with the requested brightness
	 * Note We can not use mipi_dsi_dcs_set_display_brightness()
	 * as ili9488 driver support only 8-bit brightness (1 param).
	 */
	dcs_write_seq(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
		      ILI9488_BACKLIGHT_DEFAULT);

	/* Update Brightness Control & Backlight */
	dcs_write_seq(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);

	/* Update Brightness Control & Backlight */
	dcs_write_seq_hs(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY);

	/* Need to wait a few time before sending the first image */
	mdelay(10);

	return 0;
}

static int ili9488_panel_get_display_timing(struct udevice *dev,
					     struct display_timing *timings)
{
	memcpy(timings, &default_timing, sizeof(*timings));

	return 0;
}

static int ili9488_panel_ofdata_to_platdata(struct udevice *dev)
{
	struct ili9488_panel_priv *priv = dev_get_priv(dev);
	int ret;

	if (IS_ENABLED(CONFIG_DM_REGULATOR)) {
		ret =  device_get_supply_regulator(dev, "power-supply",
						   &priv->reg);
		if (ret && ret != -ENOENT) {
			dev_err(dev, "Warning: cannot get power supply\n");
			return ret;
		}
	}

	ret = gpio_request_by_name(dev, "reset-gpios", 0, &priv->reset,
				   GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "warning: cannot get reset GPIO\n");
		if (ret != -ENOENT)
			return ret;
	}

	return 0;
}

static int ili9488_panel_probe(struct udevice *dev)
{
	struct ili9488_panel_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	int ret;

	if (IS_ENABLED(CONFIG_DM_REGULATOR) && priv->reg) {
		dev_dbg(dev, "enable regulator '%s'\n", priv->reg->name);
		ret = regulator_set_enable(priv->reg, true);
		if (ret)
			return ret;
	}

	/* reset panel */
	dm_gpio_set_value(&priv->reset, true);
	mdelay(1); /* >50us */
	dm_gpio_set_value(&priv->reset, false);
	mdelay(10); /* >5ms */

	/* fill characteristics of DSI data link */
	plat->lanes = 1;
	plat->format = MIPI_DSI_FMT_RGB888; 
	plat->mode_flags = MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_MODE_VIDEO_BURST |
			   MIPI_DSI_MODE_EOT_PACKET |
			   MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct panel_ops ili9488_panel_ops = {
	.enable_backlight = ili9488_panel_enable_backlight,
	.get_display_timing = ili9488_panel_get_display_timing,
};

static const struct udevice_id ili9488_panel_ids[] = {
	{ .compatible = "FRD350HXXX,ili9488" },
	{ }
};

U_BOOT_DRIVER(ili9488_panel) = {
	.name			  = "ili9488_panel",
	.id			  = UCLASS_PANEL,
	.of_match		  = ili9488_panel_ids,
	.ops			  = &ili9488_panel_ops,
	.ofdata_to_platdata	  = ili9488_panel_ofdata_to_platdata,
	.probe			  = ili9488_panel_probe,
	.platdata_auto_alloc_size = sizeof(struct mipi_dsi_panel_plat),
	.priv_auto_alloc_size	= sizeof(struct ili9488_panel_priv),
};
