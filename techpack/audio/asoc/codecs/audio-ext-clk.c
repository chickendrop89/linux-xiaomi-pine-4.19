// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2018, 2019 The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk/msm-clk-provider.h>
#include <linux/clk/msm-clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <dt-bindings/clock/msm-clocks-8952.h>
#include <dsp/q6afe-v2.h>
#include "audio-ext-clk.h"

enum audio_clk_mux {
	PMI_CLK,
	AP_CLK2,
	LPASS_MCLK,
};

struct pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *sleep;
	struct pinctrl_state *active;
};

struct audio_ext_ap_clk {
	bool enabled;
	int gpio;
	struct clk c;
};

struct audio_ext_ap_clk2 {
	bool enabled;
	struct pinctrl_info pnctrl_info;
	struct clk c;
};

struct audio_ext_pmi_clk {
	int gpio;
	bool enabled;
	struct pinctrl_info pnctrl_info;
	struct clk c;
};

static struct afe_clk_set clk2_config = {
	Q6AFE_LPASS_CLK_CONFIG_API_VERSION,
	Q6AFE_LPASS_CLK_ID_SPEAKER_I2S_OSR,
	Q6AFE_LPASS_IBIT_CLK_11_P2896_MHZ,
	Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	0,
};

static inline struct audio_ext_ap_clk *to_audio_ap_clk(struct clk *clk)
{
	return container_of(clk, struct audio_ext_ap_clk, c);
}

static inline struct audio_ext_pmi_clk *to_audio_pmi_clk(struct clk *clk)
{
	return container_of(clk, struct audio_ext_pmi_clk, c);
}

static int audio_ext_pmi_clk_prepare(struct clk *clk)
{
	struct audio_ext_pmi_clk *audio_pmi_clk = to_audio_pmi_clk(clk);
	struct pinctrl_info *pnctrl_info = &audio_pmi_clk->pnctrl_info;
	int ret;

	if (!pnctrl_info->pinctrl || !pnctrl_info->active) {
		pr_err("%s: pinctrl state not defined\n", __func__);
		return -EINVAL;
	}

	ret = pinctrl_select_state(pnctrl_info->pinctrl,
					pnctrl_info->active);
	if (ret) {
		pr_err("%s: active state select failed with %d\n",
			__func__, ret);
		return -EIO;
	}
	return 0;
}

static void audio_ext_pmi_clk_unprepare(struct clk *clk)
{
	struct audio_ext_pmi_clk *audio_pmi_clk = to_audio_pmi_clk(clk);
	struct pinctrl_info *pnctrl_info = &audio_pmi_clk->pnctrl_info;
	int ret;

	if (!pnctrl_info->pinctrl || !pnctrl_info->active) {
		pr_err("%s: pinctrl state not defined\n", __func__);
		return;
	}

	ret = pinctrl_select_state(pnctrl_info->pinctrl,
					pnctrl_info->sleep);
	if (ret)
		pr_err("%s: sleep state select failed with %d\n",
			__func__, ret);
}

static int audio_ext_clk_prepare(struct clk *clk)
{
	struct audio_ext_ap_clk *audio_clk = to_audio_ap_clk(clk);

	pr_debug("%s: gpio: %d\n", __func__, audio_clk->gpio);
	if (gpio_is_valid(audio_clk->gpio))
		return gpio_direction_output(audio_clk->gpio, 1);
	return 0;
}

static void audio_ext_clk_unprepare(struct clk *clk)
{
	struct audio_ext_ap_clk *audio_clk = to_audio_ap_clk(clk);

	pr_debug("%s: gpio: %d\n", __func__, audio_clk->gpio);
	if (gpio_is_valid(audio_clk->gpio))
		gpio_direction_output(audio_clk->gpio, 0);
}

static inline struct audio_ext_ap_clk2 *to_audio_ap_clk2(struct clk *clk)
{
	return container_of(clk, struct audio_ext_ap_clk2, c);
}

