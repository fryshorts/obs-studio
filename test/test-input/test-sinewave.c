#include <math.h>
#include "test-sinewave.h"

/* middle C */
const double rate = 261.63/48000.0;

#define M_PI 3.1415926535897932384626433832795
#define M_PI_X2 M_PI*2

static void *sinewave_thread(void *pdata)
{
	struct sinewave_data *swd = pdata;
	uint64_t last_time = os_gettime_ns();
	uint64_t ts = 0;
	double cos_val = 0.0;
	uint8_t bytes[480];

	while (event_try(&swd->event) == EAGAIN) {
		if (!os_sleepto_ns(last_time += 10000000))
			last_time = os_gettime_ns();

		for (size_t i = 0; i < 480; i++) {
			cos_val += rate * M_PI_X2;
			if (cos_val > M_PI_X2)
				cos_val -= M_PI_X2;

			double wave = cos(cos_val);
			bytes[i] = (uint8_t)((wave+1.0)*0.5 * 255.0);
		}

		struct source_audio data;
		data.data[0] = bytes;
		data.frames = 480;
		data.speakers = SPEAKERS_MONO;
		data.samples_per_sec = 48000;
		data.timestamp = ts;
		data.format = AUDIO_FORMAT_U8BIT;
		obs_source_output_audio(swd->source, &data);

		ts += 10000000;
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */

const char *sinewave_getname(const char *locale)
{
	return "Sinewave Sound Source (Test)";
}

struct sinewave_data *sinewave_create(const char *settings, obs_source_t source)
{
	struct sinewave_data *swd = bzalloc(sizeof(struct sinewave_data));
	swd->source = source;

	if (event_init(&swd->event, EVENT_TYPE_MANUAL) != 0)
		goto fail;
	if (pthread_create(&swd->thread, NULL, sinewave_thread, swd) != 0)
		goto fail;

	swd->initialized_thread = true;
	return swd;

fail:
	sinewave_destroy(swd);
	return NULL;
}

void sinewave_destroy(struct sinewave_data *swd)
{
	if (swd) {
		if (swd->initialized_thread) {
			void *ret;
			event_signal(&swd->event);
			pthread_join(swd->thread, &ret);
		}

		event_destroy(&swd->event);
		bfree(swd);
	}
}

uint32_t sinewave_get_output_flags(struct sinewave_data *swd)
{
	return SOURCE_AUDIO;
}
