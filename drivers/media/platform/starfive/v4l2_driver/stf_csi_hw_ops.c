// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include "stfcamss.h"
#include <linux/regmap.h>

#define CSI2RX_DEVICE_CFG_REG			0x000

#define CSI2RX_SOFT_RESET_REG			0x004
#define CSI2RX_SOFT_RESET_PROTOCOL		BIT(1)
#define CSI2RX_SOFT_RESET_FRONT			BIT(0)

#define CSI2RX_DPHY_LANE_CONTROL                0x040

#define CSI2RX_STATIC_CFG_REG			0x008
#define CSI2RX_STATIC_CFG_DLANE_MAP(llane, plane)	\
		((plane) << (16 + (llane) * 4))
#define CSI2RX_STATIC_CFG_LANES_MASK		GENMASK(11, 8)

#define CSI2RX_STREAM_BASE(n)		(((n) + 1) * 0x100)

#define CSI2RX_STREAM_CTRL_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x000)
#define CSI2RX_STREAM_CTRL_START		BIT(0)

#define CSI2RX_STREAM_DATA_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x008)
#define CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT	BIT(31)
#define CSI2RX_STREAM_DATA_CFG_EN_DATA_TYPE_0 BIT(7)
#define CSI2RX_STREAM_DATA_CFG_VC_SELECT(n)	BIT((n) + 16)

#define CSI2RX_STREAM_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x00c)
#define CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF	(1 << 8)

#define CSI2RX_LANES_MAX	4
#define CSI2RX_STREAMS_MAX	4

static int stf_csi_power_on(struct stf_csi_dev *csi_dev, u8 on)
{
	struct stfcamss *stfcamss = csi_dev->stfcamss;
	int ret;

	if (on) {
		ret = regulator_enable(csi_dev->mipirx_0p9);
		if (ret) {
			st_err(ST_CSI, "Cannot enable mipirx_0p9 regulator\n");
			return ret;
		}
	} else
		regulator_disable(csi_dev->mipirx_0p9);

	regmap_update_bits(stfcamss->stf_aon_syscon, stfcamss->aon_gp_reg,
				BIT(31), BIT(31));

	return 0;
}

static int stf_csi_clk_enable(struct stf_csi_dev *csi_dev)
{
	struct stfcamss *stfcamss = csi_dev->stfcamss;

	clk_set_rate(stfcamss->sys_clk[STFCLK_MIPI_RX0_PXL].clk, 198000000);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF0].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF1].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF2].clk);
	clk_prepare_enable(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF3].clk);

	reset_control_deassert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF0].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF1].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF2].rstc);
	reset_control_deassert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF3].rstc);

	switch (csi_dev->s_type) {
	case SENSOR_VIN:
		reset_control_deassert(stfcamss->sys_rst[STFRST_AXIWR].rstc);
		clk_set_parent(stfcamss->sys_clk[STFCLK_AXIWR].clk,
			stfcamss->sys_clk[STFCLK_MIPI_RX0_PXL].clk);
		break;
	case SENSOR_ISP:
		clk_set_parent(stfcamss->sys_clk[STFCLK_WRAPPER_CLK_C].clk,
			stfcamss->sys_clk[STFCLK_MIPI_RX0_PXL].clk);
		break;
	}

	return 0;
}

static int stf_csi_clk_disable(struct stf_csi_dev *csi_dev)
{
	struct stfcamss *stfcamss = csi_dev->stfcamss;

	switch (csi_dev->s_type) {
	case SENSOR_VIN:
		reset_control_assert(stfcamss->sys_rst[STFRST_AXIWR].rstc);
		break;
	case SENSOR_ISP:
		break;
	}

	reset_control_assert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF3].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF2].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF1].rstc);
	reset_control_assert(stfcamss->sys_rst[STFRST_PIXEL_CLK_IF0].rstc);

	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF3].clk);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF2].clk);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF1].clk);
	clk_disable_unprepare(stfcamss->sys_clk[STFCLK_PIXEL_CLK_IF0].clk);

	return 0;
}

static void csi2rx_reset(void *reg_base)
{
	writel(CSI2RX_SOFT_RESET_PROTOCOL | CSI2RX_SOFT_RESET_FRONT,
	       reg_base + CSI2RX_SOFT_RESET_REG);

	udelay(10);

	writel(0, reg_base + CSI2RX_SOFT_RESET_REG);
}

static int csi2rx_start(struct stf_csi_dev *csi_dev, void *reg_base, u32 dt)
{
	struct stfcamss *stfcamss = csi_dev->stfcamss;
	struct csi2phy_cfg *csiphy =
		stfcamss->csiphy_dev->csiphy;
	unsigned int i;
	unsigned long lanes_used = 0;
	u32 reg;

	if (!csiphy) {
		st_err(ST_CSI, "csiphy0 config not exist\n");
		return -EINVAL;
	}

	csi2rx_reset(reg_base);

	reg = csiphy->num_data_lanes << 8;
	for (i = 0; i < csiphy->num_data_lanes; i++) {
#ifndef USE_CSIDPHY_ONE_CLK_MODE
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, csiphy->data_lanes[i]);
		set_bit(csiphy->data_lanes[i] - 1, &lanes_used);
#else
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, i + 1);
		set_bit(i, &lanes_used);
#endif
	}

	/*
	 * Even the unused lanes need to be mapped. In order to avoid
	 * to map twice to the same physical lane, keep the lanes used
	 * in the previous loop, and only map unused physical lanes to
	 * the rest of our logical lanes.
	 */
	for (i = csiphy->num_data_lanes; i < CSI2RX_LANES_MAX; i++) {
		unsigned int idx = find_first_zero_bit(&lanes_used,
						       CSI2RX_LANES_MAX);

		set_bit(idx, &lanes_used);
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, idx + 1);
	}

	writel(reg, reg_base + CSI2RX_STATIC_CFG_REG);

	// 0x40 DPHY_LANE_CONTROL
	reg = 0;