static int audio_ext_clk2_prepare(struct clk *clk)
{
	struct audio_ext_ap_clk2 *audio_clk2 = to_audio_ap_clk2(clk);
	struct pinctrl_info *pnctrl_info = &audio_clk2->pnctrl_info;
	int ret;


	if (!pnctrl_info->pinctrl || !pnctrl_info->active)
		return 0;

	ret = pinctrl_select_state(pnctrl_info->pinctrl,
				   pnctrl_info->active);
	if (ret) {
		pr_err("%s: active state select failed with %d\n",
			__func__, ret);
		return -EIO;
	}

	clk2_config.enable = 1;
	ret = afe_set_lpass_clk_cfg(IDX_RSVD_3, &clk2_config);
	if (ret < 0) {
		pr_err("%s: failed to set clock, ret = %d\n", __func__, ret);
		return -EINVAL;
	}

	return 0;
}

static void audio_ext_clk2_unprepare(struct clk *clk)
{
	struct audio_ext_ap_clk2 *audio_clk2 = to_audio_ap_clk2(clk);
	struct pinctrl_info *pnctrl_info = &audio_clk2->pnctrl_info;
	int ret;

	if (!pnctrl_info->pinctrl || !pnctrl_info->sleep)
		return;

	ret = pinctrl_select_state(pnctrl_info->pinctrl,
				   pnctrl_info->sleep);
	if (ret)
		pr_err("%s: sleep state select failed with %d\n",
			__func__, ret);

	clk2_config.enable = 0;
	ret = afe_set_lpass_clk_cfg(IDX_RSVD_3, &clk2_config);
	if (ret < 0)
		pr_err("%s: failed to reset clock, ret = %d\n", __func__, ret);
}

static const struct clk_ops audio_ext_ap_clk_ops = {
	.prepare = audio_ext_clk_prepare,
	.unprepare = audio_ext_clk_unprepare,
};

static const struct clk_ops audio_ext_ap_clk2_ops = {
	.prepare = audio_ext_clk2_prepare,
	.unprepare = audio_ext_clk2_unprepare,
};

static const struct clk_ops audio_ext_pmi_clk_ops = {
	.prepare = audio_ext_pmi_clk_prepare,
	.unprepare = audio_ext_pmi_clk_unprepare,
};
static struct audio_ext_pmi_clk audio_pmi_lnbb_clk = {
	.gpio = -EINVAL,
	.c = {
		.dbg_name = "audio_ext_pmi_lnbb_clk",
		CLK_INIT(audio_pmi_lnbb_clk.c),
	},
};

static struct audio_ext_ap_clk audio_ap_clk = {
	.gpio = -EINVAL,
	.c = {
		.dbg_name = "audio_ext_ap_clk",
		.ops = &audio_ext_ap_clk_ops,
		CLK_INIT(audio_ap_clk.c),
	},
};

static struct audio_ext_ap_clk2 audio_ap_clk2 = {
	.c = {
		.dbg_name = "audio_ext_ap_clk2",
		.ops = &audio_ext_ap_clk2_ops,
		CLK_INIT(audio_ap_clk2.c),
	},
};

static struct audio_ext_pmi_clk audio_pmi_clk = {
	.c = {
		.dbg_name = "audio_ext_pmi_clk",
		.ops = &audio_ext_pmi_clk_ops,
		CLK_INIT(audio_pmi_clk.c),
	},
};

static struct clk_lookup audio_ref_clock[] = {
	CLK_LIST(audio_ap_clk),
	CLK_LIST(audio_pmi_clk),
	CLK_LIST(audio_ap_clk2),
};

static int audio_get_pinctrl(struct platform_device *pdev,
		enum audio_clk_mux mux)
{
	struct device *dev =  &pdev->dev;
	struct pinctrl_info *pnctrl_info;
	struct pinctrl *pinctrl;
	int ret;

	switch (mux) {
	case PMI_CLK:
		pnctrl_info = &audio_pmi_clk.pnctrl_info;
		break;
	case AP_CLK2:
		pnctrl_info = &audio_ap_clk2.pnctrl_info;
		break;
	default:
		dev_err(dev, "%s Not a valid MUX ID: %d\n",
			__func__, mux);
		return -EINVAL;
	}

