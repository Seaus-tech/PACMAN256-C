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

    Architecture: single-file C99, same style as pacman.c by floooh.
    All state lives in one global nested struct. 60 Hz tick-driven.
------------------------------------------------------------------------------*/
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "sokol_letterbox.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/*== CONFIG ==================================================================*/
#define AUDIO_VOLUME        (0.5f)
#define DBG_SKIP_INTRO      (0)
#define DBG_GODMODE         (0)
#define DBG_DOUBLE_SPEED    (0)

#if DBG_DOUBLE_SPEED
  #define TICK_DURATION_NS  (8333333)
#else
  #define TICK_DURATION_NS  (16666666)
#endif
#define TICK_TOLERANCE_NS   (1000000)

/*== CONSTANTS ===============================================================*/
#define NUM_VOICES          (3)
#define NUM_SOUNDS          (3)
#define NUM_SAMPLES         (128)
#define DISABLED_TICKS      (0xFFFFFFFF)

/* tile/sprite dimensions */
#define TILE_WIDTH          (8)
#define TILE_HEIGHT         (8)
#define SPRITE_WIDTH        (16)
#define SPRITE_HEIGHT       (16)

/* display: 28 tiles wide, 36 tiles tall (same as original) */
#define DISPLAY_TILES_X     (28)
#define DISPLAY_TILES_Y     (36)
#define DISPLAY_PIXELS_X    (DISPLAY_TILES_X * TILE_WIDTH)
#define DISPLAY_PIXELS_Y    (DISPLAY_TILES_Y * TILE_HEIGHT)

/* maze world is wider than screen for chunk generation */
#define MAZE_TILES_X        (28)
/* how many tile-rows we keep buffered above and below the viewport */
#define MAZE_BUFFER_ROWS    (72)

#define NUM_SPRITES         (16)   /* pacman + 7 ghosts + clone + effects */
#define NUM_DEBUG_MARKERS   (16)
#define TILE_TEXTURE_WIDTH  (256 * TILE_WIDTH)
#define TILE_TEXTURE_HEIGHT (TILE_HEIGHT + SPRITE_HEIGHT)
#define MAX_VERTICES        (((DISPLAY_TILES_X * DISPLAY_TILES_Y) + NUM_SPRITES + NUM_DEBUG_MARKERS) * 6)

/* timing */
#define FADE_TICKS          (30)
#define GAMEOVER_TICKS      (3*60)
#define GHOST_EATEN_FREEZE_TICKS (60)
#define PACMAN_EATEN_TICKS  (60)
#define PACMAN_DEATH_TICKS  (150)

/* game-specific */
#define NUM_GHOSTS          (7)    /* Blinky Pinky Inky Clyde Sue Funky Spunky */
#define MAX_COMBO           (256)  /* combo chain cap — the "256" */
#define GLITCH_START_Y      (32)   /* tile row where glitch starts (near bottom) */
#define GLITCH_SPEED_TICKS  (120)  /* ticks between glitch advancing one row */
#define SCROLL_SPEED_TICKS  (4)    /* ticks per 1-pixel upward scroll */

/* power-up slot count (player can carry 3 active power-ups) */
#define POWERUP_SLOTS       (3)

/* power-up durations in ticks */
#define PU_LASER_TICKS      (60)
#define PU_FREEZE_TICKS     (300)
#define PU_BOMB_RADIUS      (3)    /* in tiles */
#define PU_GIANT_TICKS      (300)
#define PU_TORNADO_TICKS    (240)
#define PU_STEALTH_TICKS    (300)
#define PU_FIRE_TICKS       (240)
#define PU_LIGHTNING_TICKS  (60)
#define PU_MAGNET_TICKS     (300)
#define PU_CLONE_TICKS      (300)
#define PU_PAINT_TICKS      (240)
#define PU_SPEED_TICKS      (300)
#define PU_REGEN_TICKS      (300)
#define PU_HAMMER_TICKS     (120)
#define PU_SPIRAL_TICKS     (180)

/*== TILE / COLOR / SPRITE CODES (reused from original ROM) ==================*/
enum {
    TILE_SPACE          = 0x40,
    TILE_DOT            = 0x10,
    TILE_PILL           = 0x14,
    TILE_DOOR           = 0xCF,
    TILE_GLITCH_BASE    = 0x40, /* glitch uses space tile with special color */

    /* fruit tiles */
    TILE_CHERRIES       = 0x90,
    TILE_STRAWBERRY     = 0x94,
    TILE_PEACH          = 0x98,
    TILE_BELL           = 0x9C,
    TILE_APPLE          = 0xA0,
    TILE_GRAPES         = 0xA4,
    TILE_GALAXIAN       = 0xA8,
    TILE_KEY            = 0xAC,

    /* power-up tile codes (repurpose some unused codes) */
    TILE_PU_LASER       = 0x16,
    TILE_PU_FREEZE      = 0x17,
    TILE_PU_BOMB        = 0x18,
    TILE_PU_GIANT       = 0x19,
    TILE_PU_TORNADO     = 0x1A,
    TILE_PU_STEALTH     = 0x1B,
    TILE_PU_FIRE        = 0x1C,
    TILE_PU_LIGHTNING   = 0x1D,
    TILE_PU_MAGNET      = 0x1E,
    TILE_PU_CLONE       = 0x1F,
    TILE_PU_PAINT       = 0x50,
    TILE_PU_SPEED       = 0x51,
    TILE_PU_REGEN       = 0x52,
    TILE_PU_HAMMER      = 0x53,
    TILE_PU_SPIRAL      = 0x54,

    SPRITETILE_INVISIBLE    = 30,
    SPRITETILE_SCORE_200    = 40,
    SPRITETILE_SCORE_400    = 41,
    SPRITETILE_SCORE_800    = 42,
    SPRITETILE_SCORE_1600   = 43,
    SPRITETILE_CHERRIES     = 0,
    SPRITETILE_STRAWBERRY   = 1,
    SPRITETILE_PEACH        = 2,
    SPRITETILE_BELL         = 3,
    SPRITETILE_APPLE        = 4,
    SPRITETILE_GRAPES       = 5,
    SPRITETILE_GALAXIAN     = 6,
    SPRITETILE_KEY          = 7,
    SPRITETILE_PACMAN_CLOSED_MOUTH = 48,

    COLOR_BLANK             = 0x00,
    COLOR_DEFAULT           = 0x0F,
    COLOR_DOT               = 0x10,
    COLOR_PACMAN            = 0x09,
    COLOR_BLINKY            = 0x01,
    COLOR_PINKY             = 0x03,
    COLOR_INKY              = 0x05,
    COLOR_CLYDE             = 0x07,
    COLOR_SUE               = 0x04,
    COLOR_FUNKY             = 0x06,
    COLOR_SPUNKY            = 0x08,
    COLOR_FRIGHTENED        = 0x11,
    COLOR_FRIGHTENED_BLINK  = 0x12,
    COLOR_GHOST_SCORE       = 0x18,
    COLOR_EYES              = 0x19,
    COLOR_GLITCH            = 0x1E, /* vivid glitch color */
    COLOR_FIRE_TRAIL        = 0x01,
    COLOR_PAINT_TRAIL       = 0x14,
    COLOR_GIANT_PACMAN      = 0x09,
    COLOR_WHITE_BORDER      = 0x1F,
    COLOR_FRUIT_SCORE       = 0x03,
};

/*== ENUMS ===================================================================*/
typedef enum {
    GAMESTATE_INTRO,
    GAMESTATE_GAME,
} gamestate_t;

typedef enum {
    DIR_RIGHT = 0,
    DIR_DOWN  = 1,
    DIR_LEFT  = 2,
    DIR_UP    = 3,
    NUM_DIRS  = 4,
} dir_t;

typedef enum {
    FRUIT_NONE,
    FRUIT_CHERRIES,
    FRUIT_STRAWBERRY,
    FRUIT_PEACH,
    FRUIT_APPLE,
    FRUIT_GRAPES,
    FRUIT_GALAXIAN,
    FRUIT_BELL,
    FRUIT_KEY,
    NUM_FRUITS,
} fruit_t;

typedef enum {
    GHOSTTYPE_BLINKY = 0,
    GHOSTTYPE_PINKY,
    GHOSTTYPE_INKY,
    GHOSTTYPE_CLYDE,
    GHOSTTYPE_SUE,
    GHOSTTYPE_FUNKY,
    GHOSTTYPE_SPUNKY,
} ghosttype_t;

typedef enum {
    GHOSTSTATE_NONE,
    GHOSTSTATE_CHASE,
    GHOSTSTATE_SCATTER,
    GHOSTSTATE_FRIGHTENED,
    GHOSTSTATE_EYES,       /* returning to spawn */
    GHOSTSTATE_SPAWNING,   /* briefly frozen at spawn before entering maze */
    GHOSTSTATE_FROZEN,     /* freeze power-up */
} ghoststate_t;

typedef enum {
    FREEZETYPE_INTRO     = (1<<0),
    FREEZETYPE_READY     = (1<<1),
    FREEZETYPE_EAT_GHOST = (1<<2),
    FREEZETYPE_DEAD      = (1<<3),
} freezetype_t;

/* all 15 power-up types */
typedef enum {
    PU_NONE = 0,
    PU_LASER,
    PU_FREEZE,
    PU_BOMB,
    PU_GIANT,
    PU_TORNADO,
    PU_STEALTH,
    PU_FIRE,
    PU_LIGHTNING,
    PU_MAGNET,
    PU_CLONE,
    PU_PAINT,
    PU_SPEED,
    PU_REGEN,
    PU_HAMMER,
    PU_SPIRAL,
    NUM_POWERUP_TYPES,
} powerup_type_t;

typedef enum {
    SPRITE_PACMAN   = 0,
    SPRITE_BLINKY   = 1,
    SPRITE_PINKY    = 2,
    SPRITE_INKY     = 3,
    SPRITE_CLYDE    = 4,
    SPRITE_SUE      = 5,
    SPRITE_FUNKY    = 6,
    SPRITE_SPUNKY   = 7,
    SPRITE_CLONE    = 8,   /* clone decoy sprite */
    SPRITE_TORNADO  = 9,   /* tornado effect sprite */
    SPRITE_EFFECT0  = 10,  /* generic effect sprites */
    SPRITE_EFFECT1  = 11,
    SPRITE_EFFECT2  = 12,
    SPRITE_EFFECT3  = 13,
    SPRITE_FRUIT    = 14,
    SPRITE_PU_ICON  = 15,  /* power-up pickup icon */
} sprite_index_t;

