#include <stdio.h>
#include <time.h>
#include <windows.h>

#include <util/base.h>
#include <media-io/audio-resampler.h>
#include <obs.h>

#include <intrin.h>

static const int cx = 800;
static const int cy = 600;

/* --------------------------------------------------- */

class SourceContext {
	obs_source_t source;

public:
	inline SourceContext(obs_source_t source) : source(source) {}
	inline ~SourceContext() {obs_source_release(source);}
	inline operator obs_source_t() {return source;}
};

/* --------------------------------------------------- */

class SceneContext {
	obs_scene_t scene;

public:
	inline SceneContext(obs_scene_t scene) : scene(scene) {}
	inline ~SceneContext() {obs_scene_release(scene);}
	inline operator obs_scene_t() {return scene;}
};

/* --------------------------------------------------- */

static LRESULT CALLBACK sceneProc(HWND hwnd, UINT message, WPARAM wParam,
		LPARAM lParam)
{
	switch (message) {

	case WM_CLOSE:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return 0;
}

static void do_log(enum log_type type, const char *msg, va_list args)
{
	char bla[4096];
	vsnprintf(bla, 4095, msg, args);

	OutputDebugStringA(bla);
	OutputDebugStringA("\n");

	if (type >= LOG_WARNING)
		__debugbreak();
}

static void CreateOBS(HWND hwnd)
{
	RECT rc;
	GetClientRect(hwnd, &rc);

	if (!obs_startup())
		throw "Couldn't create OBS";

	struct obs_video_info ovi;
	ovi.adapter         = 0;
	ovi.base_width      = rc.right;
	ovi.base_height     = rc.bottom;
	ovi.fps_num         = 30000;
	ovi.fps_den         = 1001;
	ovi.graphics_module = "libobs-opengl";
	ovi.window_width    = rc.right;
	ovi.window_height   = rc.bottom;
	ovi.output_format   = VIDEO_FORMAT_RGBA;
	ovi.output_width    = rc.right;
	ovi.output_height   = rc.bottom;
	ovi.window.hwnd     = hwnd;

	if (!obs_reset_video(&ovi))
		throw "Couldn't initialize video";
}

static void AddTestItems(obs_scene_t scene, obs_source_t source)
{
	obs_sceneitem_t item = NULL;

	item = obs_scene_add(scene, source);
}

static HWND CreateTestWindow(HINSTANCE instance)
{
	WNDCLASS wc;

	memset(&wc, 0, sizeof(wc));
	wc.lpszClassName = TEXT("bla");
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.hInstance     = instance;
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.lpfnWndProc   = (WNDPROC)sceneProc;

	if (!RegisterClass(&wc))
		return 0;

	return CreateWindow(TEXT("bla"), TEXT("bla"),
			WS_OVERLAPPEDWINDOW|WS_VISIBLE,
			1920/2 - cx/2, 1080/2 - cy/2, cx, cy,
			NULL, NULL, instance, NULL);
}

/* --------------------------------------------------- */

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine,
		int numCmd)
{
	HWND hwnd = NULL;
	base_set_log_handler(do_log);

	try {
		hwnd = CreateTestWindow(instance);
		if (!hwnd)
			throw "Couldn't create main window";

		/* ------------------------------------------------------ */
		/* create OBS */
		CreateOBS(hwnd);

		/* ------------------------------------------------------ */
		/* load module */
		if (obs_load_module("test-input") != 0)
			throw "Couldn't load module";

		/* ------------------------------------------------------ */
		/* create source */
		SourceContext source = obs_source_create(OBS_SOURCE_TYPE_INPUT,
				"random", "some randon source", NULL);
		if (!source)
			throw "Couldn't create random test source";

		/* ------------------------------------------------------ */
		/* create filter */
		SourceContext filter = obs_source_create(OBS_SOURCE_TYPE_FILTER,
				"test", "a nice little green filter", NULL);
		if (!filter)
			throw "Couldn't create test filter";
		obs_source_filter_add(source, filter);

		/* ------------------------------------------------------ */
		/* create scene and add source to scene (twice) */
		SceneContext scene = obs_scene_create("test scene");
		if (!scene)
			throw "Couldn't create scene";

		AddTestItems(scene, source);

		/* ------------------------------------------------------ */
		/* set the scene as the primary draw source and go */
		obs_set_output_source(0, source);

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	} catch (char *error) {
		MessageBoxA(NULL, error, NULL, 0);
	}

	obs_shutdown();

	blog(LOG_INFO, "Number of memory leaks: %llu", bnum_allocs());
	DestroyWindow(hwnd);

	return 0;
}
