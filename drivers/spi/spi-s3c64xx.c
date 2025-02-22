/*
 * Copyright (C) 2009 Samsung Electronics Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/platform_data/spi-s3c64xx.h>

static unsigned int force32b = 0;
module_param(force32b, uint, 0);
MODULE_PARM_DESC(force32b, "force 32bits fb data");

#include <mach/exynos-fimc-is.h>

#if defined(CONFIG_S3C_DMA) || defined(CONFIG_SAMSUNG_DMADEV)
#include <mach/dma.h>
#endif

#define MAX_SPI_PORTS		5
#define SPI_AUTOSUSPEND_TIMEOUT		(2000)

/* Registers and bit-fields */

#define S3C64XX_SPI_CH_CFG		0x00
#define S3C64XX_SPI_CLK_CFG		0x04
#define S3C64XX_SPI_MODE_CFG	0x08
#define S3C64XX_SPI_SLAVE_SEL	0x0C
#define S3C64XX_SPI_INT_EN		0x10
#define S3C64XX_SPI_STATUS		0x14
#define S3C64XX_SPI_TX_DATA		0x18
#define S3C64XX_SPI_RX_DATA		0x1C
#define S3C64XX_SPI_PACKET_CNT	0x20
#define S3C64XX_SPI_PENDING_CLR	0x24
#define S3C64XX_SPI_SWAP_CFG	0x28
#define S3C64XX_SPI_FB_CLK		0x2C

#define S3C64XX_SPI_CH_HS_EN		(1<<6)	/* High Speed Enable */
#define S3C64XX_SPI_CH_SW_RST		(1<<5)
#define S3C64XX_SPI_CH_SLAVE		(1<<4)
#define S3C64XX_SPI_CPOL_L		(1<<3)
#define S3C64XX_SPI_CPHA_B		(1<<2)
#define S3C64XX_SPI_CH_RXCH_ON		(1<<1)
#define S3C64XX_SPI_CH_TXCH_ON		(1<<0)

#define S3C64XX_SPI_CLKSEL_SRCMSK	(3<<9)
#define S3C64XX_SPI_CLKSEL_SRCSHFT	9
#define S3C64XX_SPI_ENCLK_ENABLE	(1<<8)
#define S3C64XX_SPI_PSR_MASK		0xff

#define S3C64XX_SPI_MODE_CH_TSZ_BYTE		(0<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_HALFWORD	(1<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_WORD		(2<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_MASK		(3<<29)
#define S3C64XX_SPI_MODE_BUS_TSZ_BYTE		(0<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_HALFWORD	(1<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_WORD		(2<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_MASK		(3<<17)
#define S3C64XX_SPI_MODE_RXDMA_ON		(1<<2)
#define S3C64XX_SPI_MODE_TXDMA_ON		(1<<1)
#define S3C64XX_SPI_MODE_4BURST			(1<<0)

#define S3C64XX_SPI_SLAVE_NSC_CNT_2		(2<<4)
#define S3C64XX_SPI_SLAVE_NSC_CNT_1		(1<<4)
#define S3C64XX_SPI_SLAVE_AUTO			(1<<1)
#define S3C64XX_SPI_SLAVE_SIG_INACT		(1<<0)

#define S3C64XX_SPI_INT_TRAILING_EN		(1<<6)
#define S3C64XX_SPI_INT_RX_OVERRUN_EN		(1<<5)
#define S3C64XX_SPI_INT_RX_UNDERRUN_EN		(1<<4)
#define S3C64XX_SPI_INT_TX_OVERRUN_EN		(1<<3)
#define S3C64XX_SPI_INT_TX_UNDERRUN_EN		(1<<2)
#define S3C64XX_SPI_INT_RX_FIFORDY_EN		(1<<1)
#define S3C64XX_SPI_INT_TX_FIFORDY_EN		(1<<0)

#define S3C64XX_SPI_ST_RX_OVERRUN_ERR		(1<<5)
#define S3C64XX_SPI_ST_RX_UNDERRUN_ERR	(1<<4)
#define S3C64XX_SPI_ST_TX_OVERRUN_ERR		(1<<3)
#define S3C64XX_SPI_ST_TX_UNDERRUN_ERR	(1<<2)
#define S3C64XX_SPI_ST_RX_FIFORDY		(1<<1)
#define S3C64XX_SPI_ST_TX_FIFORDY		(1<<0)

#define S3C64XX_SPI_PACKET_CNT_EN		(1<<16)

#define S3C64XX_SPI_PND_TX_UNDERRUN_CLR		(1<<4)
#define S3C64XX_SPI_PND_TX_OVERRUN_CLR		(1<<3)
#define S3C64XX_SPI_PND_RX_UNDERRUN_CLR		(1<<2)
#define S3C64XX_SPI_PND_RX_OVERRUN_CLR		(1<<1)
#define S3C64XX_SPI_PND_TRAILING_CLR		(1<<0)

#define S3C64XX_SPI_SWAP_RX_HALF_WORD		(1<<7)
#define S3C64XX_SPI_SWAP_RX_BYTE		(1<<6)
#define S3C64XX_SPI_SWAP_RX_BIT			(1<<5)
#define S3C64XX_SPI_SWAP_RX_EN			(1<<4)
#define S3C64XX_SPI_SWAP_TX_HALF_WORD		(1<<3)
#define S3C64XX_SPI_SWAP_TX_BYTE		(1<<2)
#define S3C64XX_SPI_SWAP_TX_BIT			(1<<1)
#define S3C64XX_SPI_SWAP_TX_EN			(1<<0)

#define S3C64XX_SPI_FBCLK_MSK		(3<<0)

#define FIFO_LVL_MASK(i) ((i)->port_conf->fifo_lvl_mask[i->port_id])
#define S3C64XX_SPI_ST_TX_DONE(v, i) (((v) & \
				(1 << (i)->port_conf->tx_st_done)) ? 1 : 0)
#define TX_FIFO_LVL(v, i) (((v) >> 6) & FIFO_LVL_MASK(i))
#define RX_FIFO_LVL(v, i) (((v) >> (i)->port_conf->rx_lvl_offset) & \
					FIFO_LVL_MASK(i))

#define S3C64XX_SPI_MAX_TRAILCNT	0x3ff
#define S3C64XX_SPI_TRAILCNT_OFF	19

#define S3C64XX_SPI_TRAILCNT		S3C64XX_SPI_MAX_TRAILCNT

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

#define RXBUSY    (1<<2)
#define TXBUSY    (1<<3)

/**
 * struct s3c64xx_spi_info - SPI Controller hardware info
 * @fifo_lvl_mask: Bit-mask for {TX|RX}_FIFO_LVL bits in SPI_STATUS register.
 * @rx_lvl_offset: Bit offset of RX_FIFO_LVL bits in SPI_STATUS regiter.
 * @tx_st_done: Bit offset of TX_DONE bit in SPI_STATUS regiter.
 * @high_speed: True, if the controller supports HIGH_SPEED_EN bit.
 * @clk_from_cmu: True, if the controller does not include a clock mux and
 *	prescaler unit.
 *
 * The Samsung s3c64xx SPI controller are used on various Samsung SoC's but
 * differ in some aspects such as the size of the fifo and spi bus clock
 * setup. Such differences are specified to the driver using this structure
 * which is provided as driver data to the driver.
 */
