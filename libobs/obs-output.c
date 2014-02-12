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

#include "obs.h"
#include "obs-internal.h"

static inline const struct obs_output_info *find_output(const char *id)
{
	size_t i;
	for (i = 0; i < obs->output_types.num; i++)
		if (strcmp(obs->output_types.array[i].id, id) == 0)
			return obs->output_types.array+i;

	return NULL;
}

obs_output_t obs_output_create(const char *id, const char *name,
		obs_data_t settings)
{
	const struct obs_output_info *info = find_output(id);
	struct obs_output *output;

	if (!info) {
		blog(LOG_WARNING, "Output '%s' not found", id);
		return NULL;
	}

	output = bmalloc(sizeof(struct obs_output));
	output->info = *info;
	output->settings  = obs_data_newref(settings);
	output->data      = info->create(output->settings, output);

	if (!output->data) {
		obs_data_release(output->settings);
		bfree(output);
		return NULL;
	}

	output->name = bstrdup(name);

	pthread_mutex_lock(&obs->data.outputs_mutex);
	da_push_back(obs->data.outputs, &output);
	pthread_mutex_unlock(&obs->data.outputs_mutex);
	return output;
}

void obs_output_destroy(obs_output_t output)
{
	if (output) {
		if (output->info.active) {
			if (output->info.active(output->data))
				output->info.stop(output->data);
		}

		pthread_mutex_lock(&obs->data.outputs_mutex);
		da_erase_item(obs->data.outputs, &output);
		pthread_mutex_unlock(&obs->data.outputs_mutex);

		output->info.destroy(output->data);
		obs_data_release(output->settings);
		bfree(output->name);
		bfree(output);
	}
}

bool obs_output_start(obs_output_t output)
{
	return output->info.start(output->data);
}

void obs_output_stop(obs_output_t output)
{
	output->info.stop(output->data);
}

bool obs_output_active(obs_output_t output)
{
	return output->info.active(output);
}

obs_properties_t obs_output_properties(const char *id, const char *locale)
{
	const struct obs_output_info *info = find_output(id);
	if (info && info->properties)
		return info->properties(locale);
	return NULL;
}

void obs_output_update(obs_output_t output, obs_data_t settings)
{
	obs_data_replace(&output->settings, settings);

	if (output->info.update)
		output->info.update(output->data, output->settings);
}

obs_data_t obs_output_get_settings(obs_output_t output)
{
	if (!output)
		return NULL;

	obs_data_addref(output->settings);
	return output->settings;
}

bool obs_output_canpause(obs_output_t output)
{
	return output->info.pause != NULL;
}

void obs_output_pause(obs_output_t output)
{
	if (output->info.pause)
		output->info.pause(output->data);
}
