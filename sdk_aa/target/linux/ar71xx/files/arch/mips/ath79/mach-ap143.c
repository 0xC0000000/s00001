/*
 * Atheros AP143 reference board support
 *
 * Copyright (c) 2012 Qualcomm Atheros
 * Copyright (c) 2012 Gabor Juhos <juhosg@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <linux/platform_device.h>
#include <linux/ar8216_platform.h>

#include <asm/mach-ath79/ar71xx_regs.h>

#include "common.h"
#include "dev-ap9x-pci.h"
#include "dev-gpio-buttons.h"
#include "dev-eth.h"
#include "dev-leds-gpio.h"
#include "dev-m25p80.h"
#include "dev-usb.h"
#include "dev-wmac.h"
#include "machtypes.h"

#define DISABLE_PIN11_12

#define AP143_GPIO_USB_PWR       0
#define AP143_GPIO_USB_BT_SELECT 4
#define AP143_GPIO_USB_CONN_SELECT 3

#define AP143_GPIO_LED_WAN		12

#define AP143_GPIO_BTN_JUMPSTART_SW		11

#define AP143_KEYS_POLL_INTERVAL	20	/* msecs */
#define AP143_KEYS_DEBOUNCE_INTERVAL	(3 * AP143_KEYS_POLL_INTERVAL)

#define AP143_MAC0_OFFSET		0
#define AP143_MAC1_OFFSET		6
#define AP143_WMAC_CALDATA_OFFSET	0x1000
#define AP143_PCIE_CALDATA_OFFSET	0x5000

enum {
	DIR_OUT=0,
	DIR_IN
};

#ifndef DISABLE_PIN11_12
static struct gpio_led ap143_leds_gpio[] __initdata = {
	{
		.name		= "ap143:green:wan",
		.gpio		= AP143_GPIO_LED_WAN,
		.active_low	= 1,
	}
};

static struct gpio_keys_button ap143_gpio_keys[] __initdata = {
	{
		.desc		= "restart button",
		.type		= EV_KEY,
		.code		= KEY_RESTART,
		.debounce_interval = AP143_KEYS_DEBOUNCE_INTERVAL,
		.gpio		= AP143_GPIO_BTN_JUMPSTART_SW,
		.active_low	= 1,
	}
};
#endif

static void __init ap143_setup(void)
{
	//u8 *mac = (u8 *) KSEG1ADDR(0x1f01fc00);
	u8 *mac = (u8 *) KSEG1ADDR(0x1ffe0100);
	u8 *ee = (u8 *) KSEG1ADDR(0x1fff1000);
	u8 tmpmac[ETH_ALEN];

	/* Disable JTAG, enabling GPIOs 0-3 */
	ath79_gpio_function_setup(AR934X_GPIO_FUNC_JTAG_DISABLE, 0);

	/* config gpio0(usb power) as output */
	qca953x_gpio_direction_select(AP143_GPIO_USB_PWR,DIR_OUT);
	qca953x_gpio_set_value(AP143_GPIO_USB_PWR,1);

	/* config gpio4(usb bluetooth select) as output  */
	qca953x_gpio_direction_select(AP143_GPIO_USB_BT_SELECT,DIR_OUT);
	qca953x_gpio_set_value(AP143_GPIO_USB_BT_SELECT,1);

#ifndef DISABLE_PIN11_12
	/* config gpio12(led) as output */
	qca953x_gpio_direction_select(AP143_GPIO_LED_WAN,DIR_OUT);
	/* led light */
	qca953x_gpio_set_value(AP143_GPIO_LED_WAN,0);
#endif

	ath79_register_m25p80(NULL);

	ath79_register_usb();

	ath79_setup_ar933x_phy4_switch(false, false);

	ath79_register_mdio(0, 0x0);

	/* WAN */
	ath79_init_mac(ath79_eth1_data.mac_addr, mac, 0);
	ath79_register_eth(0);

	/* LAN */
	ath79_switch_data.phy4_mii_en = 1;
	ath79_eth0_data.phy_if_mode = PHY_INTERFACE_MODE_MII;
	ath79_init_mac(ath79_eth0_data.mac_addr, mac, 1);
	ath79_register_eth(1);

	ath79_init_mac(tmpmac, mac, 2);
	ath79_register_wmac(ee, tmpmac);

#ifndef DISABLE_PIN11_12
	ath79_register_leds_gpio(-1, ARRAY_SIZE(ap143_leds_gpio),
				 ap143_leds_gpio);

	ath79_register_gpio_keys_polled(1, AP143_KEYS_POLL_INTERVAL,
					ARRAY_SIZE(ap143_gpio_keys),
					ap143_gpio_keys);
#endif

}

MIPS_MACHINE(ATH79_MACH_AP143, "AP143", "Atheros AP143 reference board",
	     ap143_setup);

