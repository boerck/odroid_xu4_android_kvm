/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <uapi/linux/v4l2-dv-timings.h>

#include "hdmi.h"

#define EDID_SEGMENT_ADDR	(0x60 >> 1)
#define EDID_ADDR		(0xA0 >> 1)
#define EDID_SEGMENT_IGNORE	(2)
#define EDID_BLOCK_SIZE		128
#define EDID_SEGMENT(x)		((x) >> 1)
#define EDID_OFFSET(x)		(((x) & 1) * EDID_BLOCK_SIZE)
#define EDID_EXTENSION_FLAG	0x7E
#define EDID_NATIVE_FORMAT	0x83
#define EDID_BASIC_AUDIO	(1 << 6)
#define EDID_3D_STRUCTURE_ALL	0x1
#define EDID_3D_STRUCTURE_MASK	0x2
#define EDID_3D_FP_MASK		(1)
#define EDID_3D_TB_MASK		(1 << 6)
#define EDID_3D_SBS_MASK	(1 << 8)
#define EDID_3D_FP		0
#define EDID_3D_TB		6
#define EDID_3D_SBS		8

#ifdef CONFIG_OF
static const struct of_device_id edid_device_table[] = {
	{ .compatible = "samsung,exynos5-edid_driver" },
	{},
};
MODULE_DEVICE_TABLE(of, edid_device_table);
#endif

#ifdef CONFIG_MACH_ODROIDXU3
//[*]--------------------------------------------------------------------------------------------------[*]
//
// HDMI PHY Bootargs parsing
//
//[*]--------------------------------------------------------------------------------------------------[*]
unsigned char   HdmiPHYBootArgs[12] = "720p60hz";

// Bootargs parsing
static int __init hdmi_resolution_setup(char *line)
{
	memset(HdmiPHYBootArgs, 0x00, sizeof(HdmiPHYBootArgs));
	sprintf(HdmiPHYBootArgs, "%s", line);
	return 0;
}
__setup("hdmi_phy_res=", hdmi_resolution_setup);

unsigned long   HdmiEDIDBootArgs = 0;

// Bootargs parsing
static int __init hdmi_edid_setup(char *line)
{
	if(kstrtoul(line, 10, &HdmiEDIDBootArgs) != 0)    HdmiEDIDBootArgs = 0;
	return 0;
}
__setup("edid=", hdmi_edid_setup);

unsigned long   HdmiHPDBootArgs = 1;

// Bootargs parsing
static int __init hdmi_hpd_setup(char *line)
{
	if(kstrtoul(line, 10, &HdmiHPDBootArgs) != 0)    HdmiHPDBootArgs = 1;
	return 0;
}
__setup("hpd=", hdmi_hpd_setup);

#endif

static struct i2c_client *edid_client;

/* Structure for Checking 3D Mandatory Format in EDID */
static const struct edid_3d_mandatory_preset {
	struct v4l2_dv_timings dv_timings;
	u16 xres;
	u16 yres;
	u16 refresh;
	u32 vmode;
	u32 s3d;
} edid_3d_mandatory_presets[] = {
	{ V4L2_DV_BT_CEA_1280X720P60_FP,	1280, 720, 60, FB_VMODE_NONINTERLACED, EDID_3D_FP },
	{ V4L2_DV_BT_CEA_1280X720P60_TB,	1280, 720, 60, FB_VMODE_NONINTERLACED, EDID_3D_TB },
	{ V4L2_DV_BT_CEA_1280X720P50_FP,	1280, 720, 50, FB_VMODE_NONINTERLACED, EDID_3D_FP },
	{ V4L2_DV_BT_CEA_1280X720P50_TB,	1280, 720, 50, FB_VMODE_NONINTERLACED, EDID_3D_TB },
	{ V4L2_DV_BT_CEA_1920X1080P24_FP,	1920, 1080, 24, FB_VMODE_NONINTERLACED, EDID_3D_FP },
	{ V4L2_DV_BT_CEA_1920X1080P24_TB,	1920, 1080, 24, FB_VMODE_NONINTERLACED, EDID_3D_TB },
};