/*== DATA STRUCTURES =========================================================*/

typedef struct { uint32_t tick; } trigger_t;
typedef struct { int16_t x, y; } int2_t;

typedef struct {
    dir_t    dir;
    int2_t   pos;        /* pixel-space, world coordinates */
    uint32_t anim_tick;
} actor_t;

/* one active power-up in the player's inventory */
typedef struct {
    powerup_type_t type;
    trigger_t      activated; /* when it was activated (DISABLED if not active) */
    bool           active;
} active_powerup_t;

/* a power-up item sitting in the maze */
typedef struct {
    powerup_type_t type;
    int2_t         tile_pos; /* world tile coordinates */
    bool           present;
} maze_powerup_t;

#define MAX_MAZE_POWERUPS (8)

typedef struct {
    actor_t      actor;
    ghosttype_t  type;
    dir_t        next_dir;
    int2_t       target_pos;  /* world tile coords */
    ghoststate_t state;
    trigger_t    frightened;
    trigger_t    frozen;
    trigger_t    eaten;
    bool         alive;
    bool         spawned;
    trigger_t    spawn_timer;
} ghost_t;

typedef struct {
    actor_t actor;
    bool    is_giant;         /* giant power-up active */
    bool    is_stealthed;
    trigger_t giant_timer;
    trigger_t stealth_timer;
    trigger_t speed_timer;
    trigger_t fire_timer;     /* fire trail active */
    trigger_t paint_timer;    /* paint trail active */
    trigger_t magnet_timer;
    trigger_t clone_timer;
    trigger_t regen_timer;
} pacman_t;

typedef struct {
    int2_t  pos;       /* world tile pos of tornado */
    dir_t   dir;
    bool    active;
    trigger_t timer;
} tornado_t;

typedef struct {
    int2_t pos;        /* world tile pos of clone */
    dir_t  dir;
    bool   active;
    trigger_t timer;
} clone_t;

/* fire/paint trail cell */
typedef struct {
    int2_t    tile_pos;
    bool      active;
    trigger_t timer;
} trail_cell_t;

#define MAX_TRAIL_CELLS (64)

/* vertex for rendering */
typedef struct {
    float    x, y, u, v;
    uint32_t attr;
} vertex_t;

typedef struct {
    bool    enabled;
    uint8_t tile, color;
    bool    flipx, flipy;
    int2_t  pos;   /* screen-space pixel position */
} sprite_t;

typedef struct {
    bool    enabled;
    uint8_t tile, color;
    int2_t  tile_pos;
} debugmarker_t;

typedef void (*sound_func_t)(int slot);
typedef struct {
    sound_func_t     func;
    const uint32_t*  ptr;
    uint32_t         size;
    bool             voice[3];
} sound_desc_t;

typedef struct {
    uint32_t counter;
    uint32_t frequency;
    uint8_t  waveform;
    uint8_t  volume;
    float    sample_acc;
    float    sample_div;
} voice_t;

typedef enum {
    SOUNDFLAG_VOICE0 = (1<<0),
    SOUNDFLAG_VOICE1 = (1<<1),
    SOUNDFLAG_VOICE2 = (1<<2),
    SOUNDFLAG_ALL    = (1<<0)|(1<<1)|(1<<2),
} soundflag_t;

typedef struct {
    uint32_t         cur_tick;
    sound_func_t     func;
    uint32_t         num_ticks;
    uint32_t         stride;
    const uint32_t*  data;
    uint8_t          flags;
} sound_t;

/*== MAZE CHUNK SYSTEM =======================================================*/
/*
   The maze is represented as a ring-buffer of tile rows.
   World Y grows upward as the game scrolls. The viewport shows
   DISPLAY_TILES_Y rows at a time. New rows are procedurally generated
   and appended at the top as old ones scroll off the bottom.

   Tile storage: maze_tiles[row % MAZE_BUFFER_ROWS][col]
   World tile row 0 is the starting row.
*/
#define MAZE_ROW(world_row) ((world_row) % MAZE_BUFFER_ROWS)

/* tile codes in maze buffer */
#define MT_WALL   0x01
#define MT_DOT    0x02
#define MT_PILL   0x03
#define MT_SPACE  0x00
#define MT_PU     0x04   /* power-up item (type stored separately) */
#define MT_FRUIT  0x05

/*== GLOBAL STATE ============================================================*/
static struct {

    gamestate_t gamestate;

    struct {
        uint32_t tick;
        uint64_t laptime_store;
        int32_t  tick_accum;
    } timing;

    struct {
        trigger_t started;
        int       frame;   /* animation frame for intro */
    } intro;

    struct {
        uint32_t  xorshift;
        uint32_t  hiscore;
        trigger_t started;
        trigger_t ready_started;
        trigger_t round_started;
        trigger_t game_over;
        trigger_t dot_eaten;
        trigger_t pill_eaten;
        trigger_t ghost_eaten;
        trigger_t pacman_eaten;
        trigger_t fruit_eaten;

        uint8_t   freeze;          /* FREEZETYPE_* flags */
        uint32_t  score;
        uint32_t  dots_eaten_total;

        /* combo chain */
        uint32_t  combo;           /* current consecutive dot streak */
        uint32_t  combo_multiplier;/* 1/2/4/8/.../256 */
        trigger_t combo_timeout;   /* resets combo if no dot eaten in time */

        /* glitch */
        int32_t   glitch_world_row;  /* current bottom row of glitch */
        trigger_t glitch_advance;    /* ticks until glitch moves up one row */

        /* scroll */
        int32_t   scroll_pixel;      /* how many pixels scrolled (world pixels) */
        trigger_t scroll_tick;       /* next pixel scroll */
        int32_t   viewport_world_row;/* top tile row currently visible */

        /* maze generation */
        int32_t   gen_world_row;     /* highest generated world row so far */
        uint32_t  maze_gen_seed;

        /* tile buffers */
        uint8_t   maze[MAZE_BUFFER_ROWS][MAZE_TILES_X]; /* MT_* codes */
        uint8_t   maze_powerup_type[MAZE_BUFFER_ROWS][MAZE_TILES_X]; /* powerup_type_t */

        /* actors */
        ghost_t   ghost[NUM_GHOSTS];
        pacman_t  pacman;
        int8_t    num_ghosts_eaten;  /* ghosts eaten in current pill */

        /* power-up inventory */
        active_powerup_t inventory[POWERUP_SLOTS];

        /* maze power-up pickups */
        maze_powerup_t  maze_pu[MAX_MAZE_POWERUPS];

        /* active effects */
        tornado_t tornado;
        clone_t   clone;
        trail_cell_t fire_trail[MAX_TRAIL_CELLS];
        trail_cell_t paint_trail[MAX_TRAIL_CELLS];

        /* fruit */
        fruit_t   active_fruit;
        int2_t    fruit_tile_pos;
        trigger_t fruit_timer;

        /* regen state */
        bool regen_active;
        trigger_t regen_timer;

        /* hammer / spiral effect timers */
        trigger_t hammer_timer;
        trigger_t spiral_timer;

    } game;

    struct {
        bool enabled;
        bool up, down, left, right;
        bool action;   /* use power-up / confirm */
        bool slot1, slot2, slot3; /* activate power-up slot */
        bool esc;
        bool anykey;
    } input;

    struct {
        voice_t   voice[NUM_VOICES];
        sound_t   sound[NUM_SOUNDS];
        int32_t   voice_tick_accum;
        int32_t   voice_tick_period;
        int32_t   sample_duration_ns;
        int32_t   sample_accum;
        uint32_t  num_samples;
        float     sample_buffer[NUM_SAMPLES];
    } audio;

    struct {
        trigger_t fadein;
        trigger_t fadeout;
        uint8_t   fade;

        /* screen-space tile buffers (what actually gets rendered) */
        uint8_t video_ram[DISPLAY_TILES_Y][DISPLAY_TILES_X];
        uint8_t color_ram[DISPLAY_TILES_Y][DISPLAY_TILES_X];

        sprite_t      sprite[NUM_SPRITES];
        debugmarker_t debug_marker[NUM_DEBUG_MARKERS];

        /* sokol resources */
        struct { sg_image img; sg_view tex_view; } tilerom;
        struct { sg_image img; sg_view tex_view; } palette;
        struct { sg_image img; sg_view tex_view; } render;
        sg_sampler linear_smp;
        sg_sampler nearest_smp;
        struct { sg_pass pass; sg_buffer vbuf; sg_pipeline pip; } offscreen;
        struct { sg_pass_action pass_action; sg_buffer quad_vbuf; sg_pipeline pip; } display;

        int      num_vertices;
        vertex_t vertices[MAX_VERTICES];

        uint8_t  tile_pixels[TILE_TEXTURE_HEIGHT][TILE_TEXTURE_WIDTH];
        uint32_t color_palette[256];

        /* sub-pixel scroll offset for smooth scrolling (0..TILE_HEIGHT-1) */
        int32_t scroll_offset_px;
    } gfx;

} state;

/*== ROM DATA (reused from original Pac-Man ROM dumps) =======================*/
/* Tile ROM, sprite ROM, color palette, and sound wavetable data.
   These are the same 256-byte / 4KB blobs used in pacman.c.
   We include them verbatim from the original project via a shared header. */
#include "pacman_rom_data.h"

/*== FORWARD DECLARATIONS ====================================================*/
static void game_init(void);
static void game_tick(void);
static void intro_tick(void);
static void gfx_create_resources(void);
static void gfx_init(void);
static void gfx_shutdown(void);
static void gfx_draw(void);
static void gfx_fade(void);
static void snd_init(void);
static void snd_shutdown(void);
static void snd_frame(int32_t frame_time_ns);
static void snd_tick(void);
static void snd_clear(void);
static void snd_start(int slot, const sound_desc_t* desc);
static void snd_stop(int slot);
static void maze_generate_rows(int32_t up_to_world_row);
static void maze_blit_to_screen(void);

/*== UTILITY =================================================================*/
static uint32_t xorshift32(void) {
    uint32_t x = state.game.xorshift;
    if (x == 0) x = 0x12345678;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state.game.xorshift = x;
    return x;
}

