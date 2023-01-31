/*
 * linux/drivers/leds/leds-aw9523b.c
 *
 * aw9523b i2c led driver
 *
 * Copyright 2020, Howrd <howrd@21cn.com>
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/i2c/aw9523b.h>


#define DRV_NAME  "aw9523b-leds"

static const struct i2c_device_id aw9523b_id[] = {
    { DRV_NAME, 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, aw9523b_id);

struct aw9523b_led {
    struct i2c_client *client;
    struct work_struct work;
    enum led_brightness brightness;
    struct led_classdev led_cdev;
    int led_num; /* 0 .. 15 potentially */
    int flags;
    char name[32];
};

struct aw9523b_leds_data {
    struct i2c_client *client;
    u8 mode[2];
    u8 defio[2];
    u8 defcc[2][8];
    struct work_struct resume_work;
};

static struct aw9523b_leds_data s_aw9523b_data = {0};


static int aw9523b_leds_read(struct i2c_client *client, u8 reg)
{
    int ret = i2c_smbus_read_byte_data(client, reg);

    if (ret < 0)
        dev_err(&client->dev, "Read Error\n");

    return ret;
}

static int aw9523b_leds_write(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);

    if (ret < 0)
        dev_err(&client->dev, "Write Error\n");

    return ret;
}

static void aw9523b_led_work(struct work_struct *work)
{
    struct aw9523b_led *aw9523b = container_of(work, struct aw9523b_led, work);
    u8 aw_reg = 0xFF, val;
    u8 port, pbit, mode;
    int ret;

    port = AW_GET_PORT(aw9523b->flags);
    pbit = AW_GET_PBIT(aw9523b->flags);
    mode = AW_GET_MODE(aw9523b->flags);

    switch (mode) {
        case AW_MODE_GPIO:
            aw_reg = GPIO_DAT_OUT0 + port;
            ret = aw9523b_leds_read(aw9523b->client, aw_reg);
            if (ret >= 0) {
                val = (u8)ret;
                if (aw9523b->brightness > 0) {
                    val |= (1 << pbit);
                } else {
                    val &=~ (1 << pbit);
                }
                aw9523b_leds_write(aw9523b->client, aw_reg, val);
            }
            break;
        case AW_MODE_LED:
            switch (port) {
            case 0:
                if (pbit <= 7) {
                    aw_reg = GPIO_P00_CC + pbit;
                }
                break;
            case 1:
                if (pbit <= 3) {
                    aw_reg = GPIO_P10_CC + pbit;
                } else if (pbit <= 7) {
                    aw_reg = GPIO_P14_CC + pbit - 4;
                }
                break;
            default:
                break;
            }
            if (aw_reg != 0xFF) {
                aw9523b_leds_write(aw9523b->client, aw_reg, aw9523b->brightness);
            }
            break;
    }
}

static void aw9523b_led_set(struct led_classdev *led_cdev,
                            enum led_brightness value)
{
    struct aw9523b_led *aw9523b;

    aw9523b = container_of(led_cdev, struct aw9523b_led, led_cdev);

    aw9523b->brightness = value;

    /*
     * Must use workqueue for the actual I/O since I2C operations
     * can sleep.
     */
    schedule_work(&aw9523b->work);
}