static struct edid_preset {
	struct v4l2_dv_timings dv_timings;
	u16 xres;
	u16 yres;
	u16 refresh;
	u32 vmode;
	char *name;
	bool supported;
} edid_presets[] = {
	{ V4L2_DV_BT_CEA_480X320P60,	480, 320, 60, FB_VMODE_NONINTERLACED, "480x320p@60" },
	{ V4L2_DV_BT_DMT_640X480P60,	640, 480, 60, FB_VMODE_NONINTERLACED, "480p@60" },
	{ V4L2_DV_BT_CEA_720X480P59_94,	720, 480, 59, FB_VMODE_NONINTERLACED, "480p@59.94" },
	{ V4L2_DV_BT_CEA_720X576P50,	720, 576, 50, FB_VMODE_NONINTERLACED, "576p@50" },
	{ V4L2_DV_BT_CEA_480X800P60,	480, 800, 60, FB_VMODE_NONINTERLACED, "480x800@60" },
	{ V4L2_DV_BT_CEA_800X480P60,	800, 480, 60, FB_VMODE_NONINTERLACED, "800x480@60" },
	{ V4L2_DV_BT_DMT_800X600P60,	800, 600, 60, FB_VMODE_NONINTERLACED, "600@60" },
	{ V4L2_DV_BT_DMT_848X480P60,	848, 480, 60, FB_VMODE_NONINTERLACED, "848x480@60" },
	{ V4L2_DV_BT_DMT_1024X600P60,	1024, 600, 60, FB_VMODE_NONINTERLACED, "1024x600@60" },
	{ V4L2_DV_BT_DMT_1024X768P60,	1024, 768, 60, FB_VMODE_NONINTERLACED, "768@60" },
	{ V4L2_DV_BT_DMT_1152X864P75,	1152, 864, 75, FB_VMODE_NONINTERLACED, "1152x864@75" },
	{ V4L2_DV_BT_CEA_1280X720P50,	1280, 720,  50, FB_VMODE_NONINTERLACED, "720p@50" },
	{ V4L2_DV_BT_CEA_1280X720P60,	1280, 720,  60, FB_VMODE_NONINTERLACED, "720p@60" },
	{ V4L2_DV_BT_DMT_1280X768P60,	1280, 768, 60, FB_VMODE_NONINTERLACED, "1280x768@60" },
	{ V4L2_DV_BT_DMT_1280X800P60_RB,	1280, 800,  59, FB_VMODE_NONINTERLACED, "800p@59" },
	{ V4L2_DV_BT_DMT_1280X960P60,	1280, 960,  60, FB_VMODE_NONINTERLACED, "960@60" },
	{ V4L2_DV_BT_DMT_1440X900P60,	1440, 900,  60, FB_VMODE_NONINTERLACED, "900@60" },
	{ V4L2_DV_BT_DMT_1400X1050P60, 1400, 1050, 60, FB_VMODE_NONINTERLACED, "1400x1050@60" },
	{ V4L2_DV_BT_DMT_1280X1024P60,	1280, 1024,  60, FB_VMODE_NONINTERLACED, "1024p@60" },
	{ V4L2_DV_BT_DMT_1360X768P60_2,	1360, 768,  60, FB_VMODE_NONINTERLACED, "1360x768@60" },
	{ V4L2_DV_BT_DMT_1600X900P60,	1600, 900,  60, FB_VMODE_NONINTERLACED, "1600x900@60" },
	{ V4L2_DV_BT_DMT_1600X1200P60,	1600, 1200,  60, FB_VMODE_NONINTERLACED, "1600x1200@60" },
	{ V4L2_DV_BT_DMT_1792X1344P60,	1792, 1344, 60, FB_VMODE_NONINTERLACED, "1792x1344@60" },
	{ V4L2_DV_BT_DMT_1920X800P60,	1920, 800, 60, FB_VMODE_NONINTERLACED, "1920x800@60" },
	{ V4L2_DV_BT_CEA_1920X1080I50,	1920, 1080, 50, FB_VMODE_INTERLACED, "1080i@50" },
	{ V4L2_DV_BT_CEA_1920X1080I60,	1920, 1080, 60, FB_VMODE_INTERLACED, "1080i@60" },
	{ V4L2_DV_BT_CEA_1920X1080P23_976,	1920, 1080, 23, FB_VMODE_NONINTERLACED, "1080p@23.976" },
	{ V4L2_DV_BT_CEA_1920X1080P24,	1920, 1080, 24, FB_VMODE_NONINTERLACED, "1080p@24" },
	{ V4L2_DV_BT_CEA_1920X1080P25,	1920, 1080, 25, FB_VMODE_NONINTERLACED, "1080p@25" },
	{ V4L2_DV_BT_CEA_1920X1080P30,	1920, 1080, 30, FB_VMODE_NONINTERLACED, "1080p@30" },
	{ V4L2_DV_BT_CEA_1920X1080P50,	1920, 1080, 50, FB_VMODE_NONINTERLACED, "1080p@50" },
	{ V4L2_DV_BT_CEA_1920X1080P60,	1920, 1080, 60, FB_VMODE_NONINTERLACED, "1080p@60" },
	{ V4L2_DV_BT_DMT_1920X1200P60_RB,	1920, 1200, 60, FB_VMODE_NONINTERLACED, "1920x1200p@60" },
};

