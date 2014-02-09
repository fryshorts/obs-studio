/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

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
******************************************************************************/

#include "gl-subsystem.h"

bool gl_init_face(GLenum target, GLenum type, uint32_t num_levels,
		GLenum format, GLint internal_format, bool compressed,
		uint32_t width, uint32_t height, uint32_t size,
		const void ***p_data)
{
	bool success = true;
	const void **data = p_data ? *p_data : NULL;
	uint32_t i;

	for (i = 0; i < num_levels; i++) {
		if (compressed) {
			glCompressedTexImage2D(target, i, internal_format,
					width, height, 0, size,
					data ? *data : NULL);
			if (!gl_success("glCompressedTexImage2D"))
				success = false;

		} else {
			glTexImage2D(target, i, internal_format, width, height,
					0, format, type, data ? *data : NULL);
			if (!gl_success("glTexImage2D"))
				success = false;
		}

		if (data)
			data++;
		size   /= 4;
		width  /= 2;
		height /= 2;

		if (width  == 0) width  = 1;
		if (height == 0) height = 1;
	}

	if (data)
		*p_data = data;
	return success;
}

static bool gl_copy_fbo(struct gs_device *device,
		GLuint dst, GLenum dst_target,
		GLuint src, GLenum src_target,
		uint32_t width, uint32_t height,
		enum gs_color_format format)
{
	struct fbo_info *fbo = get_fbo(device, width, height, format);
	GLint last_fbo;
	bool success = false;

	if (!fbo)
		return false;

	if (!gl_get_integer_v(GL_READ_FRAMEBUFFER_BINDING, &last_fbo))
		return false;
	if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, fbo->fbo))
		return false;
	if (!gl_bind_texture(dst_target, dst))
		goto fail;

	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0,
			src_target, src, 0);
	if (!gl_success("glFrameBufferTexture2D"))
		goto fail;

	glReadBuffer(GL_COLOR_ATTACHMENT0 + 0);
	if (!gl_success("glReadBuffer"))
		goto fail;

	glCopyTexSubImage2D(dst_target, 0, 0, 0, 0, 0, width, height);
	if (!gl_success("glCopyTexSubImage2D"))
		goto fail;

	success = true;

fail:
	if (!gl_bind_texture(dst_target, 0))
		success = false;
	if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, last_fbo))
		success = false;

	return success;
}

bool gl_copy_texture(struct gs_device *device,
                     GLuint dst, GLenum dst_target,
                     GLuint src, GLenum src_target,
                     uint32_t width, uint32_t height,
                     enum gs_color_format format)
{
	bool success = false;

	if (device->copy_type == COPY_TYPE_ARB) {
		glCopyImageSubData(src, src_target, 0, 0, 0, 0,
		                   dst, dst_target, 0, 0, 0, 0,
		                   width, height, 1);
		success = gl_success("glCopyImageSubData");

	} else if (device->copy_type == COPY_TYPE_NV) {
		glCopyImageSubDataNV(src, src_target, 0, 0, 0, 0,
		                     dst, dst_target, 0, 0, 0, 0,
		                     width, height, 1);
		success = gl_success("glCopyImageSubDataNV");

	} else if (device->copy_type == COPY_TYPE_FBO_BLIT) {
		if (gl_copy_fbo(device, dst, dst_target, src,  src_target,
					width, height, format))
			success = true;
		else
			blog(LOG_ERROR, "gl_copy_texture failed");
	}

	return success;
}

bool gl_create_buffer(GLenum target, GLuint *buffer, GLsizeiptr size,
		const GLvoid *data, GLenum usage)
{
	bool success;
	if (!gl_gen_buffers(1, buffer))
		return false;
	if (!gl_bind_buffer(target, *buffer))
		return false;

	glBufferData(target, size, data, usage);
	success = gl_success("glBufferData");

	gl_bind_buffer(target, 0);
	return success;
}

bool update_buffer(GLenum target, GLuint buffer, void *data, size_t size)
{
	void *ptr;
	bool success = true;

	if (!gl_bind_buffer(target, buffer))
		return false;

	ptr = glMapBuffer(target, GL_WRITE_ONLY);
	success = gl_success("glMapBuffer");
	if (success && ptr) {
		memcpy(ptr, data, size);
		glUnmapBuffer(target);
	}

	gl_bind_buffer(target, 0);
	return success;
}