static void start(trigger_t* t)                    { t->tick = state.timing.tick; }
static void start_after(trigger_t* t, uint32_t n)  { t->tick = state.timing.tick + n; }
static void disable(trigger_t* t)                  { t->tick = DISABLED_TICKS; }
static trigger_t disabled_timer(void)              { return (trigger_t){ DISABLED_TICKS }; }
static bool now(trigger_t t)                       { return t.tick == state.timing.tick; }

static uint32_t since(trigger_t t) {
    if (state.timing.tick >= t.tick) return state.timing.tick - t.tick;
    return DISABLED_TICKS;
}
static bool after_once(trigger_t t, uint32_t n)    { return since(t) == n; }
static bool after(trigger_t t, uint32_t n)         { uint32_t s = since(t); return (s != DISABLED_TICKS) && (s >= n); }
static bool before(trigger_t t, uint32_t n)        { uint32_t s = since(t); return (s != DISABLED_TICKS) && (s < n); }
static bool between(trigger_t t, uint32_t a, uint32_t b) {
    uint32_t s = since(t);
    return (s != DISABLED_TICKS) && (s >= a) && (s < b);
}
static bool is_active(trigger_t t) { return t.tick != DISABLED_TICKS; }

static int2_t i2(int16_t x, int16_t y)             { return (int2_t){x, y}; }
static int2_t add_i2(int2_t a, int2_t b)            { return i2(a.x+b.x, a.y+b.y); }
static int2_t sub_i2(int2_t a, int2_t b)            { return i2(a.x-b.x, a.y-b.y); }
static bool   equal_i2(int2_t a, int2_t b)          { return (a.x==b.x) && (a.y==b.y); }
static int32_t sq_dist_i2(int2_t a, int2_t b) {
    int32_t dx = a.x-b.x, dy = a.y-b.y;
    return dx*dx + dy*dy;
}
static int2_t dir_to_vec(dir_t d) {
    static const int2_t v[4] = {{1,0},{0,1},{-1,0},{0,-1}};
    return v[d];
}
static dir_t reverse_dir(dir_t d) { return (dir_t)((d + 2) & 3); }

/*== COORDINATE HELPERS ======================================================*/
/* Convert world pixel Y to viewport screen pixel Y */
static int32_t world_to_screen_y(int32_t world_py) {
    return world_py - state.game.scroll_pixel;
}

/* Convert world tile position to screen tile position */
static int2_t world_tile_to_screen(int2_t wt) {
    int32_t screen_y = wt.y - state.game.viewport_world_row;
    return i2(wt.x, (int16_t)screen_y);
}

/* Convert pixel pos to world tile pos */
static int2_t pixel_to_world_tile(int2_t px) {
    return i2((int16_t)(px.x / TILE_WIDTH), (int16_t)(px.y / TILE_HEIGHT));
}

/* World tile bounds check */
static bool valid_world_tile(int2_t wt) {
    return (wt.x >= 0) && (wt.x < MAZE_TILES_X) && (wt.y >= 0);
}

/* Read a maze tile (MT_* code) at world tile pos */
static uint8_t maze_get(int2_t wt) {
    if (!valid_world_tile(wt)) return MT_WALL;
    return state.game.maze[MAZE_ROW(wt.y)][wt.x];
}

static void maze_set(int2_t wt, uint8_t code) {
    if (!valid_world_tile(wt)) return;
    state.game.maze[MAZE_ROW(wt.y)][wt.x] = code;
}

static bool is_wall(int2_t wt)  { return maze_get(wt) == MT_WALL; }
static bool is_dot_tile(int2_t wt)  { return maze_get(wt) == MT_DOT; }
static bool is_pill_tile(int2_t wt) { return maze_get(wt) == MT_PILL; }
static bool is_passable(int2_t wt)  { return !is_wall(wt); }

/* wrap x coordinate (tunnels left<->right) */
static int16_t wrap_x(int16_t x) {
    if (x < 0) return (int16_t)(MAZE_TILES_X - 1);
    if (x >= MAZE_TILES_X) return 0;
    return x;
}

/* Actor movement helper */
static bool can_move_world(int2_t px, dir_t dir) {
    int2_t vec = dir_to_vec(dir);
    int2_t next_px = add_i2(px, i2((int16_t)(vec.x * TILE_WIDTH / 2),
                                   (int16_t)(vec.y * TILE_HEIGHT / 2)));
    int2_t tile = pixel_to_world_tile(next_px);
    tile.x = wrap_x(tile.x);
    return is_passable(tile);
}

/*== PROCEDURAL MAZE GENERATION ==============================================*/
/*
   The maze is generated in horizontal strips of rows.
   We use a simple cellular-automaton / corridor approach:
     - Most rows are corridors with walls on edges and occasional pillars
     - Every ~8 rows we place a full wall row with one or two gaps
     - Dots fill open spaces; pills appear rarely; power-ups very rarely
     - Glitch corrupted tiles appear in the lowest rows
*/

#define MAZE_WALL_ROW_INTERVAL  8
#define DOT_CHANCE              70   /* % of open spaces get a dot */
#define PILL_CHANCE             3    /* % of open spaces get a pill */
#define PU_SPAWN_INTERVAL       25   /* power-up appears every N rows */

static void maze_generate_row(int32_t world_row) {
    uint8_t* row = state.game.maze[MAZE_ROW(world_row)];
    uint8_t* pu_row = state.game.maze_powerup_type[MAZE_ROW(world_row)];

    /* clear */
    for (int x = 0; x < MAZE_TILES_X; x++) {
        row[x] = MT_SPACE;
        pu_row[x] = (uint8_t)PU_NONE;
    }

    /* outer walls always present */
    row[0] = MT_WALL;
    row[MAZE_TILES_X - 1] = MT_WALL;

    bool is_wall_row = ((world_row % MAZE_WALL_ROW_INTERVAL) == 0) && (world_row > 0);

    if (is_wall_row) {
        /* fill entire row with walls, then punch 1-2 gaps */
        for (int x = 1; x < MAZE_TILES_X - 1; x++) row[x] = MT_WALL;
        int gap1 = 1 + (int)(xorshift32() % (MAZE_TILES_X - 4));
        int gap2 = 1 + (int)(xorshift32() % (MAZE_TILES_X - 4));
        row[gap1] = MT_SPACE;
        row[gap2] = MT_SPACE;
        if (gap1 > 1) row[gap1-1] = MT_SPACE; /* make gap 2 wide */
        if (gap2 > 1) row[gap2-1] = MT_SPACE;
    } else {
        /* corridor row: place some interior pillars randomly */
        for (int x = 1; x < MAZE_TILES_X - 1; x++) {
            if ((xorshift32() % 100) < 8) {
                row[x] = MT_WALL;
            }
        }
        /* ensure contiguous horizontal passability - simple flood check */
        /* just guarantee a clear central corridor column 13 and 14 */
        row[13] = MT_SPACE;
        row[14] = MT_SPACE;
    }

    /* fill open spaces with dots / pills */
    for (int x = 1; x < MAZE_TILES_X - 1; x++) {
        if (row[x] == MT_SPACE) {
            uint32_t r = xorshift32() % 100;
            if (r < PILL_CHANCE) {
                row[x] = MT_PILL;
            } else if (r < PILL_CHANCE + DOT_CHANCE) {
                row[x] = MT_DOT;
            }
        }
    }

    /* occasionally spawn a power-up item */
    if ((world_row % PU_SPAWN_INTERVAL) == 0 && world_row > 0) {
        /* find a random open column */
        for (int attempt = 0; attempt < 16; attempt++) {
            int x = 1 + (int)(xorshift32() % (MAZE_TILES_X - 2));
            if (row[x] != MT_WALL) {
                row[x] = MT_PU;
                /* pick a random power-up type (1..NUM_POWERUP_TYPES-1) */
                pu_row[x] = (uint8_t)(1 + (xorshift32() % (NUM_POWERUP_TYPES - 1)));
                break;
            }
        }
    }

    /* occasionally spawn a fruit */
    if ((world_row % 40) == 20 && world_row > 0) {
        for (int attempt = 0; attempt < 16; attempt++) {
            int x = 1 + (int)(xorshift32() % (MAZE_TILES_X - 2));
            if (row[x] != MT_WALL) {
                row[x] = MT_FRUIT;
                break;
            }
        }
    }
}

static void maze_generate_rows(int32_t up_to_world_row) {
    while (state.game.gen_world_row < up_to_world_row) {
        state.game.gen_world_row++;
        maze_generate_row(state.game.gen_world_row);
    }
}

/* Pre-generate the initial set of rows */
static void maze_init(void) {
    state.game.gen_world_row = -1;
    state.game.xorshift = state.game.maze_gen_seed;
    /* Generate enough rows to fill the screen plus some lookahead */
    maze_generate_rows(DISPLAY_TILES_Y + 16);
}

/*== SCREEN BLIT =============================================================*/
/*
   Convert maze world tiles visible in the viewport to video_ram / color_ram
   for the renderer. Also renders the glitch rows.
*/
static uint8_t maze_code_to_tile(uint8_t mt, int x, int world_row) {
    switch (mt) {
        case MT_DOT:   return TILE_DOT;
        case MT_PILL:  return TILE_PILL;
        case MT_WALL: {
            /* Simple wall tile selection based on neighbor config */
            (void)x; (void)world_row;
            return 0x26; /* generic wall tile from ROM */
        }
        case MT_PU:    return TILE_PU_LASER; /* placeholder; overridden by type */
        case MT_FRUIT: return TILE_CHERRIES;
        default:       return TILE_SPACE;
    }
}

static uint8_t maze_code_to_color(uint8_t mt, int x, int world_row) {
    (void)x; (void)world_row;
    switch (mt) {
        case MT_DOT:   return COLOR_DOT;
        case MT_PILL:  return COLOR_DOT;
        case MT_WALL:  return COLOR_DEFAULT;
        case MT_PU:    return COLOR_PACMAN;
        case MT_FRUIT: return COLOR_CHERRIES;
        default:       return COLOR_BLANK;
    }
}

#define COLOR_CHERRIES 0x14

