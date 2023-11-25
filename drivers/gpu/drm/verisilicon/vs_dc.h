/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_H__
#define __VS_DC_H__

#include <linux/version.h>
#include <linux/mm_types.h>

#include <drm/drm_modes.h>
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
#include <drm/drmP.h>
#endif

#include "vs_plane.h"
#include "vs_crtc.h"
#include "vs_dc_hw.h"
#include "vs_dc_dec.h"
#ifdef CONFIG_VERISILICON_MMU
#include "vs_dc_mmu.h"
#endif

struct vs_dc_funcs {
	void (*dump_enable)(struct device *dev, dma_addr_t addr,
				unsigned int pitch);
	void (*dump_disable)(struct device *dev);
};

struct vs_dc_plane {
	enum dc_hw_plane_id id;
};

struct vs_dc {
	struct vs_crtc		*crtc[DC_DISPLAY_NUM];
	struct dc_hw		hw;
#ifdef CONFIG_VERISILICON_DEC
	struct dc_dec400l	dec400l;
#endif

	bool			first_frame;

	struct vs_dc_plane planes[PLANE_NUM];

	const struct vs_dc_funcs *funcs;

	struct clk *cpu_axi;
	struct clk *axicfg0_axi;
	struct clk *disp_axi;
	struct clk *stg_axi;
	struct clk *vout_src;
	struct clk *vout_axi;
	struct clk *ahb1;
	struct clk *vout_ahb;
	struct clk *hdmitx0_mclk;
	struct clk *bclk_mst;
	struct clk *dc8200_clk_pix0;
	struct clk *dc8200_clk_pix1;
	struct clk *dc8200_axi;
	struct clk *dc8200_core;
	struct clk *dc8200_ahb;
	struct clk *vout_top_axi;
	struct clk *vout_top_lcd;
	struct clk *hdmitx0_pixelclk;
	struct clk *dc8200_pix0;
	struct clk *dc8200_clk_pix0_out;
	struct clk *dc8200_clk_pix1_out;
	struct reset_control *vout_resets;
	struct reset_control *dc8200_rst_axi;
	struct reset_control *dc8200_rst_core;
	struct reset_control *dc8200_rst_ahb;
	struct reset_control *rst_vout_src;
	struct reset_control *noc_disp;

	struct regmap *dss_regmap;

	int	init_count;
};

extern struct platform_driver dc_platform_driver;


#endif /* __VS_DC_H__ */
