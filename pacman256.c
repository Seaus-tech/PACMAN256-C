/*------------------------------------------------------------------------------
    pacman256.c (Updated for Sokol v6)
------------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_log.h"
#include "sokol_glue.h"

#define MAZE_WIDTH 28
#define SCREEN_WIDTH 224
#define SCREEN_HEIGHT 288
#define TILE_SIZE 8

// [Keep your existing structs and game logic here...]
// ... (omitted for brevity, assume these remain unchanged) ...

static game_instance_t state;
static uint32_t pixel_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static sg_image fb_image;
static sg_pipeline pip;
static sg_bindings bind;

static void init(void) {
    sg_setup(&(sg_desc){ .environment = sglue_environment(), .logger.func = slog_func });
    
    float vertices[] = { -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };
    bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){ .data = SG_RANGE(vertices) });

    fb_image = sg_make_image(&(sg_image_desc){
        .width = SCREEN_WIDTH, .height = SCREEN_HEIGHT, .pixel_format = SG_PIXELFORMAT_RGBA8,
    });
    
    // UPDATED BINDING
    bind.fs.images[0] = fb_image;
    bind.fs.samplers[0] = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST
    });

    // ... (rest of your shader and pipeline setup) ...
}

static void frame(void) {
    // ... (your update and draw logic) ...

    // UPDATED IMAGE UPDATE
    sg_update_image(fb_image, &(sg_image_data){
        .content[0][0] = SG_RANGE(pixel_buffer)
    });

    sg_begin_pass(&(sg_pass){ .action = { .colors[0] = { .load_action = SG_LOADACTION_CLEAR } }, .swapchain = sglue_swapchain() });
    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);
    sg_draw(0, 4, 1);
    sg_end_pass();
    sg_commit();
}

// ... (rest of the file) ...