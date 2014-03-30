/*
Copyright (C) 2014 by Leonhard Oelke <leonhard@in-verted.de>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <obs.h>

#define GLX_DATA(voidptr) struct glx_data *data = voidptr;

/* flags */
#define GLX_NVIDIA	(1<<0)
#define GLX_ATI		(1<<1)
#define GLX_INTEL	(1<<2)
#define GLX_MESA	(1<<3)

struct glx_data {
	Display *dpy;
	Screen *screen;
	Window window;

	int depth;
	int_fast32_t width, height;

	/* X11 & GLX Pixmap */
	Pixmap x11_pix;
	GLXPixmap glx_pix;
	GLXFBConfig glx_fbconfig;

	/* texture and dummy */
	texture_t texture, dummy;

	uint_fast32_t flags;
};

static const char* glx_input_getname(const char* locale)
{
	UNUSED_PARAMETER(locale);
	return "GLX Screen Input";
}

/* glx functions */
static PFNGLXBINDTEXIMAGEEXTPROC glXBindTexImageEXT = NULL;
static PFNGLXRELEASETEXIMAGEEXTPROC glXReleaseTexImageEXT = NULL;

/**
 * Get handles to the glx functions
 */
static int_fast32_t glx_get_functions()
{
	glXBindTexImageEXT = (PFNGLXBINDTEXIMAGEEXTPROC)
		glXGetProcAddress((GLubyte *) "glXBindTexImageEXT");
	glXReleaseTexImageEXT = (PFNGLXRELEASETEXIMAGEEXTPROC)
		glXGetProcAddress((GLubyte*) "glXReleaseTexImageEXT");

	return 0;
}

/**
 * Get Server info
 */
static void glx_server_info(struct glx_data *data)
{
	const char *glx_vendor = glXQueryServerString(data->dpy,
		XDefaultScreen(data->dpy), GLX_VENDOR);
	blog(LOG_DEBUG, "glx-input: Server Vendor: %s", glx_vendor);

	if (!strcmp(glx_vendor, "NVIDIA Corporation"))
		data->flags |= GLX_NVIDIA;
}

/**
 * Get the fbconfig to use for the glx pixmap
 *
 * @return < 0 on error, >= 0 on success
 */
static int_fast32_t glx_get_fbconfig(struct glx_data *data)
{
	int num = 0;
	GLXFBConfig *fbconfigs;

	const int configAttribs[] = {
		GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
		GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
		GLX_BIND_TO_TEXTURE_RGBA_EXT, GL_TRUE,
		GLX_DOUBLEBUFFER, GL_FALSE,
		None
	};
	fbconfigs = glXChooseFBConfig(data->dpy,
		DefaultScreen(data->dpy), configAttribs, &num);

	if (!num)
		return -1;

	blog(LOG_DEBUG, "glx-input: found %d fbconfigs", num);

	data->glx_fbconfig = fbconfigs[0];
	XFree(fbconfigs);

	return 0;
}

/**
 * Update the pixmaps
 */
static void glx_update_pixmaps(struct glx_data *data)
{
	if (data->glx_pix)
		glXDestroyPixmap(data->dpy, data->glx_pix);
	if (data->x11_pix)
		XFreePixmap(data->dpy, data->x11_pix);

	data->x11_pix = XCompositeNameWindowPixmap(data->dpy, data->window);
	blog(LOG_DEBUG, "glx-input: x11 pixmap: %u", data->x11_pix);

	const int pixmapAttribs[] = {
		GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
		GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
		None
	};

	data->glx_pix = glXCreatePixmap(data->dpy, data->glx_fbconfig,
		data->x11_pix, pixmapAttribs);
	blog(LOG_DEBUG, "glx-input: glx pixmap: %u", data->glx_pix);
}

/**
 * Resize the obs textures
 */
static void glx_resize_textures(struct glx_data *data)
{
	if (data->dummy)
		texture_destroy(data->dummy);
	if (data->texture)
		texture_destroy(data->texture);

	data->dummy = gs_create_texture(data->width, data->height,
		GS_RGBA, 1, NULL, GS_GL_DUMMYTEX);
	data->texture = gs_create_texture(data->width, data->height,
		GS_RGBA, 1, NULL, 0);
}

/**
 * Bind the redirected texture
 */