static struct edid_3d_preset {
	struct v4l2_dv_timings dv_timings;
	u16 xres;
	u16 yres;
	u16 refresh;
	u32 vmode;
	u32 s3d;
	char *name;
	bool supported;
} edid_3d_presets[] = {
	{ V4L2_DV_BT_CEA_1280X720P60_SB_HALF,	1280, 720, 60,
		FB_VMODE_NONINTERLACED, EDID_3D_SBS, "720p@60_SBS" },
	{ V4L2_DV_BT_CEA_1280X720P60_TB,	1280, 720, 60,
		FB_VMODE_NONINTERLACED, EDID_3D_TB, "720p@60_TB" },
	{ V4L2_DV_BT_CEA_1280X720P50_SB_HALF,	1280, 720, 50,
		FB_VMODE_NONINTERLACED, EDID_3D_SBS, "720p@50_SBS" },
	{ V4L2_DV_BT_CEA_1280X720P50_TB,	1280, 720, 50,
		FB_VMODE_NONINTERLACED, EDID_3D_TB, "720p@50_TB" },
	{ V4L2_DV_BT_CEA_1920X1080P24_FP,	1920, 1080, 24,
		FB_VMODE_NONINTERLACED, EDID_3D_FP, "1080p@24_FP" },
	{ V4L2_DV_BT_CEA_1920X1080P24_SB_HALF,	1920, 1080, 24,
		FB_VMODE_NONINTERLACED, EDID_3D_SBS, "1080p@24_SBS" },
	{ V4L2_DV_BT_CEA_1920X1080P24_TB,	1920, 1080, 24,
		FB_VMODE_NONINTERLACED, EDID_3D_TB, "1080p@24_TB" },
	{ V4L2_DV_BT_CEA_1920X1080I60_SB_HALF,	1920, 1080, 60,
		FB_VMODE_INTERLACED, EDID_3D_SBS, "1080i@60_SBS" },
	{ V4L2_DV_BT_CEA_1920X1080I50_SB_HALF,	1920, 1080, 50,
		FB_VMODE_INTERLACED, EDID_3D_SBS, "1080i@50_SBS" },
	{ V4L2_DV_BT_CEA_1920X1080P60_SB_HALF,	1920, 1080, 60,
		FB_VMODE_NONINTERLACED, EDID_3D_SBS, "1080p@60_SBS" },
	{ V4L2_DV_BT_CEA_1920X1080P60_TB,	1920, 1080, 60,
		FB_VMODE_NONINTERLACED, EDID_3D_TB, "1080p@60_TB" },
	{ V4L2_DV_BT_CEA_1920X1080P30_SB_HALF,	1920, 1080, 30,
		FB_VMODE_NONINTERLACED, EDID_3D_SBS, "1080p@30_SBS" },
	{ V4L2_DV_BT_CEA_1920X1080P30_TB,	1920, 1080, 30,
		FB_VMODE_NONINTERLACED, EDID_3D_TB, "1080p@30_TB" },
};

