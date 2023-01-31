/*
 * AW9523B I2C I/O Expander
 *
 * Author: howrd <l.tao@onetolink.com>
 *
 */

#ifndef _AW9523B_H
#define _AW9523B_H

#ifdef CONFIG_LEDS_AW9523B
#include <linux/leds.h>
#endif


#define GPIO_DAT_STAT0              0x00
#define GPIO_DAT_STAT1              0x01
#define GPIO_DAT_OUT0               0x02
#define GPIO_DAT_OUT1               0x03
#define GPIO_DIR0                   0x04
#define GPIO_DIR1                   0x05
#define GPIO_INT_EN0                0x06
#define GPIO_INT_EN1                0x07
#define ID_REGISTER                 0x10
#define GPIO_P0_PP                  0x11

#define GPIO_LED_MODE0              0x12
#define GPIO_LED_MODE1              0x13

#define GPIO_P10_CC                 0x20
#define GPIO_P11_CC                 0x21
#define GPIO_P12_CC                 0x22
#define GPIO_P13_CC                 0x23
#define GPIO_P00_CC                 0x24
#define GPIO_P01_CC                 0x25
#define GPIO_P02_CC                 0x26
#define GPIO_P03_CC                 0x27
#define GPIO_P04_CC                 0x28
#define GPIO_P05_CC                 0x29
#define GPIO_P06_CC                 0x2A
#define GPIO_P07_CC                 0x2B
#define GPIO_P14_CC                 0x2C
#define GPIO_P15_CC                 0x2D
#define GPIO_P16_CC                 0x2E
#define GPIO_P17_CC                 0x2F

#define RESET_REGITER               0x7F // write 0x00 to reset


/* ID register (0x10) */
#define	AW9523_ID                   0x23

/* Global Control register (0x11) */
#define AW9523B_P0_PP	           (1 << 4)

#define AW9523B_IRANGE_ALL         (0x00)
#define AW9523B_IRANGE_34          (0x01)
#define AW9523B_IRANGE_24          (0x02)
#define AW9523B_IRANGE_14          (0x03)


#define AW9523B_MAXGPIO		16
#define AW9523B_BANK(offs)	((offs) >> 3)
#define AW9523B_BIT(offs)	(1u << ((offs) & 0x7))


//struct i2c_client; /* forward declaration */

#ifdef CONFIG_GPIO_AW9523B
struct aw9523b_gpio_platform_data {
    unsigned int reset; /* reset pin */
    unsigned int irq; /* interrupt pin */
    int wakeup; /* whether wakeup en */
	int gpio_start;		/* GPIO Chip base # */
	const char *const *names;
	unsigned irq_base;	/* interrupt base # */
	int	(*setup)(struct i2c_client *client,
				int gpio, unsigned ngpio,
				void *context);
	int	(*teardown)(struct i2c_client *client,
				int gpio, unsigned ngpio,
				void *context);
	void *context;
};
#endif


#ifdef CONFIG_LEDS_AW9523B

#define AW_PORT0  0
#define AW_PORT1  1

#define AW_PBIT0  0
#define AW_PBIT1  1
#define AW_PBIT2  2
#define AW_PBIT3  3
#define AW_PBIT4  4
#define AW_PBIT5  5
#define AW_PBIT6  6
#define AW_PBIT7  7

#define AW_MODE_GPIO  1
#define AW_MODE_LED   0

#define AW_MAKE_FLAGS(_port, _bit, _mode, _defval)  (((_port) << 24) | \
                                                    ((_bit) << 16) | \
                                                    ((_mode) << 8)| \
                                                    (_defval))

#define AW_GET_PORT(_flags)  ((unsigned char)((unsigned int)(_flags) >> 24))
#define AW_GET_PBIT(_flags)  ((unsigned char)((unsigned int)(_flags) >> 16))
#define AW_GET_MODE(_flags)  ((unsigned char)((unsigned int)(_flags) >> 8))
#define AW_GET_DEFVAL(_flags)  ((unsigned char)(_flags))

enum aw9523b_outdrv {
	AW9523B_OPEN_DRAIN,
	AW9523B_TOTEM_POLE, /* push-pull */
};

struct aw9523b_leds_platform_data {
    unsigned int power; /* power pin */
    unsigned int reset; /* reset pin */
    enum aw9523b_outdrv outdrv;
    struct led_platform_data leds;
};

#endif

#endif
