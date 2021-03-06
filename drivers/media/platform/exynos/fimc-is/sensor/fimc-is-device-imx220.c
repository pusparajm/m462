/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <mach/exynos-fimc-is-sensor.h>

#include "../fimc-is-core.h"
#include "../fimc-is-device-sensor.h"
#include "../fimc-is-resourcemgr.h"
#include "../fimc-is-hw.h"
#include "fimc-is-device-imx220.h"

#define SENSOR_NAME "IMX220"

static struct fimc_is_sensor_cfg config_imx220[] = {
	/* 5248x3936@12fps */
	FIMC_IS_SENSOR_LONG_CFG(5248, 3936, 15, 18, 0, 0),
	//FIMC_IS_SENSOR_CFG(5248, 3936, 5, 15, 0),
	/* 2642x1968@24fps */
	FIMC_IS_SENSOR_LONG_CFG(2624, 1968, 30, 9, 1, 0),
	/* 1312x984@48fps */
	FIMC_IS_SENSOR_LONG_CFG(1312, 984, 48, 8, 2, 0),
	/* 2064x1546@31fps  -- 2x2 binning*/
	FIMC_IS_SENSOR_LONG_CFG(2064, 1546, 31, 8, 3, 0),
	/* 1936x1090@30fps -- 2x2 binning*/
	FIMC_IS_SENSOR_LONG_CFG(1936, 1090, 30, 8, 4, 0),
	/* 1296x730@64fps -- 4x4 binning*/
	FIMC_IS_SENSOR_LONG_CFG(1296, 730, 60, 9, 5, 0),
	/* 2064x1546@31fps -- HDR */
	FIMC_IS_SENSOR_LONG_CFG(2064, 1546, 30, 8, 6, 0),
	/* 1936x10900@44fps -- HDR */
	FIMC_IS_SENSOR_LONG_CFG(1936, 1090, 44, 8, 7, 0),
	/* 4480x2682@22fps */
	FIMC_IS_SENSOR_LONG_CFG(4480, 2682, 22, 17, 8, 0),
	/* 3856x2170@30fps */
	FIMC_IS_SENSOR_LONG_CFG(3856, 2170, 30, 18, 9, 0),
	/* 1296x730@100fps */
	FIMC_IS_SENSOR_LONG_CFG(1296, 730, 100, 9, 10, 0),
	/* 4096x2458@24fps */
	FIMC_IS_SENSOR_LONG_CFG(4096, 2458, 24, 18, 11, 0),
	/* 4496x2698@24fps */
	FIMC_IS_SENSOR_LONG_CFG(4496, 2698, 24, 18, 12, 0),
	/* 1184x714@30fps */
	FIMC_IS_SENSOR_LONG_CFG(1184, 714, 30, 4, 13, 0),
	/* 5248x3936@1fps */
	FIMC_IS_SENSOR_LONG_CFG(5248, 3936, 1, 7, 14, 1),
};

static int sensor_imx220_init(struct v4l2_subdev *subdev, u32 val)
{
	int ret = 0;
	struct fimc_is_module_enum *module;

	BUG_ON(!subdev);

	module = (struct fimc_is_module_enum *)v4l2_get_subdevdata(subdev);

	pr_info("[MOD:D:%d] %s(%d)\n", module->id, __func__, val);

	return ret;
}

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_imx220_init
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops
};

int sensor_imx220_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct v4l2_subdev *subdev_module;
	struct fimc_is_module_enum *module;
	struct fimc_is_device_sensor *device;
	struct sensor_open_extended *ext;

	BUG_ON(!fimc_is_dev);

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core device is not yet probed");
		return -EPROBE_DEFER;
	}

	device = &core->sensor[SENSOR_IMX220_INSTANCE];

	subdev_module = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_module) {
		err("subdev_module is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	module = &device->module_enum[atomic_read(&core->resourcemgr.rsccount_module)];
	atomic_inc(&core->resourcemgr.rsccount_module);
	module->id = SENSOR_NAME_IMX220;
	module->subdev = subdev_module;
	module->device = SENSOR_IMX220_INSTANCE;
	module->client = client;
	module->active_width = 5232;
	module->active_height = 3926;
	module->pixel_width = module->active_width + 16;
	module->pixel_height = module->active_height + 10;
	module->max_framerate = 300;
	module->position = SENSOR_POSITION_REAR;
	module->mode = CSI_MODE_CH0_ONLY;
	module->lanes = CSI_DATA_LANES_4;
	module->setfile_name = "setfile_imx220.bin";
	module->cfgs = ARRAY_SIZE(config_imx220);
	module->cfg = config_imx220;
	module->ops = NULL;
	module->private_data = NULL;

	ext = &module->ext;
	ext->mipi_lane_num = module->lanes;
	ext->I2CSclk = I2C_L0;

	ext->sensor_con.product_name = SENSOR_NAME_IMX220;
	ext->sensor_con.peri_type = SE_I2C;
	ext->sensor_con.peri_setting.i2c.channel = SENSOR_CONTROL_I2C0;
	ext->sensor_con.peri_setting.i2c.slave_address = 0x20;
	ext->sensor_con.peri_setting.i2c.speed = 400000;

	ext->actuator_con.product_name = ACTUATOR_NAME_LC898212;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel = SENSOR_CONTROL_I2C0;

//	Support LM3644 LED Driver by Kundong.kim@smasung.com

	ext->flash_con.product_name = FLADRV_NAME_LM3644;
	ext->flash_con.peri_type = SE_I2C;
	ext->flash_con.peri_setting.gpio.first_gpio_port_no = 2;  //strobe
	ext->flash_con.peri_setting.gpio.second_gpio_port_no = 3; //torch

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->companion_con.product_name = COMPANION_NAME_NOTHING;
	ext->sOISCon.product_name = OIS_NAME_NOTHING;

	if (client)
		v4l2_i2c_subdev_init(subdev_module, client, &subdev_ops);
	else
		v4l2_subdev_init(subdev_module, &subdev_ops);

	v4l2_set_subdevdata(subdev_module, module);
	v4l2_set_subdev_hostdata(subdev_module, device);
	snprintf(subdev_module->name, V4L2_SUBDEV_NAME_SIZE, "sensor-subdev.%d", module->id);

p_err:
	info("%s(%d)\n", __func__, ret);
	return ret;
}