struct s3c64xx_spi_port_config {
	int	fifo_lvl_mask[MAX_SPI_PORTS];
	int	rx_lvl_offset;
	int	tx_st_done;
	bool	high_speed;
	bool	clk_from_cmu;
};

static void flush_fifo(struct s3c64xx_spi_driver_data *sdd)
{
	void __iomem *regs = sdd->regs;
	unsigned long loops;
	u32 val;

	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~(S3C64XX_SPI_CH_RXCH_ON | S3C64XX_SPI_CH_TXCH_ON);
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val |= S3C64XX_SPI_CH_SW_RST;
	val &= ~S3C64XX_SPI_CH_HS_EN;
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	/* Flush TxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
	} while (TX_FIFO_LVL(val, sdd) && loops--);

	if (loops == 0)
		dev_warn(&sdd->pdev->dev, "Timed out flushing TX FIFO\n");

	/* Flush RxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
		if (RX_FIFO_LVL(val, sdd))
			readl(regs + S3C64XX_SPI_RX_DATA);
		else
			break;
	} while (loops--);

	if (loops == 0)
		dev_warn(&sdd->pdev->dev, "Timed out flushing RX FIFO\n");

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~S3C64XX_SPI_CH_SW_RST;
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~(S3C64XX_SPI_MODE_TXDMA_ON | S3C64XX_SPI_MODE_RXDMA_ON);
	writel(val, regs + S3C64XX_SPI_MODE_CFG);
}

static void s3c64xx_spi_dmacb(void *data)
{
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_dma_data *dma = data;
	unsigned long flags;

	if (dma->direction == DMA_DEV_TO_MEM)
		sdd = container_of(data,
			struct s3c64xx_spi_driver_data, rx_dma);
	else
		sdd = container_of(data,
			struct s3c64xx_spi_driver_data, tx_dma);

	spin_lock_irqsave(&sdd->lock, flags);

	if (dma->direction == DMA_DEV_TO_MEM) {
		sdd->state &= ~RXBUSY;
		if (!(sdd->state & TXBUSY))
			complete(&sdd->xfer_completion);
	} else {
		sdd->state &= ~TXBUSY;
		if (!(sdd->state & RXBUSY))
			complete(&sdd->xfer_completion);
	}

	spin_unlock_irqrestore(&sdd->lock, flags);
}

#if defined(CONFIG_S3C_DMA) || defined(CONFIG_SAMSUNG_DMADEV)
/* FIXME: remove this section once arch/arm/mach-s3c64xx uses dmaengine */

static struct s3c2410_dma_client s3c64xx_spi_dma_client = {
	.name = "samsung-spi-dma",
};

static void prepare_dma(struct s3c64xx_spi_dma_data *dma,
					unsigned len, dma_addr_t buf)
{
	struct s3c64xx_spi_driver_data *sdd;
	struct samsung_dma_prep info;
	struct samsung_dma_config config;

	if (dma->direction == DMA_DEV_TO_MEM) {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, rx_dma);
		config.direction = sdd->rx_dma.direction;
		config.fifo = sdd->sfr_start + S3C64XX_SPI_RX_DATA;
		config.width = sdd->cur_bpw / 8;
		sdd->ops->config((enum dma_ch)sdd->rx_dma.ch, &config);
	} else {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, tx_dma);
		config.direction =  sdd->tx_dma.direction;
		config.fifo = sdd->sfr_start + S3C64XX_SPI_TX_DATA;
		config.width = sdd->cur_bpw / 8;
		sdd->ops->config((enum dma_ch)sdd->tx_dma.ch, &config);
	}

	info.cap = DMA_SLAVE;
	info.len = len;
	info.fp = s3c64xx_spi_dmacb;
	info.fp_param = dma;
	info.direction = dma->direction;
	info.buf = buf;

	sdd->ops->prepare((enum dma_ch)dma->ch, &info);
	sdd->ops->trigger((enum dma_ch)dma->ch);
}

static int acquire_dma(struct s3c64xx_spi_driver_data *sdd)
{
	struct samsung_dma_req req;
	struct device *dev = &sdd->pdev->dev;

	sdd->ops = samsung_dma_get_ops();

	req.cap = DMA_SLAVE;
	req.client = &s3c64xx_spi_dma_client;

	sdd->rx_dma.ch = (void *)sdd->ops->request(sdd->rx_dma.dmach,
							&req, dev, "rx");
	sdd->tx_dma.ch = (void *)sdd->ops->request(sdd->tx_dma.dmach,
							&req, dev, "tx");

	return 1;
}

static int s3c64xx_spi_prepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	if (sci->dma_mode == DMA_MODE) {
		/* Acquire DMA channels */
		while (!acquire_dma(sdd))
			usleep_range(10000, 11000);
	}

	pm_runtime_get_sync(&sdd->pdev->dev);

	return 0;
}

static int s3c64xx_spi_unprepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	/* Free DMA channels */
	if (sci->dma_mode == DMA_MODE) {
		sdd->ops->release((enum dma_ch)sdd->rx_dma.ch,
						&s3c64xx_spi_dma_client);
		sdd->ops->release((enum dma_ch)sdd->tx_dma.ch,
						&s3c64xx_spi_dma_client);
	}

	pm_runtime_mark_last_busy(&sdd->pdev->dev);
	pm_runtime_put_autosuspend(&sdd->pdev->dev);

	return 0;
}

static void s3c64xx_spi_dma_stop(struct s3c64xx_spi_driver_data *sdd,
				 struct s3c64xx_spi_dma_data *dma)
{
	sdd->ops->stop((enum dma_ch)dma->ch);
}
#else

static void prepare_dma(struct s3c64xx_spi_dma_data *dma,
					unsigned len, dma_addr_t buf)
{
	struct s3c64xx_spi_driver_data *sdd;
	struct dma_slave_config config;
	struct scatterlist sg;
	struct dma_async_tx_descriptor *desc;

	if (dma->direction == DMA_DEV_TO_MEM) {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, rx_dma);
		config.direction = dma->direction;
		config.src_addr = sdd->sfr_start + S3C64XX_SPI_RX_DATA;
		config.src_addr_width = sdd->cur_bpw / 8;
		config.src_maxburst = 1;
		dmaengine_slave_config(dma->ch, &config);
	} else {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, tx_dma);
		config.direction = dma->direction;
		config.dst_addr = sdd->sfr_start + S3C64XX_SPI_TX_DATA;
		config.dst_addr_width = sdd->cur_bpw / 8;
		config.dst_maxburst = 1;
		dmaengine_slave_config(dma->ch, &config);
	}

	sg_init_table(&sg, 1);
	sg_dma_len(&sg) = len;
	sg_set_page(&sg, pfn_to_page(PFN_DOWN(buf)),
		    len, offset_in_page(buf));
	sg_dma_address(&sg) = buf;

	desc = dmaengine_prep_slave_sg(dma->ch,
		&sg, 1, dma->direction, DMA_PREP_INTERRUPT);

	desc->callback = s3c64xx_spi_dmacb;
	desc->callback_param = dma;

	dmaengine_submit(desc);
	dma_async_issue_pending(dma->ch);
}