static struct v4l2_dv_timings preferred_preset;
static u32 edid_misc;
static int max_audio_channels;
static int audio_bit_rates;
static int audio_sample_rates;
static u32 source_phy_addr;

static int edid_i2c_read(struct hdmi_device *hdev, u8 segment, u8 offset,
						   u8 *buf, size_t len)
{
	struct device *dev = hdev->dev;
	struct s5p_hdmi_platdata *pdata = hdev->pdata;
	struct i2c_client *i2c = edid_client;
	int cnt = 0;
	int ret;
	struct i2c_msg msg[] = {
		{
			.addr = EDID_SEGMENT_ADDR,
			.flags = segment ? 0 : I2C_M_IGNORE_NAK,
			.len = 1,
			.buf = &segment
		},
		{
			.addr = EDID_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &offset
		},
		{
			.addr = EDID_ADDR,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf
		}
	};

	msleep(25);

	if (!i2c)
		return -ENODEV;

	do {
		/*
		 * If the HS-I2C is used for DDC(EDID),
		 * shouldn't write the segment pointer.
		 */
		if (is_ip_ver_5s2) {
			ret = i2c_transfer(i2c->adapter, &msg[1],
						EDID_SEGMENT_IGNORE);
			if (ret == EDID_SEGMENT_IGNORE)
				break;
		} else {
			ret = i2c_transfer(i2c->adapter, msg, ARRAY_SIZE(msg));
			if (ret == ARRAY_SIZE(msg))
				break;
		}

		dev_dbg(dev, "%s: can't read data, retry %d\n", __func__, cnt);
		msleep(25);
		cnt++;
	} while (cnt < 5);

	if (cnt == 5) {
		dev_err(dev, "%s: can't read data, timeout\n", __func__);
		return -ETIME;
	}

	return 0;
}

static int
edid_read_block(struct hdmi_device *hdev, int block, u8 *buf, size_t len)
{
	struct device *dev = hdev->dev;
	int ret, i;
	u8 segment = EDID_SEGMENT(block);
	u8 offset = EDID_OFFSET(block);
	u8 sum = 0;

	if (len < EDID_BLOCK_SIZE)
		return -EINVAL;

	ret = edid_i2c_read(hdev, segment, offset, buf, EDID_BLOCK_SIZE);
	if (ret)
		return ret;

	for (i = 0; i < EDID_BLOCK_SIZE; i++)
		sum += buf[i];

	if (sum) {
		dev_err(dev, "%s: checksum error block=%d sum=%d\n", __func__,
								  block, sum);
		return -EPROTO;
	}

	return 0;
}

