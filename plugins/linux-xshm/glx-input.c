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

    GLuint tex;
    Pixmap x11_pix;
    GLXPixmap glx_pix;
    GLXFBConfig *glx_fbconfigs;

    /* offscreen texture and framebuffer */
    GLuint fb;
    texture_t buffer;
};

struct gs_texture {
    device_t             device;
    enum gs_texture_type type;
    enum gs_color_format format;
    GLenum               gl_format;
    GLenum               gl_target;
    GLint                gl_internal_format;
    GLenum               gl_type;
    GLuint               texture;
    uint32_t             levels;
    bool                 is_dynamic;
    bool                 is_render_target;
    bool                 gen_mipmaps;

    samplerstate_t       cur_sampler;
};

struct gs_texture_2d {
    struct gs_texture    base;

    uint32_t             width;
    uint32_t             height;
    bool                 gen_mipmaps;
    GLuint               unpack_buffer;
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

	glDeleteTextures(1, &data->tex);

	glDeleteFramebuffers(1, &data->fb);
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

	data->x11_pix = XCompositeNameWindowPixmap(data->dpy, data->root_window);

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

	glGenTextures(1, &data->tex);

	blog(LOG_DEBUG, "Got Texture %u", data->tex);

	data->width = winAttr.width;
	data->height = winAttr.height;

	glGenFramebuffers(1, &data->fb);
	data->buffer = gs_create_texture(data->width, data->height,
			GS_RGBA, 1, NULL, GS_DYNAMIC);

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

static void glx_input_render_texture(struct glx_data *data)
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, data->tex);
	glXBindTexImageEXT_func(data->dpy, data->glx_pix, GLX_FRONT_EXT, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	gs_viewport_push();
	gs_setviewport(0, 0, data->width, data->height);

	glBegin(GL_QUADS);
		glTexCoord2i(0, 0);
		glVertex2f(-1, -1);

		glTexCoord2i(1, 0);
		glVertex2f(1, -1);

		glTexCoord2i(1, 1);
		glVertex2f(1, 1);

		glTexCoord2i(0, 1);
		glVertex2f(-1, 1);
	glEnd();

	gs_viewport_pop();

	glXReleaseTexImageEXT_func(data->dpy, data->glx_pix, GLX_FRONT_EXT);
}

static void glx_input_video_tick(void *vptr, float seconds)
{
	GLX_DATA(vptr);

	struct gs_texture_2d *tex_2d = (struct gs_texture_2d *) data->buffer;
	struct gs_texture tex_base = (struct gs_texture) tex_2d->base;

	gs_entercontext(obs_graphics());
	glBindFramebuffer(GL_FRAMEBUFFER, data->fb);

	glBindTexture(GL_TEXTURE_2D, tex_base.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, data->width, data->height, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex_base.texture, 0);
	GLenum buffers[1] = {
		GL_COLOR_ATTACHMENT0
	};
	glDrawBuffers(1, buffers);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return;

	glx_input_render_texture(data);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	gs_leavecontext();
}

static void glx_input_video_render(void *vptr, effect_t effect)
{
	GLX_DATA(vptr);

	eparam_t image = effect_getparambyname(effect, "image");
	effect_settexture(effect, image, data->buffer);

	gs_enable_blending(false);

	gs_draw_sprite(data->buffer, 0, 0, 0);
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