static int s3c64xx_spi_prepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	dma_filter_fn filter = sdd->cntrlr_info->filter;
	struct device *dev = &sdd->pdev->dev;
	dma_cap_mask_t mask;
	int ret;

	if (sci->dma_mode != DMA_MODE) {
		ret = pm_runtime_get_sync(&sdd->pdev->dev);
		if (ret < 0) {
			dev_err(dev, "Failed to enable device: %d\n", ret);
			return ret;
		}
		return 0;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Acquire DMA channels */
	sdd->rx_dma.ch = dma_request_slave_channel_compat(mask, filter,
				(void *)sdd->rx_dma.dmach, dev, "rx");
	if (!sdd->rx_dma.ch) {
		dev_err(dev, "Failed to get RX DMA channel\n");
		ret = -EBUSY;
		goto out;
	}

	sdd->tx_dma.ch = dma_request_slave_channel_compat(mask, filter,
				(void *)sdd->tx_dma.dmach, dev, "tx");
	if (!sdd->tx_dma.ch) {
		dev_err(dev, "Failed to get TX DMA channel\n");
		ret = -EBUSY;
		goto out_rx;
	}

	ret = pm_runtime_get_sync(&sdd->pdev->dev);
	if (ret < 0) {
		dev_err(dev, "Failed to enable device: %d\n", ret);
		goto out_tx;
	}

	return 0;

out_tx:
	dma_release_channel(sdd->tx_dma.ch);
out_rx:
	dma_release_channel(sdd->rx_dma.ch);
out:
	return ret;
}

static int s3c64xx_spi_unprepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	if (sci->dma_mode == DMA_MODE) {
		/* Free DMA channels */
		dma_release_channel(sdd->rx_dma.ch);
		dma_release_channel(sdd->tx_dma.ch);
	}

	pm_runtime_mark_last_busy(&sdd->pdev->dev);
	pm_runtime_put_autosuspend(&sdd->pdev->dev);
	return 0;
}

static void s3c64xx_spi_dma_stop(struct s3c64xx_spi_driver_data *sdd,
				 struct s3c64xx_spi_dma_data *dma)
{
	dmaengine_terminate_all(dma->ch);
}
#endif

static void enable_datapath(struct s3c64xx_spi_driver_data *sdd,
				struct spi_device *spi,
				struct spi_transfer *xfer, int dma_mode)
{
	void __iomem *regs = sdd->regs;
	u32 modecfg, chcfg;

	modecfg = readl(regs + S3C64XX_SPI_MODE_CFG);
	modecfg &= ~(S3C64XX_SPI_MODE_TXDMA_ON | S3C64XX_SPI_MODE_RXDMA_ON);

	chcfg = readl(regs + S3C64XX_SPI_CH_CFG);
	chcfg &= ~S3C64XX_SPI_CH_TXCH_ON;

	if (dma_mode) {
		chcfg &= ~S3C64XX_SPI_CH_RXCH_ON;
	} else {
		/* Always shift in data in FIFO, even if xfer is Tx only,
		 * this helps setting PCKT_CNT value for generating clocks
		 * as exactly needed.
		 */
		chcfg |= S3C64XX_SPI_CH_RXCH_ON;
		writel(((xfer->len * 8 / sdd->cur_bpw) & 0xffff)
					| S3C64XX_SPI_PACKET_CNT_EN,
					regs + S3C64XX_SPI_PACKET_CNT);
	}

	if (xfer->tx_buf != NULL) {
		sdd->state |= TXBUSY;
		chcfg |= S3C64XX_SPI_CH_TXCH_ON;
		if (dma_mode) {
			modecfg |= S3C64XX_SPI_MODE_TXDMA_ON;
			prepare_dma(&sdd->tx_dma, xfer->len, xfer->tx_dma);
		} else {
			switch (sdd->cur_bpw) {
			case 32:
				iowrite32_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len / 4);
				break;
			case 16:
				iowrite16_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len / 2);
				break;
			default:
				iowrite8_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len);
				break;
			}
		}
	}

	if (xfer->rx_buf != NULL) {
		sdd->state |= RXBUSY;

		if (sdd->port_conf->high_speed && sdd->cur_speed >= 30000000UL
					&& !(sdd->cur_mode & SPI_CPHA))
			chcfg |= S3C64XX_SPI_CH_HS_EN;

		if (dma_mode) {
			modecfg |= S3C64XX_SPI_MODE_RXDMA_ON;
			chcfg |= S3C64XX_SPI_CH_RXCH_ON;
			writel(((xfer->len * 8 / sdd->cur_bpw) & 0xffff)
					| S3C64XX_SPI_PACKET_CNT_EN,
					regs + S3C64XX_SPI_PACKET_CNT);
			prepare_dma(&sdd->rx_dma, xfer->len, xfer->rx_dma);
		}
	}

	writel(modecfg, regs + S3C64XX_SPI_MODE_CFG);
	writel(chcfg, regs + S3C64XX_SPI_CH_CFG);
}

static inline void enable_cs(struct s3c64xx_spi_driver_data *sdd,
						struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs;

	if (sdd->tgl_spi != NULL) { /* If last device toggled after mssg */
		if (sdd->tgl_spi != spi) { /* if last mssg on diff device */
			/* Deselect the last toggled device */
			cs = sdd->tgl_spi->controller_data;
			if(cs->line != (unsigned)NULL)
				gpio_set_value(cs->line,
					spi->mode & SPI_CS_HIGH ? 0 : 1);
			/* Quiese the signals */
			writel(spi->mode & SPI_CS_HIGH ?
				0 : S3C64XX_SPI_SLAVE_SIG_INACT,
				sdd->regs + S3C64XX_SPI_SLAVE_SEL);
		}
		sdd->tgl_spi = NULL;
	}

	cs = spi->controller_data;
	if(cs->line != (unsigned)NULL)
		gpio_set_value(cs->line, spi->mode & SPI_CS_HIGH ? 1 : 0);

	if (cs->cs_mode == AUTO_CS_MODE) {
		/* Set auto chip selection */
		writel(readl(sdd->regs + S3C64XX_SPI_SLAVE_SEL)
			| S3C64XX_SPI_SLAVE_AUTO
			| S3C64XX_SPI_SLAVE_NSC_CNT_2,
		sdd->regs + S3C64XX_SPI_SLAVE_SEL);
	} else {
		/* Start the signals */
		writel(spi->mode & SPI_CS_HIGH ?
				S3C64XX_SPI_SLAVE_SIG_INACT : 0,
			       sdd->regs + S3C64XX_SPI_SLAVE_SEL);
	}
}

static int wait_for_xfer(struct s3c64xx_spi_driver_data *sdd,
				struct spi_transfer *xfer, int dma_mode)
{
	void __iomem *regs = sdd->regs;
	unsigned long val;
	int ms;

	/* millisecs to xfer 'len' bytes @ 'cur_speed' */
	ms = xfer->len * 8 * 2000 / sdd->cur_speed;
	ms += 10; /* some tolerance */

	if (dma_mode) {
		val = msecs_to_jiffies(ms) + 10;
		val = wait_for_completion_timeout(&sdd->xfer_completion, val);
	} else {
		u32 status;
		val = msecs_to_loops(ms);
		do {
			status = readl(regs + S3C64XX_SPI_STATUS);
		} while (RX_FIFO_LVL(status, sdd) < xfer->len && --val);
	}

	if (!val)
		return -EIO;