static int edid_read(struct hdmi_device *hdev, u8 **data)
{
	u8 block0[EDID_BLOCK_SIZE];
	u8 *edid;
	int block = 0;
	int block_cnt, ret;

	ret = edid_read_block(hdev, 0, block0, sizeof(block0));
	if (ret)
		return ret;

	block_cnt = block0[EDID_EXTENSION_FLAG] + 1;

	edid = kmalloc(block_cnt * EDID_BLOCK_SIZE, GFP_KERNEL);
	if (!edid)
		return -ENOMEM;

	memcpy(edid, block0, sizeof(block0));

	while (++block < block_cnt) {
		ret = edid_read_block(hdev, block,
				edid + block * EDID_BLOCK_SIZE,
					       EDID_BLOCK_SIZE);
		if ((edid[EDID_NATIVE_FORMAT] & EDID_BASIC_AUDIO) >> 6)
			edid_misc = FB_MISC_HDMI;
		if (ret) {
			kfree(edid);
			return ret;
		}
	}

	*data = edid;
	return block_cnt;
}

static struct edid_preset *edid_find_preset(struct fb_videomode *mode)
{
	struct edid_preset *preset = edid_presets;
	int i;

	for (i = 0; i < ARRAY_SIZE(edid_presets); i++, preset++) {
		if (mode->refresh == preset->refresh &&
			mode->xres	== preset->xres &&
			mode->yres	== preset->yres &&
			mode->vmode	== preset->vmode) {
			return preset;
		}
	}

	return NULL;
}

static struct edid_3d_preset *edid_find_3d_mandatory_preset(const struct
				edid_3d_mandatory_preset *mandatory)
{
	struct edid_3d_preset *s3d_preset = edid_3d_presets;
	int i;

	for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++, s3d_preset++) {
		if (mandatory->refresh == s3d_preset->refresh &&
			mandatory->xres	== s3d_preset->xres &&
			mandatory->yres	== s3d_preset->yres &&
			mandatory->s3d	== s3d_preset->s3d) {
			return s3d_preset;
		}
	}

	return NULL;
}

static void edid_find_3d_preset(struct fb_video *vic, struct fb_vendor *vsdb)
{
	struct edid_3d_preset *s3d_preset = edid_3d_presets;
	int i;

	if ((vsdb->s3d_structure_all & EDID_3D_FP_MASK) >> EDID_3D_FP) {
		s3d_preset = edid_3d_presets;
		for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++, s3d_preset++) {
			if (vic->refresh == s3d_preset->refresh &&
				vic->xres	== s3d_preset->xres &&
				vic->yres	== s3d_preset->yres &&
				vic->vmode	== s3d_preset->vmode &&
				EDID_3D_FP	== s3d_preset->s3d) {
				if (s3d_preset->supported == false) {
					s3d_preset->supported = true;
					pr_info("EDID: found %s",
							s3d_preset->name);
				}
			}
		}
	}
	if ((vsdb->s3d_structure_all & EDID_3D_TB_MASK) >> EDID_3D_TB) {
		s3d_preset = edid_3d_presets;
		for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++, s3d_preset++) {
			if (vic->refresh == s3d_preset->refresh &&
				vic->xres	== s3d_preset->xres &&
				vic->yres	== s3d_preset->yres &&
				EDID_3D_TB	== s3d_preset->s3d) {
				if (s3d_preset->supported == false) {
					s3d_preset->supported = true;
					pr_info("EDID: found %s",
							s3d_preset->name);
				}
			}
		}
	}
	if ((vsdb->s3d_structure_all & EDID_3D_SBS_MASK) >> EDID_3D_SBS) {
		s3d_preset = edid_3d_presets;
		for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++, s3d_preset++) {
			if (vic->refresh == s3d_preset->refresh &&
				vic->xres	== s3d_preset->xres &&
				vic->yres	== s3d_preset->yres &&
				EDID_3D_SBS	== s3d_preset->s3d) {
				if (s3d_preset->supported == false) {
					s3d_preset->supported = true;
					pr_info("EDID: found %s",
							s3d_preset->name);
				}
			}
		}
	}
}