static void aw9523b_led_resume_work(struct work_struct *work)
{
    struct aw9523b_leds_data *aw9523b_data =
                container_of(work, struct aw9523b_leds_data, resume_work);
    struct i2c_client *client = aw9523b_data->client;
    struct aw9523b_leds_platform_data *pdata;
    int i;
    u8 bmode;

    pr_info("aw9523b_leds_resume:: set gpio default state\n");

    pdata = client->dev.platform_data;

    if (gpio_is_valid(pdata->power)) {
        gpio_direction_output(pdata->power, 1);
        // wait power stable
        msleep(1);
    } else {
        pr_info("power pin invalid, exit!!!\n");
        return;
    }

    if (gpio_is_valid(pdata->reset)) {
        gpio_direction_output(pdata->reset, 1);
        udelay(50);
        gpio_direction_output(pdata->reset, 0);
        udelay(100);
        gpio_direction_output(pdata->reset, 1);
    }

    /* set LED mode */
    aw9523b_leds_write(client, GPIO_LED_MODE0, s_aw9523b_data.mode[0]);
    aw9523b_leds_write(client, GPIO_LED_MODE1, s_aw9523b_data.mode[1]);

    /* Configure output: open-drain or totem pole (push-pull) */
    if (pdata && pdata->outdrv == AW9523B_TOTEM_POLE) {
        bmode = aw9523b_leds_read(client, GPIO_P0_PP);
        bmode |= (1 << 4);
        aw9523b_leds_write(client, GPIO_P0_PP, bmode);
    }

    /* set default gpio value */
    aw9523b_leds_write(client, GPIO_DAT_OUT0, s_aw9523b_data.defio[0]);
    aw9523b_leds_write(client, GPIO_DAT_OUT1, s_aw9523b_data.defio[1]);

    /* set default current control value */
    // P0 port
    for (i = 0; i < 8; i++) {
        aw9523b_leds_write(client, GPIO_P00_CC + i, s_aw9523b_data.defcc[0][i]);
    }

    // P1 port
    for (i = 0; i <= 3; i++) {
        aw9523b_leds_write(client, GPIO_P10_CC + i, s_aw9523b_data.defcc[1][i]);
    }
    for (i = 4; i <= 7; i++) {
        aw9523b_leds_write(client, GPIO_P14_CC + i-4, s_aw9523b_data.defcc[1][i]);
    }
}