	if (dma_mode) {
		u32 status;

		/*
		 * DmaTx returns after simply writing data in the FIFO,
		 * w/o waiting for real transmission on the bus to finish.
		 * DmaRx returns only after Dma read data from FIFO which
		 * needs bus transmission to finish, so we don't worry if
		 * Xfer involved Rx(with or without Tx).
		 */
		if (xfer->rx_buf == NULL) {
			val = msecs_to_loops(10);
			status = readl(regs + S3C64XX_SPI_STATUS);
			while ((TX_FIFO_LVL(status, sdd)
				|| !S3C64XX_SPI_ST_TX_DONE(status, sdd))
					&& --val) {
				cpu_relax();
				status = readl(regs + S3C64XX_SPI_STATUS);
			}

			if (!val)
				return -EIO;
		}
	} else {
		/* If it was only Tx */
		if (xfer->rx_buf == NULL) {
			sdd->state &= ~TXBUSY;
			return 0;
		}

		switch (sdd->cur_bpw) {
		case 32:
			ioread32_rep(regs + S3C64XX_SPI_RX_DATA,
				xfer->rx_buf, xfer->len / 4);
			break;
		case 16:
			ioread16_rep(regs + S3C64XX_SPI_RX_DATA,
				xfer->rx_buf, xfer->len / 2);
			break;
		default:
			ioread8_rep(regs + S3C64XX_SPI_RX_DATA,
				xfer->rx_buf, xfer->len);
			break;
		}
		sdd->state &= ~RXBUSY;
	}

	return 0;
}

static inline void disable_cs(struct s3c64xx_spi_driver_data *sdd,
						struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;

	if (sdd->tgl_spi == spi)
		sdd->tgl_spi = NULL;

	if(cs->line != (unsigned)NULL)
		gpio_set_value(cs->line, spi->mode & SPI_CS_HIGH ? 0 : 1);
	/* Quiese the signals */
	writel(spi->mode & SPI_CS_HIGH ? 0 : S3C64XX_SPI_SLAVE_SIG_INACT,
	       sdd->regs + S3C64XX_SPI_SLAVE_SEL);
}

static void s3c64xx_spi_config(struct s3c64xx_spi_driver_data *sdd)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	u32 val;

	/* Disable Clock */
	if (sdd->port_conf->clk_from_cmu) {
		clk_disable_unprepare(sdd->src_clk);
	} else {
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val &= ~S3C64XX_SPI_ENCLK_ENABLE;
		writel(val, regs + S3C64XX_SPI_CLK_CFG);
	}

	/* Set Polarity and Phase */
	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~(S3C64XX_SPI_CH_SLAVE |
			S3C64XX_SPI_CPOL_L |
			S3C64XX_SPI_CPHA_B);

	if (sdd->cur_mode & SPI_CPOL)
		val |= S3C64XX_SPI_CPOL_L;

	if (sdd->cur_mode & SPI_CPHA)
		val |= S3C64XX_SPI_CPHA_B;

	writel(val, regs + S3C64XX_SPI_CH_CFG);

	/* Set Channel & DMA Mode */
	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~(S3C64XX_SPI_MODE_BUS_TSZ_MASK
			| S3C64XX_SPI_MODE_CH_TSZ_MASK);

	switch (sdd->cur_bpw) {
	case 32:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_WORD;
		val |= S3C64XX_SPI_MODE_CH_TSZ_WORD;
		if (sci->swap_mode == SWAP_MODE) {
			writel(S3C64XX_SPI_SWAP_TX_EN |
				S3C64XX_SPI_SWAP_TX_BYTE |
				S3C64XX_SPI_SWAP_TX_HALF_WORD |
				S3C64XX_SPI_SWAP_RX_EN |
				S3C64XX_SPI_SWAP_RX_BYTE |
				S3C64XX_SPI_SWAP_RX_HALF_WORD,
				regs + S3C64XX_SPI_SWAP_CFG);
		}
		break;
	case 16:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_HALFWORD;
		val |= S3C64XX_SPI_MODE_CH_TSZ_HALFWORD;
		if (sci->swap_mode == SWAP_MODE) {
			writel(S3C64XX_SPI_SWAP_TX_EN |
				S3C64XX_SPI_SWAP_TX_BYTE |
				S3C64XX_SPI_SWAP_RX_EN |
				S3C64XX_SPI_SWAP_RX_BYTE,
				regs + S3C64XX_SPI_SWAP_CFG);
		}
		break;
	default:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_BYTE;
		val |= S3C64XX_SPI_MODE_CH_TSZ_BYTE;
		if (sci->swap_mode == SWAP_MODE)
			writel(0, regs + S3C64XX_SPI_SWAP_CFG);
		break;
	}

	writel(val, regs + S3C64XX_SPI_MODE_CFG);

	if (sdd->port_conf->clk_from_cmu) {
		/* Configure Clock */
		/* There is half-multiplier before the SPI */
		clk_set_rate(sdd->src_clk, sdd->cur_speed * 2);
		/* Enable Clock */
		clk_prepare_enable(sdd->src_clk);
	} else {
		/* Configure Clock */
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val &= ~S3C64XX_SPI_PSR_MASK;
		val |= ((clk_get_rate(sdd->src_clk) / sdd->cur_speed / 2 - 1)
				& S3C64XX_SPI_PSR_MASK);
		writel(val, regs + S3C64XX_SPI_CLK_CFG);

		/* Enable Clock */
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val |= S3C64XX_SPI_ENCLK_ENABLE;
		writel(val, regs + S3C64XX_SPI_CLK_CFG);
	}
}

#define XFER_DMAADDR_INVALID DMA_BIT_MASK(32)

static int s3c64xx_spi_map_mssg(struct s3c64xx_spi_driver_data *sdd,
						struct spi_message *msg)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct device *dev = &sdd->pdev->dev;
	struct spi_transfer *xfer;

	if ((msg->is_dma_mapped) || (sci->dma_mode != DMA_MODE))
		return 0;

	/* First mark all xfer unmapped */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		xfer->rx_dma = XFER_DMAADDR_INVALID;
		xfer->tx_dma = XFER_DMAADDR_INVALID;
	}

	/* Map until end or first fail */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		if (xfer->len <= ((FIFO_LVL_MASK(sdd) >> 1) + 1))
			continue;

		if (xfer->tx_buf != NULL) {
			xfer->tx_dma = dma_map_single(dev,
					(void *)xfer->tx_buf, xfer->len,
					DMA_TO_DEVICE);
			if (dma_mapping_error(dev, xfer->tx_dma)) {
				dev_err(dev, "dma_map_single Tx failed\n");
				xfer->tx_dma = XFER_DMAADDR_INVALID;
				return -ENOMEM;
			}
		}

		if (xfer->rx_buf != NULL) {
			xfer->rx_dma = dma_map_single(dev, xfer->rx_buf,
						xfer->len, DMA_FROM_DEVICE);
			if (dma_mapping_error(dev, xfer->rx_dma)) {
				dev_err(dev, "dma_map_single Rx failed\n");
				dma_unmap_single(dev, xfer->tx_dma,
						xfer->len, DMA_TO_DEVICE);
				xfer->tx_dma = XFER_DMAADDR_INVALID;
				xfer->rx_dma = XFER_DMAADDR_INVALID;
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static void s3c64xx_spi_unmap_mssg(struct s3c64xx_spi_driver_data *sdd,
						struct spi_message *msg)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct device *dev = &sdd->pdev->dev;
	struct spi_transfer *xfer;

	if ((msg->is_dma_mapped) || (sci->dma_mode != DMA_MODE))
		return;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		if (xfer->len <= ((FIFO_LVL_MASK(sdd) >> 1) + 1))
			continue;

		if (xfer->rx_buf != NULL
				&& xfer->rx_dma != XFER_DMAADDR_INVALID)
			dma_unmap_single(dev, xfer->rx_dma,
						xfer->len, DMA_FROM_DEVICE);

		if (xfer->tx_buf != NULL
				&& xfer->tx_dma != XFER_DMAADDR_INVALID)
			dma_unmap_single(dev, xfer->tx_dma,
						xfer->len, DMA_TO_DEVICE);
	}
}