	if (pnctrl_info->pinctrl) {
		dev_dbg(&pdev->dev, "%s: already requested before\n",
			__func__);
		return -EINVAL;
	}

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(pinctrl)) {
		dev_dbg(&pdev->dev, "%s: Unable to get pinctrl handle\n",
			__func__);
		return -EINVAL;
	}
	pnctrl_info->pinctrl = pinctrl;
	/* get all state handles from Device Tree */
	pnctrl_info->sleep = pinctrl_lookup_state(pinctrl, "sleep");
	if (IS_ERR(pnctrl_info->sleep)) {
		dev_err(&pdev->dev, "%s: could not get sleep pinstate\n",
			__func__);
		goto err;
	}
	pnctrl_info->active = pinctrl_lookup_state(pinctrl, "active");
	if (IS_ERR(pnctrl_info->active)) {
		dev_err(&pdev->dev, "%s: could not get active pinstate\n",
			__func__);
		goto err;
	}
	/* Reset the TLMM pins to a default state */
	ret = pinctrl_select_state(pnctrl_info->pinctrl,
				   pnctrl_info->sleep);
	if (ret) {
		dev_err(&pdev->dev, "%s: Disable TLMM pins failed with %d\n",
			__func__, ret);
		goto err;
	}
	return 0;

err:
	devm_pinctrl_put(pnctrl_info->pinctrl);
	return -EINVAL;
}

static int audio_ref_clk_probe(struct platform_device *pdev)
{
	int clk_gpio;
	int ret;
	struct clk *div_clk1;

	clk_gpio = of_get_named_gpio(pdev->dev.of_node,
				     "qcom,audio-ref-clk-gpio", 0);
	if (clk_gpio > 0) {
		if (of_property_read_bool(pdev->dev.of_node,
					"qcom,node_has_rpm_clock")) {
			div_clk1 = clk_get(&pdev->dev, "osr_clk");
			if (IS_ERR(div_clk1)) {
				dev_err(&pdev->dev, "Failed to get RPM div clk\n");
				ret = PTR_ERR(div_clk1);
				goto err_clk_register;
			}
			audio_pmi_clk.c.parent = div_clk1;
			audio_pmi_clk.gpio = clk_gpio;
			ret = audio_get_pinctrl(pdev, PMI_CLK);
			if (ret) {
				dev_err(&pdev->dev, "%s: Parsing pinctrl %s failed\n",
					__func__, "PMI_CLK");
				goto err_clk_register;
			}
		}
		ret = audio_get_pinctrl(pdev, AP_CLK2);
		if (ret) {
			dev_err(&pdev->dev, "%s: Parsing pinctrl %s failed\n",
						__func__, "AP_CLK2");
			goto err_clk_register;
		}
	}
	ret = of_msm_clock_register(pdev->dev.of_node, audio_ref_clock,
					ARRAY_SIZE(audio_ref_clock));
	if (ret) {
		dev_err(&pdev->dev, "%s: clock register failed\n", __func__);
		goto err_clk_register;
	}
err_clk_register:
	return ret;
}

static int audio_ref_clk_remove(struct platform_device *pdev)
{
	struct pinctrl_info *pnctrl_info = &audio_ap_clk2.pnctrl_info;
	struct pinctrl_info *pmi_pnctrl_info = &audio_pmi_clk.pnctrl_info;

	if (audio_pmi_clk.gpio > 0)
		gpio_free(audio_pmi_clk.gpio);
	else if (audio_ap_clk.gpio > 0)
		gpio_free(audio_ap_clk.gpio);

	if (pnctrl_info->pinctrl) {
		devm_pinctrl_put(pnctrl_info->pinctrl);
		pnctrl_info->pinctrl = NULL;
	}

	if (pmi_pnctrl_info->pinctrl) {
		devm_pinctrl_put(pmi_pnctrl_info->pinctrl);
		pmi_pnctrl_info->pinctrl = NULL;
	}

	return 0;
}

static const struct of_device_id audio_ref_clk_match[] = {
	{.compatible = "qcom,audio-ref-clk"},
	{}
};
MODULE_DEVICE_TABLE(of, audio_ref_clk_match);

static struct platform_driver audio_ref_clk_driver = {
	.driver = {
		.name = "audio-ref-clk",
		.owner = THIS_MODULE,
		.of_match_table = audio_ref_clk_match,
		.suppress_bind_attrs = true,
	},
	.probe = audio_ref_clk_probe,
	.remove = audio_ref_clk_remove,
};

int audio_ref_clk_platform_init(void)
{
	return platform_driver_register(&audio_ref_clk_driver);
}

void audio_ref_clk_platform_exit(void)
{
	platform_driver_unregister(&audio_ref_clk_driver);
}

MODULE_DESCRIPTION("Audio Ref Clock module platform driver");
MODULE_LICENSE("GPL v2");
