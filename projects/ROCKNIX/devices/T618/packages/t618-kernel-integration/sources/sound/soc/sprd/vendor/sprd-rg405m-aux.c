// SPDX-License-Identifier: GPL-2.0-only
#include <linux/platform_device.h>
#include <sound/soc.h>

int sipa_audio_power_scene_set(int on)
{
	return 0;
}

int soc_aux_init_only_sia81xx(struct platform_device *pdev,
			      struct snd_soc_card *card)
{
	return 0;
}