static int s3c64xx_spi_transfer_one_message(struct spi_master *master,
					    struct spi_message *msg)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct spi_device *spi = msg->spi;
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;
	struct spi_transfer *xfer;
	int status = 0, cs_toggle = 0;
	const void *origin_tx_buf = NULL;
	void *origin_rx_buf = NULL;
	unsigned target_len = 0, origin_len = 0;
	unsigned fifo_lvl = (FIFO_LVL_MASK(sdd) >> 1) + 1;
	u32 speed;
	u8 bpw;

	/* If Master's(controller) state differs from that needed by Slave */
	if (sdd->cur_speed != spi->max_speed_hz
			|| sdd->cur_mode != spi->mode
			|| sdd->cur_bpw != spi->bits_per_word) {
		sdd->cur_bpw = spi->bits_per_word;
		sdd->cur_speed = spi->max_speed_hz;
		sdd->cur_mode = spi->mode;
		s3c64xx_spi_config(sdd);
	}

	/* Map all the transfers if needed */
	if (s3c64xx_spi_map_mssg(sdd, msg)) {
		dev_err(&spi->dev,
			"Xfer: Unable to map message buffers!\n");
		status = -ENOMEM;
		goto out;
	}

	/* Configure feedback delay */
	writel(cs->fb_delay & 0x3, sdd->regs + S3C64XX_SPI_FB_CLK);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		unsigned long flags;
		int use_dma;

		INIT_COMPLETION(sdd->xfer_completion);

        /* Only BPW and Speed may change across transfers */
		bpw = xfer->bits_per_word;
		speed = xfer->speed_hz ? : spi->max_speed_hz;

        if (force32b && (xfer->len >= 64)) {
            if ((bpw == 8) && ((xfer->len % 4) == 0)) {
                bpw = 32;
            }
        }

		if (xfer->len % (bpw / 8)) {
			dev_err(&spi->dev,
				"Xfer length(%u) not a multiple of word size(%u)\n",
				xfer->len, bpw / 8);
			status = -EIO;
			goto out;
		}

		if (bpw != sdd->cur_bpw || speed != sdd->cur_speed) {
			sdd->cur_bpw = bpw;
			sdd->cur_speed = speed;
			s3c64xx_spi_config(sdd);
		}

		/* verify cpu mode */
		if (sci->dma_mode != DMA_MODE) {
			use_dma = 0;

			/* backup original tx, rx buf ptr & xfer length */
			origin_tx_buf = xfer->tx_buf;
			origin_rx_buf = xfer->rx_buf;
			origin_len = xfer->len;

			target_len = xfer->len;
			if (xfer->len > fifo_lvl)
				xfer->len = fifo_lvl;
		} else {
		/* Polling method for xfers not bigger than FIFO capacity */
			if (xfer->len <= fifo_lvl)
				use_dma = 0;
			else
				use_dma = 1;
		}
try_transfer:
		spin_lock_irqsave(&sdd->lock, flags);

		/* Pending only which is to be done */
		sdd->state &= ~RXBUSY;
		sdd->state &= ~TXBUSY;

		if (cs->cs_mode == AUTO_CS_MODE) {
			/* Slave Select */
			enable_cs(sdd, spi);

			enable_datapath(sdd, spi, xfer, use_dma);
		} else {
			enable_datapath(sdd, spi, xfer, use_dma);

			/* Slave Select */
			enable_cs(sdd, spi);
		}

		spin_unlock_irqrestore(&sdd->lock, flags);

		status = wait_for_xfer(sdd, xfer, use_dma);


		if (status) {
			dev_err(&spi->dev, "I/O Error: rx-%d tx-%d res:rx-%c tx-%c len-%d\n",
				xfer->rx_buf ? 1 : 0, xfer->tx_buf ? 1 : 0,
				(sdd->state & RXBUSY) ? 'f' : 'p',
				(sdd->state & TXBUSY) ? 'f' : 'p',
				xfer->len);

			if (use_dma) {
				if (xfer->tx_buf != NULL
						&& (sdd->state & TXBUSY))
					s3c64xx_spi_dma_stop(sdd, &sdd->tx_dma);
				if (xfer->rx_buf != NULL
						&& (sdd->state & RXBUSY))
					s3c64xx_spi_dma_stop(sdd, &sdd->rx_dma);
			}

			goto out;
		}

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change) {
			/* Hint that the next mssg is gonna be
			   for the same device */
			if (list_is_last(&xfer->transfer_list,
						&msg->transfers))
				cs_toggle = 1;
		}

		msg->actual_length += xfer->len;

		flush_fifo(sdd);

		if (sci->dma_mode != DMA_MODE) {
			target_len -= xfer->len;

			if (xfer->tx_buf != NULL)
				xfer->tx_buf += xfer->len;

			if (xfer->rx_buf != NULL)
				xfer->rx_buf += xfer->len;

			if (target_len > 0) {
				if (target_len > fifo_lvl)
					xfer->len = fifo_lvl;
				else
					xfer->len = target_len;
				goto try_transfer;
			}

			/* restore original tx, rx buf_ptr & xfer length */
			xfer->tx_buf = origin_tx_buf;
			xfer->rx_buf = origin_rx_buf;
			xfer->len = origin_len;
		}
	}

out:
	if (!cs_toggle || status)
		disable_cs(sdd, spi);
	else
		sdd->tgl_spi = spi;

	s3c64xx_spi_unmap_mssg(sdd, msg);

	msg->status = status;

	spi_finalize_current_message(master);

	return 0;
}