static void maze_blit_to_screen(void) {
    int32_t vr = state.game.viewport_world_row;
    for (int sy = 0; sy < DISPLAY_TILES_Y; sy++) {
        int32_t wy = vr + sy;
        for (int x = 0; x < MAZE_TILES_X; x++) {
            /* check if this row is inside the glitch zone */
            if (wy <= state.game.glitch_world_row) {
                /* render glitch corruption */
                uint8_t glyph = (uint8_t)(xorshift32() & 0xFF);
                state.gfx.video_ram[sy][x] = glyph;
                state.gfx.color_ram[sy][x] = COLOR_GLITCH;
            } else {
                uint8_t mt = maze_get(i2((int16_t)x, (int16_t)wy));
                uint8_t tile  = maze_code_to_tile(mt, x, wy);
                uint8_t color = maze_code_to_color(mt, x, wy);

                /* power-up tile override */
                if (mt == MT_PU) {
                    uint8_t pu_t = state.game.maze_powerup_type[MAZE_ROW(wy)][x];
                    static const uint8_t pu_tiles[NUM_POWERUP_TYPES] = {
                        TILE_SPACE,
                        TILE_PU_LASER,   TILE_PU_FREEZE,  TILE_PU_BOMB,
                        TILE_PU_GIANT,   TILE_PU_TORNADO, TILE_PU_STEALTH,
                        TILE_PU_FIRE,    TILE_PU_LIGHTNING,TILE_PU_MAGNET,
                        TILE_PU_CLONE,   TILE_PU_PAINT,   TILE_PU_SPEED,
                        TILE_PU_REGEN,   TILE_PU_HAMMER,  TILE_PU_SPIRAL,
                    };
                    if (pu_t < NUM_POWERUP_TYPES) tile = pu_tiles[pu_t];
                }

                state.gfx.video_ram[sy][x] = tile;
                state.gfx.color_ram[sy][x] = color;
            }
        }
    }
}

/*== INPUT ===================================================================*/
static void input_disable(void) { state.input.enabled = false; }
static void input_enable(void)  { state.input.enabled = true; }

static dir_t input_dir(dir_t def) {
    if (!state.input.enabled) return def;
    if (state.input.up)    return DIR_UP;
    if (state.input.down)  return DIR_DOWN;
    if (state.input.left)  return DIR_LEFT;
    if (state.input.right) return DIR_RIGHT;
    return def;
}

/*== COMBO CHAIN =============================================================*/
#define COMBO_TIMEOUT_TICKS (90)  /* ~1.5 seconds without eating a dot resets combo */

static uint32_t combo_to_multiplier(uint32_t combo) {
    if (combo <   8) return 1;
    if (combo <  16) return 2;
    if (combo <  32) return 4;
    if (combo <  64) return 8;
    if (combo < 128) return 16;
    if (combo < 256) return 32;
    return 64; /* MAX */
}

static void combo_eat_dot(void) {
    start(&state.game.combo_timeout);
    state.game.combo++;
    if (state.game.combo > MAX_COMBO) state.game.combo = MAX_COMBO;
    state.game.combo_multiplier = combo_to_multiplier(state.game.combo);
}

static void combo_tick(void) {
    if (is_active(state.game.combo_timeout)) {
        if (after(state.game.combo_timeout, COMBO_TIMEOUT_TICKS)) {
            state.game.combo = 0;
            state.game.combo_multiplier = 1;
            disable(&state.game.combo_timeout);
        }
    }
}

/*== SCROLLING ===============================================================*/
static void scroll_tick(void) {
    if (state.game.freeze) return;
    if (after_once(state.game.scroll_tick, SCROLL_SPEED_TICKS)) {
        start(&state.game.scroll_tick);
        state.game.scroll_pixel++;
        state.gfx.scroll_offset_px = state.game.scroll_pixel % TILE_HEIGHT;
        state.game.viewport_world_row = state.game.scroll_pixel / TILE_HEIGHT;
        /* ensure we have enough rows generated ahead */
        maze_generate_rows(state.game.viewport_world_row + DISPLAY_TILES_Y + 16);
    }
}

/*== GLITCH ==================================================================*/
static void glitch_tick(void) {
    if (after_once(state.game.glitch_advance, GLITCH_SPEED_TICKS)) {
        start(&state.game.glitch_advance);
        state.game.glitch_world_row++;
    }
}

static bool pacman_in_glitch(void) {
    int2_t pt = pixel_to_world_tile(state.game.pacman.actor.pos);
    return (pt.y <= state.game.glitch_world_row);
}

/*== POWER-UP INVENTORY ======================================================*/
static void inventory_add(powerup_type_t type) {
    /* find an empty slot */
    for (int i = 0; i < POWERUP_SLOTS; i++) {
        if (state.game.inventory[i].type == PU_NONE) {
            state.game.inventory[i].type = type;
            state.game.inventory[i].active = false;
            disable(&state.game.inventory[i].activated);
            return;
        }
    }
    /* no empty slot: replace slot 0 (oldest) */
    state.game.inventory[0] = state.game.inventory[1];
    state.game.inventory[1] = state.game.inventory[2];
    state.game.inventory[2].type = type;
    state.game.inventory[2].active = false;
    disable(&state.game.inventory[2].activated);
}

static bool inventory_slot_expired(int slot) {
    active_powerup_t* pu = &state.game.inventory[slot];
    if (!pu->active) return false;
    static const uint32_t durations[NUM_POWERUP_TYPES] = {
        0,
        PU_LASER_TICKS, PU_FREEZE_TICKS, 1, PU_GIANT_TICKS,
        PU_TORNADO_TICKS, PU_STEALTH_TICKS, PU_FIRE_TICKS, PU_LIGHTNING_TICKS,
        PU_MAGNET_TICKS, PU_CLONE_TICKS, PU_PAINT_TICKS, PU_SPEED_TICKS,
        PU_REGEN_TICKS, PU_HAMMER_TICKS, PU_SPIRAL_TICKS,
    };
    uint32_t dur = durations[pu->type];
    return after(pu->activated, dur);
}

/*== POWER-UP ACTIVATION EFFECTS =============================================*/

static void powerup_activate_laser(void) {
    /* Fire laser in pacman's current direction — kill all ghosts in line */
    dir_t d = state.game.pacman.actor.dir;
    int2_t vec = dir_to_vec(d);
    int2_t pt = pixel_to_world_tile(state.game.pacman.actor.pos);
    for (int step = 1; step < 32; step++) {
        int2_t tp = i2((int16_t)(pt.x + vec.x * step),
                       (int16_t)(pt.y + vec.y * step));
        if (is_wall(tp)) break;
        /* kill any ghost on this tile */
        for (int g = 0; g < NUM_GHOSTS; g++) {
            ghost_t* ghost = &state.game.ghost[g];
            if (!ghost->alive) continue;
            int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
            if (equal_i2(gpt, tp)) {
                ghost->state = GHOSTSTATE_EYES;
                start(&ghost->eaten);
                state.game.score += 20 * state.game.combo_multiplier;
            }
        }
    }
}

static void powerup_activate_freeze(void) {
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        ghost->state = GHOSTSTATE_FROZEN;
        start(&ghost->frozen);
    }
}

static void powerup_activate_bomb(void) {
    int2_t pt = pixel_to_world_tile(state.game.pacman.actor.pos);
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
        if (sq_dist_i2(pt, gpt) <= (PU_BOMB_RADIUS * PU_BOMB_RADIUS)) {
            ghost->state = GHOSTSTATE_EYES;
            start(&ghost->eaten);
            state.game.score += 20 * state.game.combo_multiplier;
        }
    }
}

static void powerup_activate_giant(void) {
    state.game.pacman.is_giant = true;
    start(&state.game.pacman.giant_timer);
}

static void powerup_activate_tornado(void) {
    state.game.tornado.active = true;
    state.game.tornado.pos = pixel_to_world_tile(state.game.pacman.actor.pos);
    state.game.tornado.dir = state.game.pacman.actor.dir;
    start(&state.game.tornado.timer);
}

static void powerup_activate_stealth(void) {
    state.game.pacman.is_stealthed = true;
    start(&state.game.pacman.stealth_timer);
}

static void powerup_activate_fire(void) {
    start(&state.game.pacman.fire_timer);
}

static void powerup_activate_lightning(void) {
    /* Chain lightning: find nearest ghost, jump to adjacent ones */
    int2_t origin = pixel_to_world_tile(state.game.pacman.actor.pos);
    bool hit[NUM_GHOSTS] = {false};
    /* find nearest */
    int nearest = -1;
    int32_t min_dist = 99999;
    for (int g = 0; g < NUM_GHOSTS; g++) {
        if (!state.game.ghost[g].alive) continue;
        int32_t d = sq_dist_i2(origin, pixel_to_world_tile(state.game.ghost[g].actor.pos));
        if (d < min_dist) { min_dist = d; nearest = g; }
    }
    if (nearest < 0) return;
    hit[nearest] = true;
    state.game.ghost[nearest].state = GHOSTSTATE_EYES;
    start(&state.game.ghost[nearest].eaten);
    state.game.score += 20 * state.game.combo_multiplier;
    /* chain up to 3 more */
    int2_t last_pos = pixel_to_world_tile(state.game.ghost[nearest].actor.pos);
    for (int chain = 0; chain < 3; chain++) {
        int next = -1;
        int32_t nd = 99999;
        for (int g = 0; g < NUM_GHOSTS; g++) {
            if (!state.game.ghost[g].alive || hit[g]) continue;
            int32_t d = sq_dist_i2(last_pos, pixel_to_world_tile(state.game.ghost[g].actor.pos));
            if (d < nd && d < 64) { nd = d; next = g; }
        }
        if (next < 0) break;
        hit[next] = true;
        state.game.ghost[next].state = GHOSTSTATE_EYES;
        start(&state.game.ghost[next].eaten);
        state.game.score += 10 * state.game.combo_multiplier;
        last_pos = pixel_to_world_tile(state.game.ghost[next].actor.pos);
    }
}

static void powerup_activate_magnet(void) {
    start(&state.game.pacman.magnet_timer);
}

static void powerup_activate_clone(void) {
    state.game.clone.active = true;
    state.game.clone.pos = pixel_to_world_tile(state.game.pacman.actor.pos);
    state.game.clone.dir = reverse_dir(state.game.pacman.actor.dir);
    start(&state.game.clone.timer);
    start(&state.game.pacman.clone_timer);
}

static void powerup_activate_paint(void) {
    start(&state.game.pacman.paint_timer);
}

static void powerup_activate_speed(void) {
    start(&state.game.pacman.speed_timer);
}

static void powerup_activate_regen(void) {
    state.game.regen_active = true;
    start(&state.game.regen_timer);
}

