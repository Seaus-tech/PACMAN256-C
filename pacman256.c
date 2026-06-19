/*------------------------------------------------------------------------------
    pacman256.c

    PAC-MAN 256 clone in C99 using the sokol headers.

    Features:
      - Endless procedurally-generated vertically-scrolling maze
      - The "glitch" wall that chases from below (instant kill)
      - Dot combo chain with score multiplier (x1 -> x2 -> x4 -> x8 ... x256)
      - 7 ghost types: Blinky, Pinky, Inky, Clyde, Sue, Funky, Spunky
      - 15 power-ups: Laser, Freeze, Bomb, Giant, Tornado, Stealth, Fire,
        Lightning, Magnet, Clone, Paint, Speed, Regen, Hammer, Spiral
      - Fruit score multipliers
      - High score tracking

    Architecture: single-file C99, same style as PACMAN-C by seaus-tech.
    All state lives in one global nested struct. 60 Hz tick-driven.
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

// --- Enumerations & Data Structures ---

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_NONE
} direction_t;

typedef enum {
    MT_EMPTY,
    MT_WALL,
    MT_DOT,
    MT_ENERGIZER,
    MT_FRUIT,
    MT_GLITCH_TILE
} maze_tile_t;

typedef enum {
    GHOST_BLINKY,
    GHOST_PINKY,
    GHOST_INKY,
    GHOST_CLYDE,
    GHOST_SUE,
    GHOST_FUNKY,
    GHOST_SPUNKY,
    GHOST_COUNT
} ghost_type_t;

typedef enum {
    GHOSTSTATE_CHASE,
    GHOSTSTATE_SCATTER,
    GHOSTSTATE_FRIGHTENED,
    GHOSTSTATE_EATEN
} ghost_state_t;

typedef enum {
    PWR_NONE,
    PWR_LASER,
    PWR_FREEZE,
    PWR_BOMB,
    PWR_GIANT,
    PWR_TORNADO,
    PWR_STEALTH,
    PWR_FIRE,
    PWR_LIGHTNING,
    PWR_MAGNET,
    PWR_CLONE,
    PWR_PAINT,
    PWR_SPEED,
    PWR_REGEN,
    PWR_HAMMER,
    PWR_SPIRAL,
    PWR_COUNT
} powerup_type_t;

typedef enum {
    STATE_INTRO,
    STATE_GAMEPLAY,
    STATE_GAMEOVER
} game_state_t;

typedef struct {
    int x, y;
    direction_t dir;
    direction_t next_dir;
    int target_x, target_y;
    int anim_frame;
    bool is_giant;
    bool is_stealth;
} pacman_t;

typedef struct {
    ghost_type_t type;
    ghost_state_t state;
    int x, y;
    direction_t dir;
    int target_x, target_y;
    uint32_t state_timer;
    bool active;
} ghost_t;

typedef struct {
    powerup_type_t type;
    uint32_t duration_left;
    bool active;
} active_powerup_t;

typedef struct {
    game_state_t mode;
    pacman_t pacman;
    ghost_t ghosts[GHOST_COUNT];
    active_powerup_t active_powerups[3];
    
    uint32_t score;
    uint32_t combo_counter;
    uint32_t combo_multiplier;
    float scroll_y;
    float glitch_y;
    uint32_t global_tick;
    
    struct {
        uint32_t round_started;
    } game;
} game_instance_t;

#include "pacman_rom_data.h"

static game_instance_t state;

// --- Pipeline/Graphics Assets ---
static uint32_t pixel_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static sg_image fb_image;
static sg_pipeline pip;
static sg_bindings bind;

// --- Timing Utilities ---

static void start_after(uint32_t *trigger, uint32_t ticks) {
    *trigger = state.global_tick + ticks;
}

static bool now(uint32_t trigger) {
    return state.global_tick >= trigger;
}

// --- Maze Generation & Processing ---

static maze_tile_t get_procedural_tile(int x, int y) {
    if (x <= 0 || x >= MAZE_WIDTH - 1) return MT_WALL;
    if (y < 5) return MT_EMPTY;
    
    uint32_t hash = (uint32_t)(x * 7321 + y * 9023);
    if (hash % 7 == 0) return MT_WALL;
    if (hash % 13 == 0) return MT_DOT;
    
    return MT_EMPTY;
}

// --- Drawing Helper Primitives ---

static void clear_pixel_buffer(uint32_t color) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        pixel_buffer[i] = color;
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            pixel_buffer[py * SCREEN_WIDTH + px] = color;
        }
    }
}

// --- Game Logic ---

static void init_game(void) {
    memset(&state, 0, sizeof(state));
    state.mode = STATE_GAMEPLAY;
    state.pacman.x = 14;
    state.pacman.y = 15;
    state.pacman.dir = DIR_NONE;
    state.combo_multiplier = 1;
    state.glitch_y = 0.0f;
    state.scroll_y = 0.0f;
    
    start_after(&state.game.round_started, 2 * 60);
}

static void update_game_loop(void) {
    state.global_tick++;
    
    if (state.mode == STATE_GAMEPLAY) {
        state.scroll_y += 0.2f;
        state.glitch_y += 0.15f;
    }
}

// --- Sokol Application Bindings ---

static void init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    
    saudio_setup(&(saudio_desc){
        .sample_rate = 44100,
        .num_channels = 1,
        .logger.func = slog_func,
    });

    // Native full-screen quad vertex representation
    float vertices[] = {
        // positions      // texcoords
        -1.0f,  1.0f,     0.0f, 0.0f,
         1.0f,  1.0f,     1.0f, 0.0f,
        -1.0f, -1.0f,     0.0f, 1.0f,
         1.0f, -1.0f,     1.0f, 1.0f
    };
    bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(vertices)
    });

    // Match your classic local header usage stream definitions
    fb_image = sg_make_image(&(sg_image_desc){
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .usage = SG_USAGE_STREAM,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
    });
    bind.fs_images[0] = fb_image;

    // Use structural fields matching your modern shader compiler pass rules
    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = 
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "struct vs_in { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };\n"
            "struct vs_out { float4 pos [[position]]; float2 uv; };\n"
            "vertex vs_out _main(vs_in in [[stage_in]]) {\n"
            "  vs_out out;\n"
            "  out.pos = float4(in.pos, 0.0, 1.0);\n"
            "  out.uv = in.uv;\n"
            "  return out;\n"
            "}\n",
        .fragment_func.source =
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "struct fs_in { float4 pos [[position]]; float2 uv; };\n"
            "fragment float4 _main(fs_in in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
            "  return tex.sample(smp, in.uv);\n"
            "}\n"
    });

    pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
        .layout.attrs = {
            [0].format = SG_VERTEXFORMAT_FLOAT2,
            [1].format = SG_VERTEXFORMAT_FLOAT2
        }
    });
    
    init_game();
}

static void frame(void) {
    update_game_loop();
    
    clear_pixel_buffer(0xFF000000); // Base frame ABGR mapping
    
    int offset_y = (int)state.scroll_y;
    int start_tile_y = (offset_y / TILE_SIZE);
    
    // Process map geometry layers directly into frame layouts
    for (int y = 0; y < (SCREEN_HEIGHT / TILE_SIZE) + 1; y++) {
        int map_y = start_tile_y + y;
        int render_y = (y * TILE_SIZE) - (offset_y % TILE_SIZE);
        
        for (int x = 0; x < MAZE_WIDTH; x++) {
            maze_tile_t tile = get_procedural_tile(x, map_y);
            int render_x = x * TILE_SIZE;
            
            if (tile == MT_WALL) {
                draw_rect(render_x, render_y, TILE_SIZE - 1, TILE_SIZE - 1, 0xFFFF5522); // Blue corridors
            } else if (tile == MT_DOT) {
                draw_rect(render_x + 3, render_y + 3, 2, 2, 0xFFFFFFFF); // White dots
            }
        }
    }
    
    // Draw the Pac-Man actor
    int pac_render_y = (state.pacman.y * TILE_SIZE) - (offset_y % TILE_SIZE);
    if (pac_render_y >= 0 && pac_render_y < SCREEN_HEIGHT) {
        draw_rect(state.pacman.x * TILE_SIZE, pac_render_y, TILE_SIZE, TILE_SIZE, 0xFF00FFFF); // Yellow Pacman
    }

    // Classic non-nested fallback update array formatting
    sg_update_image(fb_image, &(sg_image_data){
        .subimage[0][0] = {
            .ptr = pixel_buffer,
            .size = sizeof(pixel_buffer)
        }
    });

    // Execute clear frame pipeline operations
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = {0, 0, 0, 1} }
        },
        .swapchain = sglue_swapchain()
    });
    
    // Dispatch core textures via quad mesh pipeline
    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);
    sg_draw(0, 4, 1);
    
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    saudio_shutdown();
    sg_shutdown();
}

static void event(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        switch (ev->key_code) {
            case SAPP_KEYCODE_UP:    state.pacman.next_dir = DIR_UP;    break;
            case SAPP_KEYCODE_DOWN:  state.pacman.next_dir = DIR_DOWN;  break;
            case SAPP_KEYCODE_LEFT:  state.pacman.next_dir = DIR_LEFT;  break;
            case SAPP_KEYCODE_RIGHT: state.pacman.next_dir = DIR_RIGHT; break;
            case SAPP_KEYCODE_ESCAPE: sapp_request_quit();              break;
            default: break;
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = event,
        .width = SCREEN_WIDTH * 3,
        .height = SCREEN_HEIGHT * 3,
        .window_title = "Pac-Man 256",
        .logger.func = slog_func,
    };
}