static struct s3c64xx_spi_csinfo *s3c64xx_get_slave_ctrldata(
				struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs;
	struct device_node *slave_np, *data_np = NULL;
	u32 fb_delay = 0;
	u32 cs_mode = 0;

	slave_np = spi->dev.of_node;
	if (!slave_np) {
		dev_err(&spi->dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	data_np = of_get_child_by_name(slave_np, "controller-data");
	if (!data_np) {
		dev_err(&spi->dev, "child node 'controller-data' not found\n");
		return ERR_PTR(-EINVAL);
	}

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs) {
		dev_err(&spi->dev, "could not allocate memory for controller data\n");
		of_node_put(data_np);
		return ERR_PTR(-ENOMEM);
	}

	if (of_get_property(data_np, "cs-gpio", NULL)) {
		cs->line = of_get_named_gpio(data_np, "cs-gpio", 0);
		if (!gpio_is_valid(cs->line))
			cs->line = (unsigned)NULL;
	} else {
		cs->line = (unsigned)NULL;
	}

	of_property_read_u32(data_np, "samsung,spi-feedback-delay", &fb_delay);
	cs->fb_delay = fb_delay;

	of_property_read_u32(data_np, "samsung,spi-chip-select-mode", &cs_mode);
	cs->cs_mode = cs_mode;

	of_node_put(data_np);
	return cs;
}

/*
 * Here we only check the validity of requested configuration
 * and save the configuration in a local data-structure.
 * The controller is actually configured only just before we
 * get a message to transfer.
 */
static int s3c64xx_spi_setup(struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_info *sci;
	struct spi_message *msg;
	unsigned long flags;
	int err;

	sdd = spi_master_get_devdata(spi->master);
	if (!cs && spi->dev.of_node) {
		cs = s3c64xx_get_slave_ctrldata(spi);
		spi->controller_data = cs;
	}

	if (IS_ERR_OR_NULL(cs)) {
		dev_err(&spi->dev, "No CS for SPI(%d)\n", spi->chip_select);
		return -ENODEV;
	}

	if (!spi_get_ctldata(spi)) {
		if(cs->line != (unsigned)NULL) {
			err = gpio_request_one(cs->line, GPIOF_OUT_INIT_HIGH,
					       dev_name(&spi->dev));
			if (err) {
				dev_err(&spi->dev,
					"Failed to get /CS gpio [%d]: %d\n",
					cs->line, err);
				goto err_gpio_req;
			}
		}
		spi_set_ctldata(spi, cs);
	}

	sci = sdd->cntrlr_info;

	spin_lock_irqsave(&sdd->lock, flags);

	list_for_each_entry(msg, &sdd->queue, queue) {
		/* Is some mssg is already queued for this device */
		if (msg->spi == spi) {
			dev_err(&spi->dev,
				"setup: attempt while mssg in queue!\n");
			spin_unlock_irqrestore(&sdd->lock, flags);
			err = -EBUSY;
			goto err_msgq;
		}
	}

	spin_unlock_irqrestore(&sdd->lock, flags);

	if (spi->bits_per_word != 8
			&& spi->bits_per_word != 16
			&& spi->bits_per_word != 32) {
		dev_err(&spi->dev, "setup: %dbits/wrd not supported!\n",
							spi->bits_per_word);
		err = -EINVAL;
		goto setup_exit;
	}

	pm_runtime_get_sync(&sdd->pdev->dev);

	/* Check if we can provide the requested rate */
	if (!sdd->port_conf->clk_from_cmu) {
		u32 psr, speed;

		/* Max possible */
		speed = clk_get_rate(sdd->src_clk) / 2 / (0 + 1);
		if (!speed) {
			dev_err(&spi->dev, "clock rate of speed is 0\n");
			err = -EINVAL;
			goto setup_exit;
		}

		if (spi->max_speed_hz > speed)
			spi->max_speed_hz = speed;

		psr = clk_get_rate(sdd->src_clk) / 2 / spi->max_speed_hz - 1;
		psr &= S3C64XX_SPI_PSR_MASK;
		if (psr == S3C64XX_SPI_PSR_MASK)
			psr--;

		speed = clk_get_rate(sdd->src_clk) / 2 / (psr + 1);
		if (spi->max_speed_hz < speed) {
			if (psr+1 < S3C64XX_SPI_PSR_MASK) {
				psr++;
			} else {
				err = -EINVAL;
				goto setup_exit;
			}
		}

		speed = clk_get_rate(sdd->src_clk) / 2 / (psr + 1);
		if (spi->max_speed_hz >= speed) {
			spi->max_speed_hz = speed;
		} else {
			dev_err(&spi->dev, "Can't set %dHz transfer speed\n",
				spi->max_speed_hz);
			err = -EINVAL;
			goto setup_exit;
		}
	}

	pm_runtime_mark_last_busy(&sdd->pdev->dev);
	pm_runtime_put_autosuspend(&sdd->pdev->dev);

	disable_cs(sdd, spi);
	return 0;

setup_exit:
	/* setup() returns with device de-selected */
	disable_cs(sdd, spi);

err_msgq:
	gpio_free(cs->line);
	spi_set_ctldata(spi, NULL);

err_gpio_req:
	if (spi->dev.of_node)
		kfree(cs);

	return err;
}

static void s3c64xx_spi_cleanup(struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi_get_ctldata(spi);

	if (cs) {
		gpio_free(cs->line);
		if (spi->dev.of_node)
			kfree(cs);
	}
	spi_set_ctldata(spi, NULL);
}

static irqreturn_t s3c64xx_spi_irq(int irq, void *data)
{
	struct s3c64xx_spi_driver_data *sdd = data;
	struct spi_master *spi = sdd->master;
	unsigned int val, clr = 0;

	val = readl(sdd->regs + S3C64XX_SPI_STATUS);

	if (val & S3C64XX_SPI_ST_RX_OVERRUN_ERR) {
		clr = S3C64XX_SPI_PND_RX_OVERRUN_CLR;
		dev_err(&spi->dev, "RX overrun\n");
	}
	if (val & S3C64XX_SPI_ST_RX_UNDERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_RX_UNDERRUN_CLR;
		dev_err(&spi->dev, "RX underrun\n");
	}
	if (val & S3C64XX_SPI_ST_TX_OVERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_TX_OVERRUN_CLR;
		dev_err(&spi->dev, "TX overrun\n");
	}
	if (val & S3C64XX_SPI_ST_TX_UNDERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_TX_UNDERRUN_CLR;
		dev_err(&spi->dev, "TX underrun\n");
	}

	/* Clear the pending irq by setting and then clearing it */
	writel(clr, sdd->regs + S3C64XX_SPI_PENDING_CLR);
	writel(0, sdd->regs + S3C64XX_SPI_PENDING_CLR);

	return IRQ_HANDLED;
}

static void s3c64xx_spi_hwinit(struct s3c64xx_spi_driver_data *sdd, int channel)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	unsigned int val;

	sdd->cur_speed = 0;

	writel(S3C64XX_SPI_SLAVE_SIG_INACT, sdd->regs + S3C64XX_SPI_SLAVE_SEL);

	/* Disable Interrupts - we use Polling if not DMA mode */
	writel(0, regs + S3C64XX_SPI_INT_EN);

	if (!sdd->port_conf->clk_from_cmu)
		writel(sci->src_clk_nr << S3C64XX_SPI_CLKSEL_SRCSHFT,
				regs + S3C64XX_SPI_CLK_CFG);
	writel(0, regs + S3C64XX_SPI_MODE_CFG);
	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	/* Clear any irq pending bits, should set and clear the bits */
	val = S3C64XX_SPI_PND_RX_OVERRUN_CLR |
		S3C64XX_SPI_PND_RX_UNDERRUN_CLR |
		S3C64XX_SPI_PND_TX_OVERRUN_CLR |
		S3C64XX_SPI_PND_TX_UNDERRUN_CLR;
	writel(val, regs + S3C64XX_SPI_PENDING_CLR);
	writel(0, regs + S3C64XX_SPI_PENDING_CLR);

	writel(0, regs + S3C64XX_SPI_SWAP_CFG);

	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~S3C64XX_SPI_MODE_4BURST;
	val &= ~(S3C64XX_SPI_MAX_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	val |= (S3C64XX_SPI_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	writel(val, regs + S3C64XX_SPI_MODE_CFG);

	flush_fifo(sdd);
}

#ifdef CONFIG_OF
static struct s3c64xx_spi_info *s3c64xx_spi_parse_dt(struct device *dev)
{
	struct s3c64xx_spi_info *sci;
	u32 temp;
	const char *domain;

	sci = devm_kzalloc(dev, sizeof(*sci), GFP_KERNEL);
	if (!sci) {
		dev_err(dev, "memory allocation for spi_info failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_get_property(dev->of_node, "dma-mode", NULL))
		sci->dma_mode = DMA_MODE;
	else
		sci->dma_mode = CPU_MODE;

	if (of_get_property(dev->of_node, "swap-mode", NULL))
		sci->swap_mode = SWAP_MODE;
	else
		sci->swap_mode = NO_SWAP_MODE;

	if (of_property_read_u32(dev->of_node, "samsung,spi-src-clk", &temp)) {
		dev_warn(dev, "spi bus clock parent not specified, using clock at index 0 as parent\n");
		sci->src_clk_nr = 0;
	} else {
		sci->src_clk_nr = temp;
	}

	if (of_property_read_u32(dev->of_node, "num-cs", &temp)) {
		dev_warn(dev, "number of chip select lines not specified, assuming 1 chip select line\n");
		sci->num_cs = 1;
	} else {
		sci->num_cs = temp;
	}

	sci->domain = DOMAIN_TOP;
	if (!of_property_read_string(dev->of_node, "domain", &domain)) {
		if (strncmp(domain, "isp", 3) == 0)
			sci->domain = DOMAIN_ISP;
		else if (strncmp(domain, "cam1", 4) == 0)
			sci->domain = DOMAIN_CAM1;
	}

	return sci;
}
#else
static struct s3c64xx_spi_info *s3c64xx_spi_parse_dt(struct device *dev)
{
	return dev->platform_data;
}
#endif

static const struct of_device_id s3c64xx_spi_dt_match[];

static inline struct s3c64xx_spi_port_config *s3c64xx_spi_get_port_config(
						struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(s3c64xx_spi_dt_match, pdev->dev.of_node);
		return (struct s3c64xx_spi_port_config *)match->data;
	}
#endif
	return (struct s3c64xx_spi_port_config *)
			 platform_get_device_id(pdev)->driver_data;
}

static int s3c64xx_spi_probe(struct platform_device *pdev)
{
	struct resource	*mem_res;
	struct resource	*res;
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_info *sci = pdev->dev.platform_data;
	struct spi_master *master;
	int ret, irq;
	char clk_name[16];

	if (!sci && pdev->dev.of_node) {
		sci = s3c64xx_spi_parse_dt(&pdev->dev);
		if (IS_ERR(sci))
			return PTR_ERR(sci);
	}

#ifndef CONFIG_VIDEO_EXYNOS_FIMC_IS
	if (sci->domain != DOMAIN_TOP)
		return -ENODEV;
#endif
	if (!sci) {
		dev_err(&pdev->dev, "platform_data missing!\n");
		return -ENODEV;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI MEM resource\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_warn(&pdev->dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	master = spi_alloc_master(&pdev->dev,
				sizeof(struct s3c64xx_spi_driver_data));
	if (master == NULL) {
		dev_err(&pdev->dev, "Unable to allocate SPI Master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);

	sdd = spi_master_get_devdata(master);
	sdd->port_conf = s3c64xx_spi_get_port_config(pdev);
	sdd->master = master;
	sdd->cntrlr_info = sci;
	sdd->pdev = pdev;
	sdd->sfr_start = mem_res->start;
	if (pdev->dev.of_node) {
		ret = of_alias_get_id(pdev->dev.of_node, "spi");
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to get alias id, errno %d\n",
				ret);
			goto err0;
		}
		pdev->id = sdd->port_id = ret;
	} else {
		sdd->port_id = pdev->id;
	}

	sdd->cur_bpw = 8;

	if (sci->dma_mode == DMA_MODE) {
		if (!sdd->pdev->dev.of_node) {
			res = platform_get_resource(pdev, IORESOURCE_DMA,  0);
			if (!res) {
				dev_err(&pdev->dev,
					"Unable to get SPI tx dma resource\n");
				return -ENXIO;
			}
			sdd->tx_dma.dmach = res->start;

			res = platform_get_resource(pdev, IORESOURCE_DMA,  1);
			if (!res) {
				dev_err(&pdev->dev,
					"Unable to get SPI rx dma resource\n");
				return -ENXIO;
			}
			sdd->rx_dma.dmach = res->start;
		}

		sdd->tx_dma.direction = DMA_MEM_TO_DEV;
		sdd->rx_dma.direction = DMA_DEV_TO_MEM;
	}

	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = sdd->port_id;
	master->setup = s3c64xx_spi_setup;
	master->cleanup = s3c64xx_spi_cleanup;
	master->prepare_transfer_hardware = s3c64xx_spi_prepare_transfer;
	master->transfer_one_message = s3c64xx_spi_transfer_one_message;
	master->unprepare_transfer_hardware = s3c64xx_spi_unprepare_transfer;
	master->num_chipselect = sci->num_cs;
	master->dma_alignment = 8;
	master->bits_per_word_mask = BIT(32 - 1) | BIT(16 - 1) | BIT(8 - 1);
	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	sdd->regs = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(sdd->regs)) {
		ret = PTR_ERR(sdd->regs);
		goto err0;
	}

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, SPI_AUTOSUSPEND_TIMEOUT);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (sci->cfg_gpio && sci->cfg_gpio()) {
		dev_err(&pdev->dev, "Unable to config gpio\n");
		ret = -EBUSY;
		goto err0;
	}

	/* Setup clocks */
	sdd->clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(sdd->clk)) {
		dev_err(&pdev->dev, "Unable to acquire clock 'spi'\n");
		ret = PTR_ERR(sdd->clk);
		goto err0;
	}

	if (clk_prepare_enable(sdd->clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock 'spi'\n");
		ret = -EBUSY;
		goto err0;
	}

	snprintf(clk_name, sizeof(clk_name), "spi_busclk%d", sci->src_clk_nr);
	sdd->src_clk = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(sdd->src_clk)) {
		dev_err(&pdev->dev,
			"Unable to acquire clock '%s'\n", clk_name);
		ret = PTR_ERR(sdd->src_clk);
		goto err2;
	}

	if (clk_prepare_enable(sdd->src_clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock '%s'\n", clk_name);
		ret = -EBUSY;
		goto err2;
	}


	/* Setup Deufult Mode */
	s3c64xx_spi_hwinit(sdd, sdd->port_id);

	spin_lock_init(&sdd->lock);
	init_completion(&sdd->xfer_completion);
	INIT_LIST_HEAD(&sdd->queue);

	ret = devm_request_irq(&pdev->dev, irq, s3c64xx_spi_irq, 0,
				"spi-s3c64xx", sdd);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n",
			irq, ret);
		goto err3;
	}

	writel(S3C64XX_SPI_INT_RX_OVERRUN_EN | S3C64XX_SPI_INT_RX_UNDERRUN_EN |
	       S3C64XX_SPI_INT_TX_OVERRUN_EN | S3C64XX_SPI_INT_TX_UNDERRUN_EN,
	       sdd->regs + S3C64XX_SPI_INT_EN);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	if (spi_register_master(master)) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		ret = -EBUSY;
		goto err3;
	}

	dev_dbg(&pdev->dev, "Samsung SoC SPI Driver loaded for Bus SPI-%d with %d Slaves attached\n",
					sdd->port_id, master->num_chipselect);
	dev_dbg(&pdev->dev, "\tIOmem=[0x%x-0x%x]\tDMA=[Rx-%d, Tx-%d]\n",
					mem_res->end, mem_res->start,
					sdd->rx_dma.dmach, sdd->tx_dma.dmach);

    pr_err("%s : force32b = %d\n", __func__, force32b);

	return 0;

err3:
	pm_runtime_disable(&pdev->dev);
	clk_disable_unprepare(sdd->src_clk);
err2:
	clk_disable_unprepare(sdd->clk);
err0:
	platform_set_drvdata(pdev, NULL);
	spi_master_put(master);

	return ret;
}