static void powerup_activate_hammer(void) {
    start(&state.game.hammer_timer);
    /* smash all visible ghosts downward - mark them as eaten */
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        ghost->state = GHOSTSTATE_EYES;
        start(&ghost->eaten);
        state.game.score += 20 * state.game.combo_multiplier;
    }
}

static void powerup_activate_spiral(void) {
    start(&state.game.spiral_timer);
    /* spiral pushes all ghosts away from pacman */
    int2_t origin = pixel_to_world_tile(state.game.pacman.actor.pos);
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
        if (sq_dist_i2(origin, gpt) < 100) {
            ghost->state = GHOSTSTATE_FRIGHTENED;
            start(&ghost->frightened);
        }
    }
}

static void powerup_activate(int slot) {
    active_powerup_t* pu = &state.game.inventory[slot];
    if (pu->type == PU_NONE || pu->active) return;
    pu->active = true;
    start(&pu->activated);
    switch (pu->type) {
        case PU_LASER:     powerup_activate_laser();     pu->active = false; pu->type = PU_NONE; break;
        case PU_FREEZE:    powerup_activate_freeze();    break;
        case PU_BOMB:      powerup_activate_bomb();      pu->active = false; pu->type = PU_NONE; break;
        case PU_GIANT:     powerup_activate_giant();     break;
        case PU_TORNADO:   powerup_activate_tornado();   break;
        case PU_STEALTH:   powerup_activate_stealth();   break;
        case PU_FIRE:      powerup_activate_fire();      break;
        case PU_LIGHTNING: powerup_activate_lightning(); pu->active = false; pu->type = PU_NONE; break;
        case PU_MAGNET:    powerup_activate_magnet();    break;
        case PU_CLONE:     powerup_activate_clone();     break;
        case PU_PAINT:     powerup_activate_paint();     break;
        case PU_SPEED:     powerup_activate_speed();     break;
        case PU_REGEN:     powerup_activate_regen();     break;
        case PU_HAMMER:    powerup_activate_hammer();    pu->active = false; pu->type = PU_NONE; break;
        case PU_SPIRAL:    powerup_activate_spiral();    break;
        default: break;
    }
}

/*== POWER-UP TICK (ongoing effects) =========================================*/

static void powerup_tick_frozen_ghosts(void) {
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (ghost->state == GHOSTSTATE_FROZEN) {
            if (after(ghost->frozen, PU_FREEZE_TICKS)) {
                ghost->state = GHOSTSTATE_CHASE;
                disable(&ghost->frozen);
            }
        }
    }
}

static void powerup_tick_giant(void) {
    if (!state.game.pacman.is_giant) return;
    if (after(state.game.pacman.giant_timer, PU_GIANT_TICKS)) {
        state.game.pacman.is_giant = false;
        disable(&state.game.pacman.giant_timer);
    }
}

static void powerup_tick_stealth(void) {
    if (!state.game.pacman.is_stealthed) return;
    if (after(state.game.pacman.stealth_timer, PU_STEALTH_TICKS)) {
        state.game.pacman.is_stealthed = false;
        disable(&state.game.pacman.stealth_timer);
    }
}

static void powerup_tick_speed(void) {
    if (!is_active(state.game.pacman.speed_timer)) return;
    if (after(state.game.pacman.speed_timer, PU_SPEED_TICKS)) {
        disable(&state.game.pacman.speed_timer);
    }
}

static void powerup_tick_fire(void) {
    if (!is_active(state.game.pacman.fire_timer)) return;
    if (after(state.game.pacman.fire_timer, PU_FIRE_TICKS)) {
        disable(&state.game.pacman.fire_timer);
    }
    /* check fire trail cells: kill ghosts that walk onto them */
    for (int i = 0; i < MAX_TRAIL_CELLS; i++) {
        trail_cell_t* c = &state.game.fire_trail[i];
        if (!c->active) continue;
        if (after(c->timer, PU_FIRE_TICKS)) { c->active = false; continue; }
        for (int g = 0; g < NUM_GHOSTS; g++) {
            ghost_t* ghost = &state.game.ghost[g];
            if (!ghost->alive) continue;
            int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
            if (equal_i2(gpt, c->tile_pos)) {
                ghost->state = GHOSTSTATE_EYES;
                start(&ghost->eaten);
                state.game.score += 20 * state.game.combo_multiplier;
            }
        }
    }
}

static void powerup_tick_magnet(void) {
    if (!is_active(state.game.pacman.magnet_timer)) return;
    if (after(state.game.pacman.magnet_timer, PU_MAGNET_TICKS)) {
        disable(&state.game.pacman.magnet_timer);
        return;
    }
    /* eat all dots within 3 tiles of pacman */
    int2_t pp = pixel_to_world_tile(state.game.pacman.actor.pos);
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int2_t tp = i2((int16_t)(pp.x + dx), (int16_t)(pp.y + dy));
            if (!valid_world_tile(tp)) continue;
            uint8_t mt = maze_get(tp);
            if (mt == MT_DOT || mt == MT_PILL) {
                maze_set(tp, MT_SPACE);
                state.game.score += (mt == MT_PILL ? 5 : 1) * state.game.combo_multiplier;
                state.game.dots_eaten_total++;
                combo_eat_dot();
            }
        }
    }
}

static void powerup_tick_tornado(void) {
    if (!state.game.tornado.active) return;
    if (after(state.game.tornado.timer, PU_TORNADO_TICKS)) {
        state.game.tornado.active = false;
        return;
    }
    /* move tornado every 8 ticks, targeting nearest ghost */
    if ((state.timing.tick & 7) != 0) return;
    int2_t tp = state.game.tornado.pos;
    /* find nearest ghost */
    int nearest = -1;
    int32_t min_d = 99999;
    for (int g = 0; g < NUM_GHOSTS; g++) {
        if (!state.game.ghost[g].alive) continue;
        int32_t d = sq_dist_i2(tp, pixel_to_world_tile(state.game.ghost[g].actor.pos));
        if (d < min_d) { min_d = d; nearest = g; }
    }
    if (nearest >= 0) {
        int2_t target = pixel_to_world_tile(state.game.ghost[nearest].actor.pos);
        /* step toward target */
        int16_t dx = (target.x > tp.x) ? 1 : (target.x < tp.x) ? -1 : 0;
        int16_t dy = (target.y > tp.y) ? 1 : (target.y < tp.y) ? -1 : 0;
        state.game.tornado.pos = i2((int16_t)(tp.x + dx), (int16_t)(tp.y + dy));
        /* kill any ghost on tornado tile */
        for (int g = 0; g < NUM_GHOSTS; g++) {
            ghost_t* ghost = &state.game.ghost[g];
            if (!ghost->alive) continue;
            if (equal_i2(pixel_to_world_tile(ghost->actor.pos), state.game.tornado.pos)) {
                ghost->state = GHOSTSTATE_EYES;
                start(&ghost->eaten);
                state.game.score += 20 * state.game.combo_multiplier;
            }
        }
    }
}

static void powerup_tick_regen(void) {
    if (!state.game.regen_active) return;
    if (after(state.game.regen_timer, PU_REGEN_TICKS)) {
        state.game.regen_active = false;
        return;
    }
    /* Every 10 ticks, respawn a dot near pacman in an empty space */
    if ((state.timing.tick % 10) != 0) return;
    int2_t pp = pixel_to_world_tile(state.game.pacman.actor.pos);
    for (int attempt = 0; attempt < 8; attempt++) {
        int dx = (int)(xorshift32() % 7) - 3;
        int dy = (int)(xorshift32() % 7) - 3;
        int2_t tp = i2((int16_t)(pp.x + dx), (int16_t)(pp.y + dy));
        if (valid_world_tile(tp) && maze_get(tp) == MT_SPACE) {
            maze_set(tp, MT_DOT);
            break;
        }
    }
}

static void powerup_tick_paint(void) {
    if (!is_active(state.game.pacman.paint_timer)) return;
    if (after(state.game.pacman.paint_timer, PU_PAINT_TICKS)) {
        disable(&state.game.pacman.paint_timer);
        return;
    }
    /* paint trail: mark pacman's current tile as a dot if it was empty */
    int2_t pp = pixel_to_world_tile(state.game.pacman.actor.pos);
    if (valid_world_tile(pp) && maze_get(pp) == MT_SPACE) {
        maze_set(pp, MT_DOT);
    }
}

static void powerup_tick_clone(void) {
    if (!state.game.clone.active) return;
    if (after(state.game.clone.timer, PU_CLONE_TICKS)) {
        state.game.clone.active = false;
        return;
    }
    /* move clone in its direction every tick, randomly change direction */
    if ((state.timing.tick & 15) == 0) {
        state.game.clone.dir = (dir_t)(xorshift32() % NUM_DIRS);
    }
    /* ghosts that touch clone: get frightened briefly */
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
        if (equal_i2(gpt, state.game.clone.pos)) {
            ghost->state = GHOSTSTATE_FRIGHTENED;
            start(&ghost->frightened);
        }
    }
}

static void powerup_tick_all(void) {
    powerup_tick_frozen_ghosts();
    powerup_tick_giant();
    powerup_tick_stealth();
    powerup_tick_speed();
    powerup_tick_fire();
    powerup_tick_magnet();
    powerup_tick_tornado();
    powerup_tick_regen();
    powerup_tick_paint();
    powerup_tick_clone();
    /* expire inventory slots */
    for (int i = 0; i < POWERUP_SLOTS; i++) {
        if (inventory_slot_expired(i)) {
            state.game.inventory[i].active = false;
            state.game.inventory[i].type = PU_NONE;
            disable(&state.game.inventory[i].activated);
        }
    }
}

/*== GHOST AI ================================================================*/

/* scatter corner targets (in world tile space, relative to spawn area) */
static const int2_t ghost_scatter_targets[NUM_GHOSTS] = {
    { 25, 4 }, { 2, 4 }, { 27, 8 }, { 0, 8 }, { 25, 0 }, { 2, 0 }, { 13, 2 }
};

static int2_t ghost_pacman_tile(void) {
    return pixel_to_world_tile(state.game.pacman.actor.pos);
}

