#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_fourcc.h>
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_framebuffer.h>

struct drm_tutorial {
	struct drm_device *drm;

	struct drm_connector connector;
	struct drm_encoder encoder;

	struct drm_crtc crtc;
	struct drm_plane plane;
};

struct drm_display_mode s_drm_disp_mode;
static struct drm_tutorial s_drm_tutorial;
static struct platform_device *s_pdev;

static int
drm_tutorial_plane_helper_atomic_check(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *new_crtc_state = NULL;
	int ret;

	printk("%s\n", __func__);

	if (new_plane_state->crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(
			state, new_plane_state->crtc);

	ret = drm_atomic_helper_check_plane_state(
		new_plane_state, new_crtc_state, DRM_PLANE_NO_SCALING,
		DRM_PLANE_NO_SCALING, false, false);

	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	return 0;
}

static void
drm_tutorial_plane_helper_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = plane->state;
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_plane_state *old_plane_state;
	struct drm_gem_dma_object *dma_obj;
	struct drm_gem_object *obj;
	struct drm_rect rect;
	void *vaddr;
	u16 *pixels;
	int x, y;
	int idx;

	if (!fb)
		return;

	old_plane_state = drm_atomic_get_old_plane_state(state, plane);

	if (!drm_dev_enter(plane->dev, &idx))
		return;

	/* framebuffer -> GEM object */
	obj = fb->obj[0];

	/* GEM object -> DMA GEM object */
	dma_obj = to_drm_gem_dma_obj(obj);

	/* DMA GEM object -> CPU virtual address */
	vaddr = dma_obj->vaddr;
	if (!vaddr)
		goto out;

	pr_info("%s: fb=%u %ux%u pitch=%u vaddr=%p\n", __func__, fb->base.id,
		fb->width, fb->height, fb->pitches[0], vaddr);

	/* Dump x(0 ~ 15) pixels data */
	pixels = (u16 *)vaddr;
	for (y = 0; y < 4; y++) {
		for (x = 0; x < 4; x++) {
			u16 pixel;

			pixel = pixels[y * (fb->pitches[0] / 2) + x];

			pr_info("pixel_x|y:[%02d][%02d] = 0x%04x\n", x, y,
				pixel);
		}
	}

	if (drm_atomic_helper_damage_merged(old_plane_state, plane_state,
					    &rect))
		printk("%s, x1 : %u, y1 : %u, x2 : %u, y2 : %u\n", __func__,
		       rect.x1, rect.y1, rect.x2, rect.y2);
out:
	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs drm_tutorial_plane_helper_funcs = {
	.begin_fb_access = drm_gem_begin_shadow_fb_access,
	.end_fb_access = drm_gem_end_shadow_fb_access,
	.atomic_check = drm_tutorial_plane_helper_atomic_check,
	.atomic_update = drm_tutorial_plane_helper_atomic_update,
};

static const struct drm_plane_funcs drm_tutorial_plane_funcs = {
	.reset = drm_gem_reset_shadow_plane,
	.atomic_duplicate_state = drm_gem_duplicate_shadow_plane_state,
	.atomic_destroy_state = drm_gem_destroy_shadow_plane_state,
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
};

static const u32 drm_tutorial_plane_formats[] = {
	DRM_FORMAT_RGB565,
};

static const u64 drm_tutorial_plane_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static int drm_tutorial_create_plane(struct drm_tutorial *dev)
{
	struct drm_plane *plane = &dev->plane;
	int ret;

	ret = drm_universal_plane_init(dev->drm, plane, 0,
				       &drm_tutorial_plane_funcs,
				       drm_tutorial_plane_formats,
				       ARRAY_SIZE(drm_tutorial_plane_formats),
				       drm_tutorial_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(plane, &drm_tutorial_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(plane);

	return 0;
}

static enum drm_mode_status
drm_tutorial_crtc_helper_mode_valid(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode)
{
	return drm_crtc_helper_mode_valid_fixed(crtc, mode, &s_drm_disp_mode);
}

static int drm_tutorial_crtc_helper_atomic_check(struct drm_crtc *crtc,
						 struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_crtc_state(state, crtc);
	int ret;

	if (!crtc_state->enable)
		goto out;

	ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
	if (ret)
		return ret;

out:
	return drm_atomic_add_affected_planes(state, crtc);
}

static void
drm_tutorial_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	printk("%s\n", __func__);
}

static void
drm_tutorial_crtc_helper_atomic_enable(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	printk("%s\n", __func__);
}

static const struct drm_crtc_helper_funcs drm_tutorial_crtc_helper_funcs = {
	.mode_valid = drm_tutorial_crtc_helper_mode_valid,
	.atomic_check = drm_tutorial_crtc_helper_atomic_check,
	.atomic_disable = drm_tutorial_crtc_helper_atomic_disable,
	.atomic_enable = drm_tutorial_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs drm_tutorial_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.destroy = drm_crtc_cleanup,
};

static int drm_tutorial_create_crtc(struct drm_tutorial *dev)
{
	struct drm_crtc *crtc = &dev->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(dev->drm, crtc, &dev->plane, NULL,
					&drm_tutorial_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, &drm_tutorial_crtc_helper_funcs);
	return 0;
}

static const struct drm_encoder_funcs drm_tutorial_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int drm_tutorial_create_encoder(struct drm_tutorial *dev)
{
	struct drm_encoder *encoder = &dev->encoder;
	struct drm_crtc *crtc = &dev->crtc;
	int ret;

	ret = drm_encoder_init(dev->drm, encoder, &drm_tutorial_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = drm_crtc_mask(crtc);

	return 0;
}

static int drm_tutorial_connector_get_modes(struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &s_drm_disp_mode);
}

static const struct drm_connector_helper_funcs
	drm_tutorial_connector_helper_funcs = {
		.get_modes = drm_tutorial_connector_get_modes,
	};

static const struct drm_connector_funcs drm_tutorial_connector_funcs = {
	// [ 1966.394761] RIP: 0010:drm_atomic_connector_get_property+0x1a3/0x340
	.reset = drm_atomic_helper_connector_reset,

	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.destroy = drm_connector_cleanup,
};

static int drm_tutorial_create_connector(struct drm_tutorial *dev)
{
	struct drm_connector *connector = &dev->connector;
	int ret;

	ret = drm_connector_init(dev->drm, connector,
				 &drm_tutorial_connector_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ret;

	drm_connector_helper_add(connector,
				 &drm_tutorial_connector_helper_funcs);

	return 0;
}

static const struct drm_mode_config_helper_funcs
	drm_tutorial_mode_config_helper_funcs = {
		.atomic_commit_tail = drm_atomic_helper_commit_tail,
	};

static const struct drm_mode_config_funcs drm_tutorial_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_DMA_FOPS(drm_tutorial_fops);

static const struct drm_driver drm_tutorial_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &drm_tutorial_fops,
	.dumb_create = drm_gem_dma_dumb_create,
	.gem_prime_import_sg_table = drm_gem_dma_prime_import_sg_table_vmap,

// drm_client_setup(drm, NULL); - void drm_client_setup(struct drm_device *dev, const struct drm_format_info *format)
// int drm_fbdev_client_setup(struct drm_device *dev, const struct drm_format_info *format)
// drm_client_register(&fb_helper->client); - void drm_client_register(struct drm_client_dev *client)
// ret = client->funcs->hotplug(client); - &drm_fbdev_client_funcs - static int drm_fbdev_client_hotplug(struct drm_client_dev *client)
// ret = drm_fb_helper_initial_config(fb_helper); - int drm_fb_helper_initial_config(struct drm_fb_helper *fb_helper)
// ret = __drm_fb_helper_initial_config_and_unlock(fb_helper); - static int __drm_fb_helper_initial_config_and_unlock(struct drm_fb_helper *fb_helper)
//
// ret = drm_fb_helper_single_fb_probe(fb_helper); - static int drm_fb_helper_single_fb_probe(struct drm_fb_helper *fb_helper)
// if (drm_WARN_ON(dev, !dev->driver->fbdev_probe))
// 		return -EINVAL;
#ifdef CONFIG_DRM_FBDEV_EMULATION
	.fbdev_probe = drm_fbdev_dma_driver_fbdev_probe,
#else
	.fbdev_probe = NULL,
#endif

	.name = "drm_tutorial",
	.desc = "DRM Tutorial driver",
	.major = 0,
	.minor = 1,
};

static int drm_tutorial_probe(struct platform_device *pdev)
{
	struct drm_tutorial *dev = &s_drm_tutorial;
	struct drm_display_mode *mode = &s_drm_disp_mode;
	struct drm_device *drm;
	int ret;

	pr_info("%s\n", __func__);

	dev->drm = drm_dev_alloc(&drm_tutorial_driver, &pdev->dev);
	if (IS_ERR(dev->drm))
		return PTR_ERR(dev->drm);
	drm = dev->drm;

	mode->type = DRM_MODE_TYPE_DRIVER;
	mode->clock = 1;
	mode->hdisplay = 128;
	mode->hsync_start = 128;
	mode->hsync_end = 128;
	mode->htotal = 128;
	mode->vdisplay = 160;
	mode->vsync_start = 160;
	mode->vsync_end = 160;
	mode->vtotal = 160;
	mode->width_mm = 28;
	mode->height_mm = 35;

	drm_mode_config_init(dev->drm);

	drm->mode_config.min_width = 128;
	drm->mode_config.max_width = 128;
	drm->mode_config.min_height = 160;
	drm->mode_config.max_height = 160;

	// [  183.411864] RIP: 0010:drm_mode_validate_driver+0x86/0xd0
	drm->mode_config.funcs = &drm_tutorial_mode_config_funcs;

	drm->mode_config.preferred_depth = 16;
	drm->mode_config.helper_private =
		&drm_tutorial_mode_config_helper_funcs;

	ret = drm_tutorial_create_plane(dev);
	if (ret)
		return ret;

	ret = drm_tutorial_create_crtc(dev);
	if (ret)
		return ret;

	ret = drm_tutorial_create_encoder(dev);
	if (ret)
		return ret;

	ret = drm_tutorial_create_connector(dev);
	if (ret)
		return ret;

	ret = drm_connector_attach_encoder(&dev->connector, &dev->encoder);
	if (ret)
		return ret;

	drm_mode_config_reset(dev->drm);

	ret = drm_dev_register(dev->drm, 0);
	if (ret)
		return ret;

	// [28184.395330] drm_tutorial drm_tutorial: [drm] *ERROR* Failed to register client: -95
	// [28184.396708] drm_tutorial drm_tutorial: [drm] Failed to set up DRM client; error -95
	// if (!drm_core_check_feature(dev, DRIVER_MODESET) || !dev->driver->dumb_create)
	// 		return -EOPNOTSUPP;
	// #define	EOPNOTSUPP	95	/* Operation not supported on transport endpoint */
	drm_client_setup(dev->drm, NULL);

	pr_info("DRM device registered\n");

	return 0;
}

static void drm_tutorial_remove(struct platform_device *pdev)
{
	struct drm_tutorial *dev = &s_drm_tutorial;

	pr_info("%s\n", __func__);

	drm_dev_unplug(dev->drm);
	drm_atomic_helper_shutdown(dev->drm);
}

static struct platform_driver drm_tutorial_platform_driver = {
	.probe = drm_tutorial_probe,
	.remove = drm_tutorial_remove,
	.driver = { .name = "drm_tutorial" },
};

static int __init drm_tutorial_init(void)
{
	s_pdev = platform_device_register_simple("drm_tutorial", -1, NULL, 0);

	platform_driver_register(&drm_tutorial_platform_driver);

	return 0;
}

static void __exit drm_tutorial_exit(void)
{
	platform_driver_unregister(&drm_tutorial_platform_driver);

	platform_device_unregister(s_pdev);
}

module_init(drm_tutorial_init);
module_exit(drm_tutorial_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DRM tutorial driver");
