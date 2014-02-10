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

static inline bool upload_texture_cube(struct gs_texture_cube *tex,
		const void **data)
{
	uint32_t row_size   = tex->size * gs_get_format_bpp(tex->base.format);
	uint32_t tex_size   = tex->size * row_size / 8;
	uint32_t num_levels = tex->base.levels;
	bool     compressed = gs_is_compressed_format(tex->base.format);
	GLenum   gl_type    = get_gl_format_type(tex->base.format);
	bool     success    = true;
	uint32_t i;

	if (!num_levels)
		num_levels = gs_num_total_levels(tex->size, tex->size);

	for (i = 0; i < 6; i++) {
		GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;

		if (!gl_bind_texture(target, tex->base.texture))
			success = false;

		if (!gl_init_face(target, gl_type, num_levels,
					tex->base.gl_format,
					tex->base.gl_internal_format,
					compressed, tex->size, tex->size,
					tex_size, &data))
			success = false;

		if (!gl_bind_texture(target, 0))
			success = false;

		if (data)
			data++;
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, num_levels);
	if (!gl_success("glTexParameteri"))
		success = false;

	return success;
}

texture_t device_create_cubetexture(device_t device, uint32_t size,
		enum gs_color_format color_format, uint32_t levels,
		const void **data, uint32_t flags)
{
	struct gs_texture_cube *tex = bzalloc(sizeof(struct gs_texture_cube));
	tex->base.device             = device;
	tex->base.type               = GS_TEXTURE_CUBE;
	tex->base.format             = color_format;
	tex->base.levels             = levels;
	tex->base.gl_format          = convert_gs_format(color_format);
	tex->base.gl_internal_format = convert_gs_internal_format(color_format);
	tex->base.gl_target          = GL_TEXTURE_CUBE_MAP;
	tex->base.is_render_target   = (flags & GS_RENDERTARGET) != 0;
	tex->base.gen_mipmaps        = (flags & GS_BUILDMIPMAPS) != 0;
	tex->size                    = size;

	if (!gl_gen_textures(1, &tex->base.texture))
		goto fail;
	if (!upload_texture_cube(tex, data))
		goto fail;

	return (texture_t)tex;

fail:
	cubetexture_destroy((texture_t)tex);
	blog(LOG_ERROR, "device_create_cubetexture (GL) failed");
	return NULL;
}

void cubetexture_destroy(texture_t tex)
{
	if (!tex)
		return;

	if (tex->texture) {
		glDeleteTextures(1, &tex->texture);
		gl_success("glDeleteTextures");
	}

	bfree(tex);
}

static inline bool is_texture_cube(texture_t tex, const char *func)
{
	bool is_texcube = tex->type == GS_TEXTURE_CUBE;
	if (!is_texcube)
		blog(LOG_ERROR, "%s (GL) failed:  Not a cubemap texture", func);
	return is_texcube;
}

uint32_t cubetexture_getsize(texture_t cubetex)
{
	struct gs_texture_cube *cube = (struct gs_texture_cube*)cubetex;
	if (!is_texture_cube(cubetex, "cubetexture_getsize"))
		return 0;

	return cube->size;
}

enum gs_color_format cubetexture_getcolorformat(texture_t cubetex)
{
	return cubetex->format;
}