/* Per-ghost target computation (classic Pac-Man AI + new ghosts) */
static int2_t ghost_chase_target(ghost_t* ghost) {
    int2_t pac = ghost_pacman_tile();
    int2_t pac_dir_vec = dir_to_vec(state.game.pacman.actor.dir);
    switch (ghost->type) {
        case GHOSTTYPE_BLINKY:
            return pac;  /* directly chase */
        case GHOSTTYPE_PINKY: {
            /* 4 tiles ahead of pacman */
            return i2((int16_t)(pac.x + pac_dir_vec.x * 4),
                      (int16_t)(pac.y + pac_dir_vec.y * 4));
        }
        case GHOSTTYPE_INKY: {
            /* vector from Blinky to 2 tiles ahead of pacman, doubled */
            int2_t two_ahead = i2((int16_t)(pac.x + pac_dir_vec.x * 2),
                                  (int16_t)(pac.y + pac_dir_vec.y * 2));
            int2_t blinky_tile = pixel_to_world_tile(state.game.ghost[GHOSTTYPE_BLINKY].actor.pos);
            return i2((int16_t)(two_ahead.x + (two_ahead.x - blinky_tile.x)),
                      (int16_t)(two_ahead.y + (two_ahead.y - blinky_tile.y)));
        }
        case GHOSTTYPE_CLYDE: {
            /* chase if far, scatter if close */
            int32_t d = sq_dist_i2(pixel_to_world_tile(ghost->actor.pos), pac);
            if (d > 64) return pac;
            return ghost_scatter_targets[GHOSTTYPE_CLYDE];
        }
        case GHOSTTYPE_SUE: {
            /* Sue: ambushes from left side, targets 4 tiles left of pacman */
            return i2((int16_t)(pac.x - 4), pac.y);
        }
        case GHOSTTYPE_FUNKY: {
            /* Funky: mirrors Blinky but with vertical offset */
            return i2(pac.x, (int16_t)(pac.y + 3));
        }
        case GHOSTTYPE_SPUNKY: {
            /* Spunky: random walk toward pacman */
            if ((state.timing.tick & 15) == 0) {
                return i2((int16_t)(pac.x + (int16_t)(xorshift32() % 5) - 2),
                          (int16_t)(pac.y + (int16_t)(xorshift32() % 5) - 2));
            }
            return ghost->target_pos;
        }
        default:
            return pac;
    }
}

static ghoststate_t ghost_scatter_or_chase(void) {
    /* simple alternating scatter/chase based on time since round start */
    uint32_t t = since(state.game.round_started);
    if (t < 7*60)  return GHOSTSTATE_SCATTER;
    if (t < 27*60) return GHOSTSTATE_CHASE;
    if (t < 34*60) return GHOSTSTATE_SCATTER;
    if (t < 54*60) return GHOSTSTATE_CHASE;
    if (t < 59*60) return GHOSTSTATE_SCATTER;
    return GHOSTSTATE_CHASE;
}

static void ghost_update_state(ghost_t* ghost) {
    if (!ghost->alive) return;
    if (ghost->state == GHOSTSTATE_SPAWNING) {
        if (after(ghost->spawn_timer, 60)) ghost->state = GHOSTSTATE_SCATTER;
        return;
    }
    if (ghost->state == GHOSTSTATE_FROZEN) return;  /* handled by powerup tick */
    if (ghost->state == GHOSTSTATE_EYES) {
        /* respawn after brief delay */
        if (after(ghost->eaten, 5*60)) {
            ghost->state = GHOSTSTATE_SPAWNING;
            start(&ghost->spawn_timer);
            /* reset to a spawn position just above the current viewport */
            int16_t sx = (int16_t)(1 + (int)(xorshift32() % (MAZE_TILES_X - 2)));
            int16_t sy = (int16_t)(state.game.viewport_world_row + DISPLAY_TILES_Y - 2);
            ghost->actor.pos = i2((int16_t)(sx * TILE_WIDTH + TILE_WIDTH/2),
                                  (int16_t)(sy * TILE_HEIGHT + TILE_HEIGHT/2));
        }
        return;
    }
    if (ghost->state == GHOSTSTATE_FRIGHTENED) {
        if (after(ghost->frightened, 6*60)) {
            ghost->state = ghost_scatter_or_chase();
            disable(&ghost->frightened);
        }
        return;
    }
    ghost->state = ghost_scatter_or_chase();
}

static void ghost_update_target(ghost_t* ghost) {
    if (!ghost->alive) return;
    switch (ghost->state) {
        case GHOSTSTATE_CHASE:
            ghost->target_pos = ghost_chase_target(ghost);
            break;
        case GHOSTSTATE_SCATTER:
            ghost->target_pos = ghost_scatter_targets[ghost->type];
            /* offset scatter target by viewport so it stays relevant */
            ghost->target_pos.y += (int16_t)state.game.viewport_world_row;
            break;
        case GHOSTSTATE_FRIGHTENED:
            ghost->target_pos = i2((int16_t)(xorshift32() % MAZE_TILES_X),
                                   (int16_t)(state.game.viewport_world_row + xorshift32() % DISPLAY_TILES_Y));
            break;
        default:
            break;
    }
}

static dir_t ghost_choose_dir(ghost_t* ghost) {
    int2_t cur_tile = pixel_to_world_tile(ghost->actor.pos);
    dir_t  rev = reverse_dir(ghost->actor.dir);
    dir_t  best_dir = ghost->actor.dir;
    int32_t best_dist = 0x7FFFFFFF;
    for (int d = 0; d < NUM_DIRS; d++) {
        if ((dir_t)d == rev) continue; /* no 180-turn */
        int2_t vec = dir_to_vec((dir_t)d);
        int2_t next = i2((int16_t)(cur_tile.x + vec.x), (int16_t)(cur_tile.y + vec.y));
        next.x = wrap_x(next.x);
        if (is_wall(next)) continue;
        int32_t dist = sq_dist_i2(next, ghost->target_pos);
        if (dist < best_dist) { best_dist = dist; best_dir = (dir_t)d; }
    }
    return best_dir;
}

static void ghost_move(ghost_t* ghost) {
    if (!ghost->alive) return;
    if (ghost->state == GHOSTSTATE_SPAWNING ||
        ghost->state == GHOSTSTATE_FROZEN  ||
        ghost->state == GHOSTSTATE_NONE) return;

    /* speed: frightened = half speed, eyes = double, normal = every other tick */
    int move_this_tick = 0;
    switch (ghost->state) {
        case GHOSTSTATE_FRIGHTENED: move_this_tick = !(state.timing.tick & 1); break;
        case GHOSTSTATE_EYES:       move_this_tick = 1; break;
        default:                    move_this_tick = !(state.timing.tick & 1); break;
    }
    if (!move_this_tick) return;

    /* at tile center, choose new direction */
    int2_t pos = ghost->actor.pos;
    bool at_center = ((pos.x % TILE_WIDTH) == TILE_WIDTH/2) &&
                     ((pos.y % TILE_HEIGHT) == TILE_HEIGHT/2);
    if (at_center) {
        ghost->actor.dir = ghost_choose_dir(ghost);
    }

    int2_t vec = dir_to_vec(ghost->actor.dir);
    int2_t new_pos = add_i2(pos, i2(vec.x, vec.y));
    new_pos.x = (int16_t)((new_pos.x + DISPLAY_PIXELS_X) % DISPLAY_PIXELS_X);
    int2_t new_tile = pixel_to_world_tile(new_pos);
    new_tile.x = wrap_x(new_tile.x);
    if (!is_wall(new_tile)) {
        ghost->actor.pos = new_pos;
        ghost->actor.anim_tick++;
    }
}

/*== PACMAN MOVEMENT =========================================================*/

static bool pacman_should_move(void) {
    if (state.game.freeze) return false;
    if (before(state.game.pill_eaten, 3)) return false;
    return true;
}

static int pacman_speed(void) {
    /* speed power-up: move every tick; normal: every other tick */
    if (is_active(state.game.pacman.speed_timer) &&
        !after(state.game.pacman.speed_timer, PU_SPEED_TICKS)) return 1;
    return !(state.timing.tick & 1);
}

static void pacman_move(void) {
    if (!pacman_should_move()) return;
    if (!pacman_speed()) return;

    actor_t* actor = &state.game.pacman.actor;
    dir_t wanted = input_dir(actor->dir);
    if (can_move_world(actor->pos, wanted)) actor->dir = wanted;
    if (can_move_world(actor->pos, actor->dir)) {
        int2_t vec = dir_to_vec(actor->dir);
        actor->pos = add_i2(actor->pos, i2(vec.x, vec.y));
        actor->pos.x = (int16_t)((actor->pos.x + DISPLAY_PIXELS_X) % DISPLAY_PIXELS_X);
        actor->anim_tick++;
    }
}

/*== EATING LOGIC ============================================================*/

static void pacman_eat_dot(int2_t tile_pos) {
    maze_set(tile_pos, MT_SPACE);
    state.game.score += 1 * state.game.combo_multiplier;
    state.game.dots_eaten_total++;
    start(&state.game.dot_eaten);
    combo_eat_dot();
}

static void pacman_eat_pill(int2_t tile_pos) {
    maze_set(tile_pos, MT_SPACE);
    state.game.score += 5 * state.game.combo_multiplier;
    start(&state.game.pill_eaten);
    state.game.num_ghosts_eaten = 0;
    /* frighten all alive ghosts */
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        if (ghost->state == GHOSTSTATE_EYES || ghost->state == GHOSTSTATE_SPAWNING) continue;
        ghost->state = GHOSTSTATE_FRIGHTENED;
        start(&ghost->frightened);
    }
}

static void pacman_eat_powerup(int2_t tile_pos) {
    uint8_t pu_type = state.game.maze_powerup_type[MAZE_ROW(tile_pos.y)][tile_pos.x];
    maze_set(tile_pos, MT_SPACE);
    state.game.maze_powerup_type[MAZE_ROW(tile_pos.y)][tile_pos.x] = (uint8_t)PU_NONE;
    if (pu_type > 0 && pu_type < NUM_POWERUP_TYPES) {
        inventory_add((powerup_type_t)pu_type);
    }
}

static void pacman_eat_fruit(int2_t tile_pos) {
    maze_set(tile_pos, MT_SPACE);
    /* fruit boosts multiplier temporarily */
    state.game.combo_multiplier *= 2;
    if (state.game.combo_multiplier > 64) state.game.combo_multiplier = 64;
    state.game.score += 10 * state.game.combo_multiplier;
    start(&state.game.fruit_eaten);
}