static void edid_find_3d_more_preset(struct fb_video *vic, char s3d_structure)
{
	struct edid_3d_preset *s3d_preset = edid_3d_presets;
	int i;

	for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++, s3d_preset++) {
		if (vic->refresh == s3d_preset->refresh &&
			vic->xres	== s3d_preset->xres &&
			vic->yres	== s3d_preset->yres &&
			vic->vmode	== s3d_preset->vmode &&
			s3d_structure	== s3d_preset->s3d) {
			if (s3d_preset->supported == false) {
				s3d_preset->supported = true;
				pr_info("EDID: found %s", s3d_preset->name);
			}
		}
	}
}

#ifdef CONFIG_MACH_ODROIDXU3
static void edid_bootarg_preset(void)
{
	int i;
	printk("###########################################\n");
	printk("# HDMI PHY Resolution %s #\n", HdmiPHYBootArgs);
	printk("###########################################\n");

	if (strncmp(HdmiPHYBootArgs, "1920x1200p60hz", 14) == 0)
		preferred_preset = hdmi_conf[32].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p60hz", 9) == 0)
		preferred_preset = hdmi_conf[31].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p50hz", 9) == 0)
		preferred_preset = hdmi_conf[30].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p30hz", 9) == 0)
		preferred_preset = hdmi_conf[29].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p25hz", 9) == 0)
		preferred_preset = hdmi_conf[28].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p24hz", 9) == 0)
		preferred_preset = hdmi_conf[27].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080p23.976hz", 13) == 0)
		preferred_preset = hdmi_conf[26].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080i60hz", 9) == 0)
		preferred_preset = hdmi_conf[25].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1080i50hz", 9) == 0)
		preferred_preset = hdmi_conf[24].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1920x800p60hz", 13) == 0)
		preferred_preset = hdmi_conf[23].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1792x1344p60hz", 14) == 0)
		preferred_preset = hdmi_conf[22].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1600x1200p60hz", 14) == 0)
		preferred_preset = hdmi_conf[21].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1600x900p60hz", 13) == 0)
		preferred_preset = hdmi_conf[20].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1360x768p60hz", 13) == 0)
		preferred_preset = hdmi_conf[19].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1024p60hz", 9) == 0)
		preferred_preset = hdmi_conf[18].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1400x1050p60hz", 14) == 0)
		preferred_preset = hdmi_conf[17].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "900p60hz", 8) == 0)
		preferred_preset = hdmi_conf[16].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "960p60hz", 8) == 0)
		preferred_preset = hdmi_conf[15].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "800p59hz", 8) == 0)
		preferred_preset = hdmi_conf[14].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1280x768p60hz", 13) == 0)
		preferred_preset = hdmi_conf[13].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "720p60hz", 8) == 0)
		preferred_preset = hdmi_conf[12].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "720p50hz", 8) == 0)
		preferred_preset = hdmi_conf[11].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1152x864p75hz", 13) == 0)
		preferred_preset = hdmi_conf[10].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "768p60hz", 8) == 0)
		preferred_preset = hdmi_conf[9].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "1024x600p60hz", 13) == 0)
		preferred_preset = hdmi_conf[8].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "848x480p60hz", 12) == 0)
		preferred_preset = hdmi_conf[7].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "600p60hz", 8) == 0)
		preferred_preset = hdmi_conf[6].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "800x480p60hz", 12) == 0)
		preferred_preset = hdmi_conf[5].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "480x800p60hz", 12) == 0)
		preferred_preset = hdmi_conf[4].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "576p50hz", 8) == 0)
		preferred_preset = hdmi_conf[3].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "480p59.94hz", 11) == 0)
		preferred_preset = hdmi_conf[2].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "480p60hz", 8) == 0)
		preferred_preset = hdmi_conf[1].dv_timings;
	else if (strncmp(HdmiPHYBootArgs, "480x320p60hz", 12) == 0)
		preferred_preset = hdmi_conf[0].dv_timings;
	else
		preferred_preset = hdmi_conf[12].dv_timings;

	for (i = 0; i < ARRAY_SIZE(edid_presets); i++)
		edid_presets[i].supported =
			v4l_match_dv_timings(&edid_presets[i].dv_timings,
					&preferred_preset, 0);
	max_audio_channels = 2;
}
#endif