static int s3c64xx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	pm_runtime_disable(&pdev->dev);

	spi_unregister_master(master);

	writel(0, sdd->regs + S3C64XX_SPI_INT_EN);

	clk_disable_unprepare(sdd->src_clk);

	clk_disable_unprepare(sdd->clk);

	platform_set_drvdata(pdev, NULL);
	spi_master_put(master);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int s3c64xx_spi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
#ifndef CONFIG_PM_RUNTIME
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
#endif

	spi_master_suspend(master);

#ifndef CONFIG_PM_RUNTIME
	if (sci->domain == DOMAIN_TOP) {
		/* Disable the clock */
		clk_disable_unprepare(sdd->src_clk);
		clk_disable_unprepare(sdd->clk);
	}
#endif
	sdd->cur_speed = 0; /* Output Clock is stopped */

	return 0;
}

static int s3c64xx_spi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	if (sci->domain == DOMAIN_TOP) {
		/* Enable the clock */
		clk_prepare_enable(sdd->src_clk);
		clk_prepare_enable(sdd->clk);

		if (sci->cfg_gpio)
			sci->cfg_gpio();

		s3c64xx_spi_hwinit(sdd, sdd->port_id);

#ifdef CONFIG_PM_RUNTIME
		/* Disable the clock */
		clk_disable_unprepare(sdd->src_clk);
		clk_disable_unprepare(sdd->clk);
#endif
	}

	spi_master_resume(master);

	return 0;
}
#else
static int s3c64xx_spi_suspend(struct device *dev)
{
	return 0;
}