static void pacman_eat_ghost(ghost_t* ghost) {
    ghost->state = GHOSTSTATE_EYES;
    start(&ghost->eaten);
    start(&state.game.ghost_eaten);
    state.game.num_ghosts_eaten++;
    uint32_t pts = 20 * (uint32_t)(1 << state.game.num_ghosts_eaten) * state.game.combo_multiplier;
    state.game.score += pts;
    state.game.freeze |= FREEZETYPE_EAT_GHOST;
}

static void pacman_check_collisions(void) {
    int2_t pac_tile = pixel_to_world_tile(state.game.pacman.actor.pos);

    /* eat tile contents */
    uint8_t mt = maze_get(pac_tile);
    switch (mt) {
        case MT_DOT:   pacman_eat_dot(pac_tile);   break;
        case MT_PILL:  pacman_eat_pill(pac_tile);  break;
        case MT_PU:    pacman_eat_powerup(pac_tile); break;
        case MT_FRUIT: pacman_eat_fruit(pac_tile); break;
        default: break;
    }

    /* fire trail: place a trail cell */
    if (is_active(state.game.pacman.fire_timer) &&
        !after(state.game.pacman.fire_timer, PU_FIRE_TICKS)) {
        for (int i = 0; i < MAX_TRAIL_CELLS; i++) {
            if (!state.game.fire_trail[i].active) {
                state.game.fire_trail[i].active = true;
                state.game.fire_trail[i].tile_pos = pac_tile;
                start(&state.game.fire_trail[i].timer);
                break;
            }
        }
    }

    /* ghost collisions */
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        if (!ghost->alive) continue;
        int2_t gpt = pixel_to_world_tile(ghost->actor.pos);
        if (!equal_i2(pac_tile, gpt)) continue;

        if (state.game.pacman.is_stealthed) continue; /* stealth: ignore */

        if (state.game.pacman.is_giant) {
            /* giant: crush ghost */
            pacman_eat_ghost(ghost);
        } else if (ghost->state == GHOSTSTATE_FRIGHTENED) {
            pacman_eat_ghost(ghost);
        } else if (ghost->state == GHOSTSTATE_CHASE || ghost->state == GHOSTSTATE_SCATTER) {
            /* pacman dies */
            #if !DBG_GODMODE
            if (!(state.game.freeze & FREEZETYPE_DEAD)) {
                start(&state.game.pacman_eaten);
                state.game.freeze |= FREEZETYPE_DEAD;
                start_after(&state.game.game_over, PACMAN_EATEN_TICKS + PACMAN_DEATH_TICKS);
                input_disable();
            }
            #endif
        }
    }

    /* glitch kills pacman */
    if (pacman_in_glitch()) {
        #if !DBG_GODMODE
        if (!(state.game.freeze & FREEZETYPE_DEAD)) {
            start(&state.game.pacman_eaten);
            state.game.freeze |= FREEZETYPE_DEAD;
            start_after(&state.game.game_over, PACMAN_EATEN_TICKS + PACMAN_DEATH_TICKS);
            input_disable();
        }
        #endif
    }
}

/*== SPRITE RENDERING ========================================================*/

static sprite_t* spr(int idx) { return &state.gfx.sprite[idx]; }

static void spr_clear(void) {
    for (int i = 0; i < NUM_SPRITES; i++) {
        state.gfx.sprite[i].enabled = false;
        state.gfx.sprite[i].tile  = SPRITETILE_INVISIBLE;
    }
}

/* World pixel pos -> screen pixel pos */
static int2_t world_px_to_screen(int2_t world_px) {
    int32_t sy = world_px.y - state.game.scroll_pixel;
    return i2(world_px.x, (int16_t)sy);
}

static void spr_anim_pacman(dir_t dir, uint32_t tick) {
    static const uint8_t tiles[4][4] = {
        { 44, 46, 48, 46 }, /* right */
        { 45, 47, 48, 47 }, /* down */
        { 44, 46, 48, 46 }, /* left (flipx) */
        { 45, 47, 48, 47 }, /* up */
    };
    sprite_t* s = spr(SPRITE_PACMAN);
    uint32_t phase = (tick / 2) & 3;
    s->tile  = tiles[dir][phase];
    s->color = COLOR_PACMAN;
    s->flipx = (dir == DIR_LEFT);
    s->flipy = (dir == DIR_UP);
    if (state.game.pacman.is_giant) {
        /* giant: doubled size — we use a 2-tile offset hack visually */
        s->color = COLOR_GIANT_PACMAN;
    }
}

static void spr_anim_ghost(int g_idx, ghosttype_t gtype, dir_t dir, uint32_t tick) {
    static const uint8_t tiles[4][2] = {{32,33},{34,35},{36,37},{38,39}};
    static const uint8_t ghost_colors[NUM_GHOSTS] = {
        COLOR_BLINKY, COLOR_PINKY, COLOR_INKY, COLOR_CLYDE,
        COLOR_SUE, COLOR_FUNKY, COLOR_SPUNKY
    };
    sprite_t* s = spr(SPRITE_BLINKY + g_idx);
    uint32_t phase = (tick / 4) & 1;
    s->tile  = tiles[dir][phase];
    s->color = ghost_colors[gtype];
    s->flipx = false;
    s->flipy = false;
}

static void spr_anim_ghost_frightened(int g_idx, uint32_t tick) {
    sprite_t* s = spr(SPRITE_BLINKY + g_idx);
    static const uint8_t ftiles[2] = { 28, 29 };
    uint32_t phase = (tick / 4) & 1;
    s->tile = ftiles[phase];
    if (tick > (6*60 - 120)) {
        s->color = (tick & 0x10) ? COLOR_FRIGHTENED : COLOR_FRIGHTENED_BLINK;
    } else {
        s->color = COLOR_FRIGHTENED;
    }
    s->flipx = false;
}

static void spr_anim_ghost_eyes(int g_idx, dir_t dir) {
    static const uint8_t eye_tiles[4] = { 24, 26, 25, 27 };
    sprite_t* s = spr(SPRITE_BLINKY + g_idx);
    s->tile  = eye_tiles[dir];
    s->color = COLOR_EYES;
}

static void game_update_sprites(void) {
    spr_clear();

    /* Pac-Man */
    sprite_t* ps = spr(SPRITE_PACMAN);
    ps->enabled = true;
    spr_anim_pacman(state.game.pacman.actor.dir, state.game.pacman.actor.anim_tick);
    ps->pos = world_px_to_screen(state.game.pacman.actor.pos);
    /* offset so sprite center aligns with pixel pos */
    ps->pos.x -= SPRITE_WIDTH / 2;
    ps->pos.y -= SPRITE_HEIGHT / 2;

    /* Ghosts */
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        sprite_t* gs = spr(SPRITE_BLINKY + g);
        if (!ghost->alive || ghost->state == GHOSTSTATE_SPAWNING) {
            gs->enabled = false;
            continue;
        }
        gs->enabled = true;
        gs->pos = world_px_to_screen(ghost->actor.pos);
        gs->pos.x -= SPRITE_WIDTH / 2;
        gs->pos.y -= SPRITE_HEIGHT / 2;

        if (ghost->state == GHOSTSTATE_EYES) {
            spr_anim_ghost_eyes(g, ghost->actor.dir);
        } else if (ghost->state == GHOSTSTATE_FRIGHTENED || ghost->state == GHOSTSTATE_FROZEN) {
            spr_anim_ghost_frightened(g, since(ghost->frightened));
        } else {
            spr_anim_ghost(g, ghost->type, ghost->actor.dir, ghost->actor.anim_tick);
        }
    }

    /* Clone decoy */
    if (state.game.clone.active) {
        sprite_t* cs = spr(SPRITE_CLONE);
        cs->enabled = true;
        int2_t clone_px = i2((int16_t)(state.game.clone.pos.x * TILE_WIDTH + TILE_WIDTH/2),
                              (int16_t)(state.game.clone.pos.y * TILE_HEIGHT + TILE_HEIGHT/2));
        cs->pos = world_px_to_screen(clone_px);
        cs->pos.x -= SPRITE_WIDTH / 2;
        cs->pos.y -= SPRITE_HEIGHT / 2;
        cs->tile  = 48; /* closed mouth */
        cs->color = 0x08; /* slightly different color to distinguish from pacman */
        cs->flipx = false;
    }

    /* Tornado */
    if (state.game.tornado.active) {
        sprite_t* ts = spr(SPRITE_TORNADO);
        ts->enabled = true;
        int2_t tpx = i2((int16_t)(state.game.tornado.pos.x * TILE_WIDTH + TILE_WIDTH/2),
                         (int16_t)(state.game.tornado.pos.y * TILE_HEIGHT + TILE_HEIGHT/2));
        ts->pos = world_px_to_screen(tpx);
        ts->pos.x -= SPRITE_WIDTH / 2;
        ts->pos.y -= SPRITE_HEIGHT / 2;
        ts->tile  = (uint8_t)(29 + ((state.timing.tick / 4) & 1));
        ts->color = COLOR_INKY;
    }
}

/*== HUD (Score / Combo / Power-up slots) ====================================*/

static void vid_clear(uint8_t tile, uint8_t color) {
    for (int y = 0; y < DISPLAY_TILES_Y; y++)
        for (int x = 0; x < DISPLAY_TILES_X; x++) {
            state.gfx.video_ram[y][x] = tile;
            state.gfx.color_ram[y][x] = color;
        }
}

static void vid_tile(int sx, int sy, uint8_t tile, uint8_t color) {
    if (sx < 0 || sx >= DISPLAY_TILES_X) return;
    if (sy < 0 || sy >= DISPLAY_TILES_Y) return;
    state.gfx.video_ram[sy][sx] = tile;
    state.gfx.color_ram[sy][sx] = color;
}

static void vid_char(int sx, int sy, char c, uint8_t color) {
    /* basic ASCII -> tile mapping from original ROM */
    uint8_t tile = (uint8_t)c;
    if (c >= '0' && c <= '9') tile = (uint8_t)(0x30 + c - '0');
    else if (c >= 'A' && c <= 'Z') tile = (uint8_t)(0xC0 + c - 'A');
    else if (c >= 'a' && c <= 'z') tile = (uint8_t)(0xE0 + c - 'a');
    else if (c == ' ') tile = TILE_SPACE;
    else if (c == '-') tile = 0xBB;
    vid_tile(sx, sy, tile, color);
}

static void vid_text(int sx, int sy, const char* text, uint8_t color) {
    for (int i = 0; text[i]; i++) vid_char(sx + i, sy, text[i], color);
}