static void edid_use_default_preset(void)
{
	int i;

	preferred_preset = hdmi_conf[HDMI_DEFAULT_TIMINGS_IDX].dv_timings;
	for (i = 0; i < ARRAY_SIZE(edid_presets); i++)
		edid_presets[i].supported =
			v4l_match_dv_timings(&edid_presets[i].dv_timings,
					&preferred_preset, 0);
	max_audio_channels = 2;
}

void edid_extension_update(struct fb_monspecs *specs)
{
	struct edid_3d_preset *s3d_preset;
	const struct edid_3d_mandatory_preset *s3d_mandatory
					= edid_3d_mandatory_presets;
	int i;

	if (!specs->vsdb)
		return;

	/* number of 128bytes blocks to follow */
	source_phy_addr = specs->vsdb->phy_addr;

	/* find 3D mandatory preset */
	if (specs->vsdb->s3d_present) {
		for (i = 0; i < ARRAY_SIZE(edid_3d_mandatory_presets);
				i++, s3d_mandatory++) {
			s3d_preset = edid_find_3d_mandatory_preset(s3d_mandatory);
			if (s3d_preset) {
				pr_info("EDID: found %s", s3d_preset->name);
				s3d_preset->supported = true;
			}
		}
	}

	/* find 3D multi preset */
	if (specs->vsdb->s3d_multi_present == EDID_3D_STRUCTURE_ALL)
		for (i = 0; i < specs->videodb_len + 1; i++)
			edid_find_3d_preset(&specs->videodb[i], specs->vsdb);
	else if (specs->vsdb->s3d_multi_present == EDID_3D_STRUCTURE_MASK)
		for (i = 0; i < specs->videodb_len + 1; i++)
			if ((specs->vsdb->s3d_structure_mask & (1 << i)) >> i)
				edid_find_3d_preset(&specs->videodb[i],
						specs->vsdb);

	/* find 3D more preset */
	if (specs->vsdb->s3d_field) {
		for (i = 0; i < specs->videodb_len + 1; i++) {
			edid_find_3d_more_preset(&specs->videodb
					[specs->vsdb->vic_order[i]],
					specs->vsdb->s3d_structure[i]);
			if (specs->vsdb->s3d_structure[i] > EDID_3D_TB + 1)
				i++;
		}
	}
}

