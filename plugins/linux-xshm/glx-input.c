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

struct glx_data {
	Display *dpy;
	Screen *screen;
	Window root_window;

	int depth;
	uint32_t width, height;

	Pixmap x11_pix;
	GLXPixmap glx_pix;
	GLXFBConfig *glx_fbconfigs;

	/* texture and dummy */
	texture_t texture, dummy;
};

// glx functions
static PFNGLXBINDTEXIMAGEEXTPROC glXBindTexImageEXT_func = NULL;
static PFNGLXRELEASETEXIMAGEEXTPROC glXReleaseTexImageEXT_func = NULL;

static const char* glx_input_getname(const char* locale)
{
	return "GLX Screen Input";
}


void glx_input_destroy(void *vptr)
{
	GLX_DATA(vptr);

	if (!data)
		return;

	XCompositeUnredirectWindow(
		data->dpy,
		data->root_window,
		CompositeRedirectAutomatic
	);

	gs_entercontext(obs_graphics());

	if (data->glx_pix)
		glXDestroyPixmap(data->dpy, data->glx_pix);

	texture_destroy(data->dummy);
	texture_destroy(data->buffer);

	gs_leavecontext();

	if (data->glx_fbconfigs)
		XFree(data->glx_fbconfigs);

	if (data->x11_pix)
		XFreePixmap(data->dpy, data->x11_pix);

	if (data->dpy)
		XCloseDisplay(data->dpy);

	bfree(data);
}


static void *glx_input_create(obs_data_t settings, obs_source_t source)
{
	// create data structure
	struct glx_data *data = bmalloc(sizeof(struct glx_data));
	memset(data, 0, sizeof(struct glx_data));

	data->dpy = XOpenDisplay(NULL);
	data->screen = XDefaultScreenOfDisplay(data->dpy);
	//data->root_window = RootWindowOfScreen(data->screen);
	data->root_window = 0x140005f;
	data->width = data->screen->width;
	data->height = data->screen->height;

	int event_base, error_base;
	if (!XCompositeQueryExtension(data->dpy, &event_base, &error_base)) {
		blog(LOG_ERROR, "XComposite Extension not found");
		goto fail;
	}

	gs_entercontext(obs_graphics());

	XCompositeRedirectWindow(
		data->dpy,
		data->root_window,
		CompositeRedirectAutomatic
	);

	data->x11_pix = XCompositeNameWindowPixmap(data->dpy,
			data->root_window);

	blog(LOG_DEBUG, "Redirected window to pixmap: %u", data->x11_pix);

	XWindowAttributes winAttr;
	XGetWindowAttributes(data->dpy, data->root_window, &winAttr);
	blog(LOG_DEBUG, "Window: %dx%d", winAttr.width, winAttr.height);

	const int configAttribs[] = {
		GLX_BIND_TO_TEXTURE_RGBA_EXT, true,
		GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
		GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
		GLX_BIND_TO_TEXTURE_RGBA_EXT, GL_TRUE,
		GLX_DOUBLEBUFFER, GL_FALSE,
		/*
		GLX_Y_INVERTED_EXT, GLX_DONT_CARE,
		*/
		None
	};

	int num_elements = 0;
	data->glx_fbconfigs = glXChooseFBConfig(data->dpy,
		DefaultScreen(data->dpy), configAttribs, &num_elements);

	blog(LOG_DEBUG, "got %u configs", num_elements);

	glXBindTexImageEXT_func = (PFNGLXBINDTEXIMAGEEXTPROC)
		glXGetProcAddress((GLubyte *) "glXBindTexImageEXT");
	glXReleaseTexImageEXT_func = (PFNGLXRELEASETEXIMAGEEXTPROC)
		glXGetProcAddress((GLubyte*) "glXReleaseTexImageEXT");

	data->width = winAttr.width;
	data->height = winAttr.height;

	data->dummy = gs_create_texture(data->width, data->height,
			GS_RGBA, 1, NULL, GS_GL_DUMMYTEX);
	data->texture = gs_create_texture(data->width, data->height,
			GS_RGBA, 1, NULL, 0);

	const int pixmapAttribs[] = {
		GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
		GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
		None
	};

	data->glx_pix = glXCreatePixmap(data->dpy, data->glx_fbconfigs[0],
		data->x11_pix, pixmapAttribs);
	blog(LOG_DEBUG, "Got glx pixmap: %u", data->glx_pix);

	gs_leavecontext();

	return data;

fail:
	glx_input_destroy(data);
	return NULL;
}

static void glx_input_video_tick(void *vptr, float seconds)
{
	GLX_DATA(vptr);
	GLuint *dummy_handle = texture_getobj(data->dummy);

	gs_entercontext(obs_graphics());

	/* simply binding to the texture before doing the copy should just
	 * associate the glx buffer with the texture object, even if it's
	 * unbound or changes to another texture unit (I think) */
	glBindTexture(GL_TEXTURE_2D, *dummy_handle);
	glXBindTexImageEXT_func(data->dpy, data->glx_pix, GLX_FRONT_EXT, NULL);

	gs_copy_texture(data->texture, data->dummy);

	glXReleaseTexImageEXT_func(data->dpy, data->glx_pix, GLX_FRONT_EXT);

	gs_leavecontext();
}

static void glx_input_video_render(void *vptr, effect_t effect)
{
	GLX_DATA(vptr);

	eparam_t image = effect_getparambyname(effect, "image");
	effect_settexture(effect, image, data->buffer);

	/* Jim should really make a way to push/pop states */
	gs_enable_blending(false);
	gs_draw_sprite(data->buffer, 0, 0, 0);
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
