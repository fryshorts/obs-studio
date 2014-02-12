#include <stdio.h>
#include <time.h>

#include <functional>
#include <memory>

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#import <OpenGL/OpenGL.h>

#include <util/base.h>
#include <obs.h>

static const int cx = 800;
static const int cy = 600;



/* --------------------------------------------------- */

template <typename T, typename D_T, D_T D>
struct OBSUniqueHandle : std::unique_ptr<T, std::function<D_T>>
{
	using base = std::unique_ptr<T, std::function<D_T>>;
	explicit OBSUniqueHandle(T *obj) : base(obj, D) {}
	operator T*() { return base::get(); }
};

#define DECLARE_DELETER(x) decltype(x), x

using SourceContext = OBSUniqueHandle<obs_source,
      DECLARE_DELETER(obs_source_release)>;

using SceneContext = OBSUniqueHandle<obs_scene,
      DECLARE_DELETER(obs_scene_release)>;

#undef DECLARE_DELETER

/* --------------------------------------------------- */

static void CreateOBS(NSWindow *win)
{
	if (!obs_startup())
		throw "Couldn't create OBS";

	struct obs_video_info ovi;
	ovi.adapter         = 0;
	ovi.fps_num         = 30000;
	ovi.fps_den         = 1001;
	ovi.graphics_module = "libobs-opengl";
	ovi.output_format   = VIDEO_FORMAT_RGBA;
	ovi.base_width      = cx;
	ovi.base_height     = cy;
	ovi.output_width    = cx;
	ovi.output_height   = cy;
	ovi.window_width    = cx;
	ovi.window_height   = cy;
	ovi.window.view     = [win contentView];

	if (!obs_reset_video(&ovi))
		throw "Couldn't initialize video";
}

static void AddTestItems(obs_scene_t scene, obs_source_t source)
{
	obs_sceneitem_t item = NULL;
	struct vec2 v2;

	item = obs_scene_add(scene, source);
	vec2_set(&v2, 100.0f, 200.0f);
	obs_sceneitem_setpos(item, &v2);
	obs_sceneitem_setrot(item, 10.0f);
	vec2_set(&v2, 20.0f, 2.0f);
	obs_sceneitem_setscale(item, &v2);

	item = obs_scene_add(scene, source);
	vec2_set(&v2, 200.0f, 100.0f);
	obs_sceneitem_setpos(item, &v2);
	obs_sceneitem_setrot(item, -45.0f);
	vec2_set(&v2, 5.0f, 7.0f);
	obs_sceneitem_setscale(item, &v2);
}

@interface window_closer : NSObject {}
@end

@implementation window_closer

+(id)window_closer
{
	return [[window_closer alloc] init];
}

-(void)windowWillClose:(NSNotification *)notification
{
	[NSApp stop:self];
}
@end

static NSWindow *CreateTestWindow()
{
	[NSApplication sharedApplication];

	ProcessSerialNumber psn = {0, kCurrentProcess};
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"	
	SetFrontProcess(&psn);
#pragma clang diagnostic pop

	NSRect content_rect = NSMakeRect(0, 0, cx, cy);
	NSWindow *win = [[NSWindow alloc]
		initWithContentRect:content_rect
		styleMask:NSTitledWindowMask | NSClosableWindowMask
		backing:NSBackingStoreBuffered
		defer:NO];
	if(!win)
		return nil;

	[win orderFrontRegardless];

	NSView *view = [[NSView alloc] initWithFrame:content_rect];
	if(!view)
		return nil;


	[win setContentView:view];
	[win setTitle:@"foo"];
	[win center];
	[win makeMainWindow];
	[win setDelegate:[window_closer window_closer]];
	return win;
}

static void test()
{
	try {
		static NSWindow *win = CreateTestWindow();
		if (!win)
			throw "Couldn't create main window";

		CreateOBS(win);

		/* ------------------------------------------------------ */
		/* load module */
		if (obs_load_module("test-input") != 0)
			throw "Couldn't load module";

		/* ------------------------------------------------------ */
		/* create source */
		SourceContext source{obs_source_create(OBS_SOURCE_TYPE_INPUT,
				"random", "a test source", NULL)};
		if (!source)
			throw "Couldn't create random test source";

		/* ------------------------------------------------------ */
		/* create filter */
		SourceContext filter{obs_source_create(OBS_SOURCE_TYPE_FILTER,
				"test", "a test filter", NULL)};
		if (!filter)
			throw "Couldn't create test filter";
		obs_source_filter_add(source, filter);

		/* ------------------------------------------------------ */
		/* create scene and add source to scene (twice) */
		SceneContext scene{obs_scene_create("test scene")};
		if (!scene)
			throw "Couldn't create scene";

		AddTestItems(scene, source);

		/* ------------------------------------------------------ */
		/* set the scene as the primary draw source and go */
		obs_set_output_source(0, obs_scene_getsource(scene));

		[NSApp run];

		obs_set_output_source(0, NULL);

	} catch (char const *error) {
		printf("%s\n", error);
	}

	obs_shutdown();

	blog(LOG_INFO, "Number of memory leaks: %llu", bnum_allocs());
}

/* --------------------------------------------------- */

int main()
{
	@autoreleasepool {
		test();
	}

	return 0;
}