static int s3c64xx_spi_resume(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_RUNTIME
static int s3c64xx_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	clk_disable_unprepare(sdd->clk);
	clk_disable_unprepare(sdd->src_clk);

	return 0;
}

static int s3c64xx_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	if (sci->domain == DOMAIN_TOP) {
		clk_prepare_enable(sdd->src_clk);
		clk_prepare_enable(sdd->clk);
	}
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	else if (sci->domain == DOMAIN_CAM1 || sci->domain == DOMAIN_ISP) {
		struct platform_device *fimc_is_pdev;

		if (fimc_is_dev == NULL)
			return -EPERM;

		fimc_is_pdev = to_platform_device(fimc_is_dev);
		exynos_fimc_is_cfg_cam_clk(fimc_is_pdev);

		clk_prepare_enable(sdd->src_clk);
		clk_prepare_enable(sdd->clk);

		s3c64xx_spi_hwinit(sdd, sdd->port_id);
	}
#endif

	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

static const struct dev_pm_ops s3c64xx_spi_pm = {
	.suspend_noirq = s3c64xx_spi_suspend,
	.resume_noirq = s3c64xx_spi_resume,
	SET_RUNTIME_PM_OPS(s3c64xx_spi_runtime_suspend,
			   s3c64xx_spi_runtime_resume, NULL)
};

static struct s3c64xx_spi_port_config s3c2443_spi_port_config = {
	.fifo_lvl_mask	= { 0x7f },
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
	.high_speed	= true,
};

static struct s3c64xx_spi_port_config s3c6410_spi_port_config = {
	.fifo_lvl_mask	= { 0x7f, 0x7F },
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
};

static struct s3c64xx_spi_port_config s5p64x0_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
};

static struct s3c64xx_spi_port_config s5pc100_spi_port_config = {
	.fifo_lvl_mask	= { 0x7f, 0x7F },
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
	.high_speed	= true,
};

static struct s3c64xx_spi_port_config s5pv210_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
};

static struct s3c64xx_spi_port_config exynos4_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F, 0x7F },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
	.clk_from_cmu	= true,
};

static struct s3c64xx_spi_port_config exynos5_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F, 0x7F, 0x1ff, 0x1ff },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
	.clk_from_cmu	= true,
};

static struct platform_device_id s3c64xx_spi_driver_ids[] = {
	{
		.name		= "s3c2443-spi",
		.driver_data	= (kernel_ulong_t)&s3c2443_spi_port_config,
	}, {
		.name		= "s3c6410-spi",
		.driver_data	= (kernel_ulong_t)&s3c6410_spi_port_config,
	}, {
		.name		= "s5p64x0-spi",
		.driver_data	= (kernel_ulong_t)&s5p64x0_spi_port_config,
	}, {
		.name		= "s5pc100-spi",
		.driver_data	= (kernel_ulong_t)&s5pc100_spi_port_config,
	}, {
		.name		= "s5pv210-spi",
		.driver_data	= (kernel_ulong_t)&s5pv210_spi_port_config,
	}, {
		.name		= "exynos4210-spi",
		.driver_data	= (kernel_ulong_t)&exynos4_spi_port_config,
	}, {
		.name		= "exynos5410-spi",
		.driver_data	= (kernel_ulong_t)&exynos5_spi_port_config,
	},
	{ },
};

#ifdef CONFIG_OF
static const struct of_device_id s3c64xx_spi_dt_match[] = {
	{ .compatible = "samsung,exynos4210-spi",
			.data = (void *)&exynos4_spi_port_config,
	},
	{ .compatible = "samsung,exynos5410-spi",
			.data = (void *)&exynos5_spi_port_config,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, s3c64xx_spi_dt_match);
#endif /* CONFIG_OF */

static struct platform_driver s3c64xx_spi_driver = {
	.driver = {
		.name	= "s3c64xx-spi",
		.owner = THIS_MODULE,
		.pm = &s3c64xx_spi_pm,
		.of_match_table = of_match_ptr(s3c64xx_spi_dt_match),
	},
	.remove = s3c64xx_spi_remove,
	.id_table = s3c64xx_spi_driver_ids,
};
MODULE_ALIAS("platform:s3c64xx-spi");

static int __init s3c64xx_spi_init(void)
{
	return platform_driver_probe(&s3c64xx_spi_driver, s3c64xx_spi_probe);
}
subsys_initcall(s3c64xx_spi_init);

static void __exit s3c64xx_spi_exit(void)
{
	platform_driver_unregister(&s3c64xx_spi_driver);
}
module_exit(s3c64xx_spi_exit);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("S3C64XX SPI Controller Driver");
MODULE_LICENSE("GPL");
