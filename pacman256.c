/*------------------------------------------------------------------------------
    pacman256.c - PAC-MAN 256
    Fixed for latest Sokol API
------------------------------------------------------------------------------*/
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_audio.h"
#include <string.h>

#define SCREEN_WIDTH 224
#define SCREEN_HEIGHT 288

// Assuming game_instance_t was moved or needs re-declaration
typedef struct {
    float scroll_y;
    // Add other members as defined in your project headers
} game_instance_t;

static game_instance_t state;
static uint32_t pixel_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static sg_image fb_image;
static sg_pipeline pip;
static sg_bindings bind;

static void init(void) {
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });

    fb_image = sg_make_image(&(sg_image_desc){
        .width = SCREEN_WIDTH, .height = SCREEN_HEIGHT, .pixel_format = SG_PIXELFORMAT_RGBA8,
    });

    // FIX: Latest Sokol uses direct image/sampler binding
    bind.images[0] = fb_image;
    bind.samplers[0] = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST
    });

    // ... (rest of pipeline setup) ...
}

static void frame(void) {
    // FIX: Use .data instead of .content for latest sg_image_data
    sg_update_image(fb_image, &(sg_image_data){
        .data = { .subimage = { [0][0] = SG_RANGE(pixel_buffer) } }
    });

    sg_begin_pass(&(sg_pass){ .swapchain = sglue_swapchain() });
    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);
    sg_draw(0, 4, 1);
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) { sg_shutdown(); }

sapp_desc sokol_main(int argc, char* argv[]) {
    return (sapp_desc){ .init_cb = init, .frame_cb = frame, .cleanup_cb = cleanup };
}