static void glx_bind_texture(struct glx_data *data)
{
	GLuint dummy_handle = *(GLuint*) texture_getobj(data->dummy);
	glBindTexture(GL_TEXTURE_2D, dummy_handle);

	glXWaitX();
	glXBindTexImageEXT(data->dpy, data->glx_pix, GLX_FRONT_EXT, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
}

/**
 * Release the redirected texture
 */
static void glx_release_texture(struct glx_data *data)
{
	if (!data->glx_pix)
		return;

	glXReleaseTexImageEXT(data->dpy, data->glx_pix, GLX_FRONT_EXT);
}

/**
 * Refresh the redirected texture
 */
static void glx_refresh_texture(struct glx_data *data)
{
	if (data->flags & GLX_NVIDIA) {
		glXWaitX();
		return;
	}

	glx_release_texture(data);
	glx_bind_texture(data);
}

/**
 * Check for geometry changes
 *
 * @return < 0 on error, 0 on no change, > 0 on change
 */
static int_fast32_t glx_check_geometry(struct glx_data *data)
{
	XWindowAttributes winAttr;
	if (!XGetWindowAttributes(data->dpy, data->window, &winAttr))
		return -1;

	if (data->width == winAttr.width && data->height == winAttr.height)
		return 0;

	data->width = winAttr.width;
	data->height = winAttr.height;

	glx_release_texture(data);

	glx_update_pixmaps(data);
	glx_resize_textures(data);

	glx_bind_texture(data);

	return 1;
}

static void glx_input_destroy(void *vptr)
{
	GLX_DATA(vptr);

	if (!data)
		return;

	XCompositeUnredirectWindow(
		data->dpy,
		data->window,
		CompositeRedirectAutomatic
	);

	gs_entercontext(obs_graphics());

	glx_release_texture(data);

	if (data->glx_pix)
		glXDestroyPixmap(data->dpy, data->glx_pix);
	if (data->x11_pix)
		XFreePixmap(data->dpy, data->x11_pix);

	if (data->dummy)
		texture_destroy(data->dummy);
	if (data->texture)
		texture_destroy(data->texture);

	gs_leavecontext();

	if (data->dpy)
		XCloseDisplay(data->dpy);

	bfree(data);
}

static void *glx_input_create(obs_data_t settings, obs_source_t source)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(source);

	// create data structure
	struct glx_data *data = bzalloc(sizeof(struct glx_data));

	data->dpy = XOpenDisplay(NULL);
	data->screen = XDefaultScreenOfDisplay(data->dpy);
	//data->root_window = RootWindowOfScreen(data->screen);
	data->window = 0x6000059;
	data->width = data->screen->width;
	data->height = data->screen->height;

	int event_base, error_base;
	if (!XCompositeQueryExtension(data->dpy, &event_base, &error_base)) {
		blog(LOG_ERROR, "glx-input: XComposite extension not found !");
		goto fail;
	}

	gs_entercontext(obs_graphics());

	if (glx_get_functions() < 0) {
		blog(LOG_ERROR, "glx-input: Unable to get functions !");
		goto fail;
	}

	if (glx_get_fbconfig(data) < 0) {
		blog(LOG_ERROR, "glx-input: No useful fbconfigs found !");
		goto fail;
	}

	XCompositeRedirectWindow(
		data->dpy,
		data->window,
		CompositeRedirectAutomatic
	);

	glx_server_info(data);

	glx_check_geometry(data);

	gs_leavecontext();

	return data;

fail:
	glx_input_destroy(data);
	return NULL;
}

static void glx_input_video_tick(void *vptr, float seconds)
{
	UNUSED_PARAMETER(seconds);
	GLX_DATA(vptr);

	if (!data->texture)
		return;

	gs_entercontext(obs_graphics());

	if (!glx_check_geometry(data))
		glx_refresh_texture(data);

	gs_copy_texture(data->texture, data->dummy);

	gs_leavecontext();
}

static void glx_input_video_render(void *vptr, effect_t effect)
{
	GLX_DATA(vptr);

	if (!data->texture)
		return;

	eparam_t image = effect_getparambyname(effect, "image");
	effect_settexture(effect, image, data->texture);

	gs_enable_blending(false);
	gs_draw_sprite(data->texture, 0, 0, 0);
	gs_enable_blending(true);
}

static uint32_t glx_input_getwidth(void *vptr)
{
	GLX_DATA(vptr);
	return data->width;
}

static uint32_t glx_input_getheight(void *vptr)
{
	GLX_DATA(vptr);
	return data->height;
}

struct obs_source_info glx_input = {
	.id           = "glx_input",
	.type         = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.getname      = glx_input_getname,
	.create       = glx_input_create,
	.destroy      = glx_input_destroy,
	.video_tick   = glx_input_video_tick,
	.video_render = glx_input_video_render,
	.getwidth     = glx_input_getwidth,
	.getheight    = glx_input_getheight
};