static void vid_number(int sx, int sy, uint32_t n, uint8_t color) {
    char buf[12];
    int pos = 11; buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    else while (n > 0) { buf[--pos] = (char)('0' + n % 10); n /= 10; }
    vid_text(sx, sy, buf + pos, color);
}

static void hud_draw(void) {
    /* top row: SCORE label and value */
    vid_text(0, 0, "SCORE", COLOR_WHITE_BORDER);
    vid_number(6, 0, state.game.score * 10, COLOR_WHITE_BORDER);

    /* hi score */
    vid_text(14, 0, "HI", COLOR_WHITE_BORDER);
    vid_number(17, 0, state.game.hiscore * 10, COLOR_WHITE_BORDER);

    /* combo multiplier */
    if (state.game.combo_multiplier > 1) {
        vid_text(0, 1, "x", COLOR_PACMAN);
        vid_number(1, 1, state.game.combo_multiplier, COLOR_PACMAN);
    }

    /* power-up slots (bottom row) */
    static const uint8_t pu_colors[NUM_POWERUP_TYPES] = {
        0, 0x01, 0x05, 0x07, 0x09, 0x05, 0x11, 0x01, 0x03, 0x04, 0x09, 0x14, 0x09, 0x11, 0x07, 0x06
    };
    static const uint8_t pu_tiles[NUM_POWERUP_TYPES] = {
        TILE_SPACE,
        TILE_PU_LASER,    TILE_PU_FREEZE,   TILE_PU_BOMB,
        TILE_PU_GIANT,    TILE_PU_TORNADO,  TILE_PU_STEALTH,
        TILE_PU_FIRE,     TILE_PU_LIGHTNING,TILE_PU_MAGNET,
        TILE_PU_CLONE,    TILE_PU_PAINT,    TILE_PU_SPEED,
        TILE_PU_REGEN,    TILE_PU_HAMMER,   TILE_PU_SPIRAL,
    };
    for (int i = 0; i < POWERUP_SLOTS; i++) {
        active_powerup_t* slot = &state.game.inventory[i];
        int sx = 1 + i * 3;
        int sy = DISPLAY_TILES_Y - 1;
        if (slot->type == PU_NONE) {
            vid_tile(sx, sy, TILE_SPACE, COLOR_BLANK);
        } else {
            uint8_t pt = pu_tiles[slot->type];
            uint8_t pc = pu_colors[slot->type];
            /* blink if active */
            if (slot->active && (state.timing.tick & 8)) pc = COLOR_WHITE_BORDER;
            vid_tile(sx, sy, pt, pc);
        }
    }

    /* bottom row slot labels */
    vid_char(0,  DISPLAY_TILES_Y - 1, '1', COLOR_DEFAULT);
    vid_char(4,  DISPLAY_TILES_Y - 1, '2', COLOR_DEFAULT);
    vid_char(7,  DISPLAY_TILES_Y - 1, '3', COLOR_DEFAULT);
}

/*== GAME INIT ===============================================================*/

static void game_init(void) {
    /* preserve hi score across games */
    uint32_t hiscore = state.game.hiscore;
    uint32_t seed = (uint32_t)(state.timing.tick * 6364136223846793005ULL + 1);

    /* zero game state */
    memset(&state.game, 0, sizeof(state.game));
    state.game.hiscore = hiscore;
    state.game.maze_gen_seed = seed ? seed : 0xDEADBEEF;
    state.game.xorshift = state.game.maze_gen_seed;
    state.game.combo_multiplier = 1;

    /* disable all triggers */
    disable(&state.game.started);
    disable(&state.game.ready_started);
    disable(&state.game.round_started);
    disable(&state.game.game_over);
    disable(&state.game.dot_eaten);
    disable(&state.game.pill_eaten);
    disable(&state.game.ghost_eaten);
    disable(&state.game.pacman_eaten);
    disable(&state.game.fruit_eaten);
    disable(&state.game.scroll_tick);
    disable(&state.game.glitch_advance);
    disable(&state.game.combo_timeout);

    for (int i = 0; i < POWERUP_SLOTS; i++) {
        state.game.inventory[i].type = PU_NONE;
        state.game.inventory[i].active = false;
        disable(&state.game.inventory[i].activated);
    }

    /* init maze */
    maze_init();

    /* glitch starts far below the viewport */
    state.game.glitch_world_row = -DISPLAY_TILES_Y;

    /* Pac-Man starting position: center of screen */
    state.game.pacman.actor.pos = i2(
        (int16_t)(14 * TILE_WIDTH),
        (int16_t)(20 * TILE_HEIGHT + TILE_HEIGHT/2)
    );
    state.game.pacman.actor.dir = DIR_LEFT;
    disable(&state.game.pacman.giant_timer);
    disable(&state.game.pacman.stealth_timer);
    disable(&state.game.pacman.speed_timer);
    disable(&state.game.pacman.fire_timer);
    disable(&state.game.pacman.paint_timer);
    disable(&state.game.pacman.magnet_timer);
    disable(&state.game.pacman.clone_timer);
    disable(&state.game.pacman.regen_timer);

    /* Spawn ghosts at staggered times and positions */
    static const int16_t gx[NUM_GHOSTS] = { 13, 15, 11, 17, 9, 19, 13 };
    static const int16_t gy[NUM_GHOSTS] = { 18, 18, 18, 18, 18, 18, 16 };
    for (int g = 0; g < NUM_GHOSTS; g++) {
        ghost_t* ghost = &state.game.ghost[g];
        ghost->type    = (ghosttype_t)g;
        ghost->alive   = true;
        ghost->state   = GHOSTSTATE_SPAWNING;
        ghost->actor.dir = DIR_LEFT;
        ghost->actor.pos = i2(
            (int16_t)(gx[g] * TILE_WIDTH + TILE_WIDTH/2),
            (int16_t)(gy[g] * TILE_HEIGHT + TILE_HEIGHT/2)
        );
        ghost->target_pos = ghost_scatter_targets[g];
        start_after(&ghost->spawn_timer, (uint32_t)(g * 60));
        disable(&ghost->frightened);
        disable(&ghost->frozen);
        disable(&ghost->eaten);
    }

    state.game.freeze = FREEZETYPE_READY;
    start(&state.game.scroll_tick);
    start(&state.game.glitch_advance);
    start(&state.game.round_started);
    input_enable();
}

/*== GAME TICK ===============================================================*/

/* forward declare round_started for ghost AI */
static trigger_t* p_round_started(void) { return &state.game.round_started; }
#define round_started (*p_round_started())

static void game_tick(void) {
    if (now(state.game.started)) {
        start(&state.gfx.fadein);
        start_after(&state.game.ready_started, 60);
    }
    if (now(state.game.ready_started)) {
        game_init();
        start_after(&state.game.round_started, 2*60);
    }
    if (now(state.game.round_started)) {
        state.game.freeze &= ~FREEZETYPE_READY;
    }

    /* game over */
    if (now(state.game.game_over)) {
        if (state.game.score > state.game.hiscore) state.game.hiscore = state.game.score;
        input_disable();
        start_after(&state.gfx.fadeout, GAMEOVER_TICKS);
        start_after(&state.intro.started, GAMEOVER_TICKS + FADE_TICKS);
    }

    /* eat ghost freeze */
    if (state.game.freeze & FREEZETYPE_EAT_GHOST) {
        if (after_once(state.game.ghost_eaten, GHOST_EATEN_FREEZE_TICKS)) {
            state.game.freeze &= ~FREEZETYPE_EAT_GHOST;
            /* compensate frightened timers */
            for (int g = 0; g < NUM_GHOSTS; g++) {
                ghost_t* gh = &state.game.ghost[g];
                if (gh->state == GHOSTSTATE_FRIGHTENED)
                    gh->frightened.tick += GHOST_EATEN_FREEZE_TICKS;
            }
        }
    }

    /* stop frightened sound after pill */
    if (after_once(state.game.pill_eaten, 6*60)) {
        /* restart normal background sound */
    }

    if (!state.game.freeze || (state.game.freeze == FREEZETYPE_EAT_GHOST)) {
        /* scroll and glitch */
        scroll_tick();
        glitch_tick();
        combo_tick();
        powerup_tick_all();

        /* move pacman */
        pacman_move();
        pacman_check_collisions();

        /* move ghosts */
        for (int g = 0; g < NUM_GHOSTS; g++) {
            ghost_t* ghost = &state.game.ghost[g];
            ghost_update_state(ghost);
            ghost_update_target(ghost);
            ghost_move(ghost);
        }

        /* handle input: activate power-up slots */
        if (state.input.enabled) {
            if (state.input.slot1) powerup_activate(0);
            if (state.input.slot2) powerup_activate(1);
            if (state.input.slot3) powerup_activate(2);
        }
    }

    /* update display */
    maze_blit_to_screen();
    hud_draw();
    game_update_sprites();

    /* hi score */
    if (state.game.score > state.game.hiscore) state.game.hiscore = state.game.score;
}

/*== INTRO TICK ==============================================================*/

static void intro_tick(void) {
    if (now(state.intro.started)) {
        start(&state.gfx.fadein);
        input_enable();
    }
    vid_clear(TILE_SPACE, COLOR_BLANK);
    /* Title */
    vid_text(5,  10, "PAC-MAN 256", COLOR_PACMAN);
    vid_text(4,  13, "EAT DOTS  SURVIVE", COLOR_DEFAULT);
    vid_text(4,  15, "AVOID THE GLITCH", COLOR_GLITCH);
    vid_text(2,  18, "ARROWS  MOVE", COLOR_DOT);
    vid_text(2,  19, "1 2 3   USE POWERUPS", COLOR_DOT);
    vid_text(4,  22, "PRESS ANY KEY", COLOR_WHITE_BORDER);

    /* blink PRESS ANY KEY */
    if (state.timing.tick & 16) {
        vid_text(4, 22, "             ", COLOR_BLANK);
    }

    /* hi score */
    vid_text(8, 2, "HI-SCORE", COLOR_WHITE_BORDER);
    vid_number(11, 3, state.game.hiscore * 10, COLOR_WHITE_BORDER);

    if (state.input.anykey) {
        input_disable();
        start(&state.gfx.fadeout);
        start_after(&state.game.started, FADE_TICKS);
        start_after(&state.gfx.fadein, FADE_TICKS);
        state.gamestate = GAMESTATE_GAME;
    }
}