//20H DIM0 P1_0 LED current control
//21H DIM1 P1_1 LED current control
//22H DIM2 P1_2 LED current control
//23H DIM3 P1_3 LED current control
//24H DIM4 P0_0 LED current control
//25H DIM5 P0_1 LED current control
//26H DIM6 P0_2 LED current control
//27H DIM7 P0_3 LED current control
//28H DIM8 P0_4 LED current control
//29H DIM9 P0_5 LED current control
//2AH DIM10 P0_6 LED current control
//2BH DIM11 P0_7 LED current control
//2CH DIM12 P1_4 LED current control
//2DH DIM13 P1_5 LED current control
//2EH DIM14 P1_6 LED current control
//2FH DIM15 P1_7 LED current control
static int aw9523b_leds_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct aw9523b_led *aw9523b;
    struct aw9523b_leds_platform_data *pdata;
    int i, j, ret;
    u8 mode[2] = {0};
    u8 defio[2] = {0};
    u8 defcc[2][8] = {{0}, {0}};
    u8 port, pbit, bmode, defval;

    pdata = client->dev.platform_data;

    if (pdata) {
        if (pdata->leds.num_leds <= 0 || pdata->leds.num_leds > AW9523B_MAXGPIO) {
            dev_err(&client->dev, "aw9523b has most %d LEDs", AW9523B_MAXGPIO);
            return -EINVAL;
        }
    }

    aw9523b = devm_kzalloc(&client->dev, AW9523B_MAXGPIO * sizeof(*aw9523b), GFP_KERNEL);
    if (!aw9523b)
        return -ENOMEM;

    i2c_set_clientdata(client, aw9523b);

    if (gpio_is_valid(pdata->power)) {
        ret = gpio_request(pdata->power, "aw9523b-leds power");
        if (ret < 0) {
            dev_err(&client->dev, "%s:failed to set gpio power.\n", __func__);
            goto err;
        }
        gpio_direction_output(pdata->power, 1);
        // wait power stable
        msleep(1);
    }

    if (gpio_is_valid(pdata->reset)) {
        ret = gpio_request(pdata->reset, "aw9523b-leds reset");
        if (ret < 0) {
            dev_err(&client->dev, "%s:failed to set gpio reset.\n", __func__);
            goto err;
        }

        gpio_direction_output(pdata->reset, 0);
        udelay(30);
        gpio_direction_output(pdata->reset, 1);
        udelay(5);
    }

    ret = aw9523b_leds_read(client, ID_REGISTER);
    pr_info("leds-aw9523 read id: 0x%02X\n", ret);
    if (ret != AW9523_ID) {
        goto err_id;
        ret = -ENODEV;
    }

    for (i = 0; i < AW9523B_MAXGPIO; i++) {
        aw9523b[i].client = client;
        aw9523b[i].led_num = i;

        /* Platform data can specify LED names and default triggers */
        if (pdata && i < pdata->leds.num_leds) {
            if (pdata->leds.leds[i].name) {
                snprintf(aw9523b[i].name, sizeof(aw9523b[i].name), "AW.%s",
                         pdata->leds.leds[i].name);
            }
            if (pdata->leds.leds[i].default_trigger) {
                aw9523b[i].led_cdev.default_trigger =
                    pdata->leds.leds[i].default_trigger;
            }
            
            bmode = AW_GET_MODE(pdata->leds.leds[i].flags);
            port = AW_GET_PORT(pdata->leds.leds[i].flags);
            pbit = AW_GET_PBIT(pdata->leds.leds[i].flags);
            defval = AW_GET_DEFVAL(pdata->leds.leds[i].flags);
            
            if (bmode == AW_MODE_GPIO) {
                mode[port] |= (1 << pbit);
                if (defval) {
                    defio[port] |= (1 << pbit);
                }
            } else {
                defcc[port%2][pbit%8] = defval;
            }
            aw9523b[i].flags = pdata->leds.leds[i].flags;
        }
        else {
            snprintf(aw9523b[i].name, sizeof(aw9523b[i].name), "AW.%d", i);
        }

        aw9523b[i].led_cdev.name = aw9523b[i].name;
        aw9523b[i].led_cdev.brightness_set = aw9523b_led_set;

        INIT_WORK(&aw9523b[i].work, aw9523b_led_work);

        ret = led_classdev_register(&client->dev, &aw9523b[i].led_cdev);
        if (ret < 0)
            goto exit;
    }

    /* set LED mode */
    aw9523b_leds_write(aw9523b->client, GPIO_LED_MODE0, mode[0]);
    aw9523b_leds_write(aw9523b->client, GPIO_LED_MODE1, mode[1]);

    /* Configure output: open-drain or totem pole (push-pull) */
    if (pdata && pdata->outdrv == AW9523B_TOTEM_POLE) {
        bmode = aw9523b_leds_read(aw9523b->client, GPIO_P0_PP);
        bmode |= (1 << 4);
        aw9523b_leds_write(aw9523b->client, GPIO_P0_PP, bmode);
    }

    /* set default gpio value */
    aw9523b_leds_write(aw9523b->client, GPIO_DAT_OUT0, defio[0]);
    aw9523b_leds_write(aw9523b->client, GPIO_DAT_OUT1, defio[1]);

    /* set default current control value */
    // P0 port
    for (j = 0; j < 8; j++) {
        aw9523b_leds_write(aw9523b->client, GPIO_P00_CC + j, defcc[0][j]);
    }
    
    // P1 port
    for (j = 0; j <= 3; j++) {
        aw9523b_leds_write(aw9523b->client, GPIO_P10_CC + j, defcc[1][j]);
    }
    for (j = 4; j <= 7; j++) {
        aw9523b_leds_write(aw9523b->client, GPIO_P14_CC + j-4, defcc[1][j]);
    }

    s_aw9523b_data.client = client;
    memcpy(s_aw9523b_data.mode, mode, sizeof(mode));
    memcpy(s_aw9523b_data.defio, defio, sizeof(defio));
    memcpy(s_aw9523b_data.defcc, defcc, sizeof(defcc));
    INIT_WORK(&s_aw9523b_data.resume_work, aw9523b_led_resume_work);
    return 0;

