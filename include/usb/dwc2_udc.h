/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * drivers/usb/gadget/dwc2_udc.h
 * Designware DWC2 on-chip full/high speed USB device controllers
 * Copyright (C) 2005 for Samsung Electronics
 */

#ifndef __DWC2_USB_GADGET
#define __DWC2_USB_GADGET

#define PHY0_SLEEP              (1 << 5)

#define DWC2_MAX_HW_ENDPOINTS	16
#define DWC2_SIZE_NB_OFFS	0
#define DWC2_SIZE_OFFS		1

struct dwc2_plat_otg_data {
	void		*priv;
	int		phy_of_node;
	int		(*phy_control)(int on);
	unsigned int	regs_phy;
	uintptr_t	regs_otg;
	unsigned int    usb_phy_ctrl;
	unsigned int    usb_flags;
	unsigned int	usb_gusbcfg;
	unsigned int	usb_gotgctl;
	unsigned int	rx_fifo_sz;
	unsigned int	np_tx_fifo_sz;
	unsigned int	tx_fifo_sz;
	/* [0] number of element, [1..17] tx_fifo_sz (max 16 endpoints)*/
	unsigned int	tx_fifo_sz_array[DWC2_MAX_HW_ENDPOINTS + 1];
};

int dwc2_udc_probe(struct dwc2_plat_otg_data *pdata);

#endif	/* __DWC2_USB_GADGET */