#ifndef USE_CSIDPHY_ONE_CLK_MODE
	for (i = 0; i < csiphy->num_data_lanes; i++)
		reg |= 1 << (csiphy->data_lanes[i] - 1)
			| 1 << (csiphy->data_lanes[i] + 11);
#else
	for (i = 0; i < csiphy->num_data_lanes; i++)
		reg |= 1 << i | 1 << (i + 12);		//data_clane
#endif

	reg |= 1 << 4 | 1 << 16;		//clk_lane
	writel(reg, reg_base + CSI2RX_DPHY_LANE_CONTROL);

	/*
	 * Create a static mapping between the CSI virtual channels
	 * and the output stream.
	 *
	 * This should be enhanced, but v4l2 lacks the support for
	 * changing that mapping dynamically.
	 *
	 * We also cannot enable and disable independent streams here,
	 * hence the reference counting.
	 */
	for (i = 0; i < CSI2RX_STREAMS_MAX; i++) {
		writel(CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF,
		       reg_base + CSI2RX_STREAM_CFG_REG(i));

		writel(CSI2RX_STREAM_DATA_CFG_EN_VC_SELECT |
		       CSI2RX_STREAM_DATA_CFG_VC_SELECT(i) |
		       CSI2RX_STREAM_DATA_CFG_EN_DATA_TYPE_0 | dt,
		       reg_base + CSI2RX_STREAM_DATA_CFG_REG(i));

		writel(CSI2RX_STREAM_CTRL_START,
		       reg_base + CSI2RX_STREAM_CTRL_REG(i));
	}

	return 0;
}

static void csi2rx_stop(struct stf_csi_dev *csi_dev, void *reg_base)
{
	unsigned int i;

	for (i = 0; i < CSI2RX_STREAMS_MAX; i++)
		writel(0, reg_base + CSI2RX_STREAM_CTRL_REG(i));
}

static void csi_set_vin_axiwr_pix(struct stf_csi_dev *csi_dev, u32 width, u8 bpp)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;
	u32 value = 0;
	int cnfg_axiwr_pix_ct = 64 / bpp;

	if (cnfg_axiwr_pix_ct == 2)
		value = 0;
	else if (cnfg_axiwr_pix_ct == 4)
		value = 1;
	else if (cnfg_axiwr_pix_ct == 8)
		value = 2;

	reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28,
		BIT(14)|BIT(13), value << 13);	//u0_vin_cnfg_axiwr0_pix_ct
	reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28,
		BIT(12)|BIT(11)|BIT(10)|BIT(9)|BIT(8)|BIT(7)|BIT(6)|BIT(5)|BIT(4)|BIT(3)|BIT(2),
		(width / cnfg_axiwr_pix_ct - 1)<<2);	//u0_vin_cnfg_axiwr0_pix_cnt_end
}

static int stf_csi_stream_set(struct stf_csi_dev *csi_dev,
			      int on, u32 dt, u32 width, u8 bpp)
{
	struct stf_vin_dev *vin = csi_dev->stfcamss->vin;
	void __iomem *reg_base = vin->csi2rx_base;

	switch (csi_dev->s_type) {
	case SENSOR_VIN:
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_20,
			BIT(3)|BIT(2)|BIT(1)|BIT(0),
			0<<0);		//u0_vin_cnfg_axiwr0_channel_sel
		reg_set_bit(vin->sysctrl_base, SYSCONSAIF_SYSCFG_28,
			BIT(16)|BIT(15),
			0<<15);		//u0_vin_cnfg_axiwr0_pixel_high_bit_sel
		csi_set_vin_axiwr_pix(csi_dev, width, bpp);
		break;
	case SENSOR_ISP:
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
			BIT(7)|BIT(6),
			0<<6);		//u0_vin_cnfg_mipi_byte_en_isp
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
			BIT(11)|BIT(10)|BIT(9)|BIT(8),
			0<<8);		//u0_vin_cnfg_mipi_channel_sel0
		reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
			BIT(16)|BIT(15)|BIT(14)|BIT(13),
			0<<13);		//u0_vin_cnfg_pix_num

		if (dt == 0x2b)
			reg_set_bit(vin->sysctrl_base,	SYSCONSAIF_SYSCFG_36,
				BIT(12),
				1<<12);		//u0_vin_cnfg_p_i_mipi_header_en0
		break;
	}

	if (on)
		csi2rx_start(csi_dev, reg_base, dt);
	else
		csi2rx_stop(csi_dev, reg_base);

	return 0;
}

void dump_csi_reg(void *__iomem csibase)
{
	st_info(ST_CSI, "DUMP CSI register:\n");
	print_reg(ST_CSI, csibase, 0x00);
	print_reg(ST_CSI, csibase, 0x04);
	print_reg(ST_CSI, csibase, 0x08);
	print_reg(ST_CSI, csibase, 0x10);

	print_reg(ST_CSI, csibase, 0x40);
	print_reg(ST_CSI, csibase, 0x48);
	print_reg(ST_CSI, csibase, 0x4c);
	print_reg(ST_CSI, csibase, 0x50);
}

struct csi_hw_ops csi_ops = {
	.csi_power_on          = stf_csi_power_on,
	.csi_clk_enable        = stf_csi_clk_enable,
	.csi_clk_disable       = stf_csi_clk_disable,
	.csi_stream_set        = stf_csi_stream_set,
};