exit:
    while (i--) {
        led_classdev_unregister(&aw9523b[i].led_cdev);
        cancel_work_sync(&aw9523b[i].work);
    }

err_id:
err:
    kfree(aw9523b);

    return ret;
}

static int aw9523b_leds_remove(struct i2c_client *client)
{
    struct aw9523b_led *aw9523b = i2c_get_clientdata(client);
    int i;

    for (i = 0; i < AW9523B_MAXGPIO; i++) {
        led_classdev_unregister(&aw9523b[i].led_cdev);
        cancel_work_sync(&aw9523b[i].work);
    }

    return 0;
}

static int aw9523b_leds_suspend(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    //struct aw9523b_led *aw9523b = i2c_get_clientdata(client);
    struct aw9523b_leds_platform_data *pdata;
    int i;
    u8 bmode;

    pdata = client->dev.platform_data;

    if (gpio_is_valid(pdata->power)) {
        pr_info("aw9523b_leds_suspend:: power down...\n");
        gpio_direction_output(pdata->power, 1);
        return 0;
    }

    pr_info("aw9523b_leds_suspend:: set default state\n");

    /* set LED mode */
    aw9523b_leds_write(client, GPIO_LED_MODE0, s_aw9523b_data.mode[0]);
    aw9523b_leds_write(client, GPIO_LED_MODE1, s_aw9523b_data.mode[1]);

    /* Configure output: open-drain or totem pole (push-pull) */
    if (pdata && pdata->outdrv == AW9523B_TOTEM_POLE) {
        bmode = aw9523b_leds_read(client, GPIO_P0_PP);
        bmode |= (1 << 4);
        aw9523b_leds_write(client, GPIO_P0_PP, bmode);
    }

    /* set default gpio value */
    aw9523b_leds_write(client, GPIO_DAT_OUT0, s_aw9523b_data.defio[0]);
    aw9523b_leds_write(client, GPIO_DAT_OUT1, s_aw9523b_data.defio[1]);

    /* set default current control value */
    // P0 port
    for (i = 0; i < 8; i++) {
        aw9523b_leds_write(client, GPIO_P00_CC + i, s_aw9523b_data.defcc[0][i]);
    }

    // P1 port
    for (i = 0; i <= 3; i++) {
        aw9523b_leds_write(client, GPIO_P10_CC + i, s_aw9523b_data.defcc[1][i]);
    }
    for (i = 4; i <= 7; i++) {
        aw9523b_leds_write(client, GPIO_P14_CC + i-4, s_aw9523b_data.defcc[1][i]);
    }

    return 0;
}

static int aw9523b_leds_resume(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct aw9523b_leds_platform_data *pdata;

    pdata = client->dev.platform_data;

    if (gpio_is_valid(pdata->power)) {
        schedule_work(&s_aw9523b_data.resume_work);
    } else {
        pr_info("aw9523b_leds_resume:: do nothing...\n");
    }

    return 0;
}


static SIMPLE_DEV_PM_OPS(aw9523b_leds_pm, aw9523b_leds_suspend, aw9523b_leds_resume);


static struct i2c_driver aw9523b_driver = {
    .driver = {
        .name   = DRV_NAME,
        .owner  = THIS_MODULE,
        .pm = &aw9523b_leds_pm,
    },
    .probe  = aw9523b_leds_probe,
    .remove = aw9523b_leds_remove,
    .id_table = aw9523b_id,
};

static int __init aw9523b_leds_init(void)
{
    return i2c_add_driver(&aw9523b_driver);
}

static void __exit aw9523b_leds_exit(void)
{
    i2c_del_driver(&aw9523b_driver);
}

module_init(aw9523b_leds_init);
module_exit(aw9523b_leds_exit);

MODULE_AUTHOR("howrd <howrd@21cn.com>");
MODULE_DESCRIPTION("AW9523B LED driver");
MODULE_LICENSE("GPL v2");