int edid_update(struct hdmi_device *hdev)
{
	struct fb_monspecs specs;
	struct edid_preset *preset;
	bool first = true;
	u8 *edid = NULL;
	int channels_max = 0, support_bit_rates = 0, support_sample_rates = 0;
	int block_cnt = 0;
	int ret = 0;
	int i;

	edid_misc = 0;

	block_cnt = edid_read(hdev, &edid);
	if (block_cnt < 0) {
		printk("###########################################\n");
		printk("# edid_read fail, broken??\n");
		printk("###########################################\n");
		goto out;
	}

	fb_edid_to_monspecs(edid, &specs);
	for (i = 1; i < block_cnt; i++) {
		ret = fb_edid_add_monspecs(edid + i * EDID_BLOCK_SIZE, &specs);
		if (ret < 0)
			goto out;
	}

	preferred_preset = hdmi_conf[HDMI_DEFAULT_TIMINGS_IDX].dv_timings;
	for (i = 0; i < ARRAY_SIZE(edid_presets); i++)
		edid_presets[i].supported = false;
	for (i = 0; i < ARRAY_SIZE(edid_3d_presets); i++)
		edid_3d_presets[i].supported = false;

	/* find 2D preset */
	for (i = 0; i < specs.modedb_len; i++) {
		preset = edid_find_preset(&specs.modedb[i]);
		if (preset) {
			if (preset->supported == false) {
				pr_info("EDID: found %s\n", preset->name);
				preset->supported = true;
			}
			if (first) {
				preferred_preset = preset->dv_timings;
				first = false;
			}
		}
	}

	/* number of 128bytes blocks to follow */
	if (block_cnt > 1)
		edid_extension_update(&specs);

	if (!edid_misc)
		edid_misc = specs.misc;
	pr_info("EDID: misc flags %08x", edid_misc);

	for (i = 0; i < specs.audiodb_len; i++) {
		if (specs.audiodb[i].format != FB_AUDIO_LPCM)
			continue;
		if (specs.audiodb[i].channel_count > channels_max) {
			channels_max = specs.audiodb[i].channel_count;
			support_sample_rates = specs.audiodb[i].sample_rates;
			support_bit_rates = specs.audiodb[i].bit_rates;
		}
	}

	if (edid_misc & FB_MISC_HDMI) {
		if (channels_max) {
			max_audio_channels = channels_max;
			audio_sample_rates = support_sample_rates;
			audio_bit_rates = support_bit_rates;
		} else {
			max_audio_channels = 2;
			audio_sample_rates = FB_AUDIO_44KHZ; /*default audio info*/
			audio_bit_rates = FB_AUDIO_16BIT;
		}
	} else {
		max_audio_channels = 0;
		audio_sample_rates = 0;
		audio_bit_rates = 0;
	}
	pr_info("EDID: Audio channels %d\n", max_audio_channels);

	fb_destroy_modedb(specs.modedb);
	fb_destroy_audiodb(specs.audiodb);
	fb_destroy_videodb(specs.videodb);
	fb_destroy_vsdb(specs.vsdb);
out:
	/* No supported preset found, use default */
	if (first)
		edid_use_default_preset();

#ifdef CONFIG_MACH_ODROIDXU3
	if (HdmiEDIDBootArgs == 0)
		edid_bootarg_preset();
#endif

	if (block_cnt == -EPROTO)
		edid_misc = FB_MISC_HDMI;

	kfree(edid);
	return block_cnt;
}

struct v4l2_dv_timings edid_preferred_preset(struct hdmi_device *hdev)
{
	return preferred_preset;
}

bool edid_supports_hdmi(struct hdmi_device *hdev)
{
	return edid_misc & FB_MISC_HDMI;
}

u32 edid_audio_informs(struct hdmi_device *hdev)
{
	u32 value = 0, ch_info = 0;

	if (max_audio_channels > 0)
		ch_info |= (1 << (max_audio_channels - 1));
	if (max_audio_channels > 6)
		ch_info |= (1 << 5);
	value = ((audio_sample_rates << 19) | (audio_bit_rates << 16) |
			ch_info);
	return value;
}

int edid_source_phy_addr(struct hdmi_device *hdev)
{
	return source_phy_addr;
}

static int edid_probe(struct i2c_client *client,
				const struct i2c_device_id *dev_id)
{
	edid_client = client;
	edid_use_default_preset();
	dev_info(&client->adapter->dev, "probed exynos edid\n");
	return 0;
}

static int edid_remove(struct i2c_client *client)
{
	edid_client = NULL;
	dev_info(&client->adapter->dev, "removed exynos edid\n");
	return 0;
}

static struct i2c_device_id edid_idtable[] = {
	{"exynos_edid", 2},
};
MODULE_DEVICE_TABLE(i2c, edid_idtable);

static struct i2c_driver edid_driver = {
	.driver = {
		.name = "exynos_edid",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(edid_device_table),
	},
	.id_table	= edid_idtable,
	.probe		= edid_probe,
	.remove		= edid_remove,
};

static int __init edid_init(void)
{
	return i2c_add_driver(&edid_driver);
}

static void __exit edid_exit(void)
{
	i2c_del_driver(&edid_driver);
}
module_init(edid_init);
module_exit(edid_exit);
