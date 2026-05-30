/**
 * aqi_pet_display.c  —  阿奇宠物养成：LVGL 9.x 显示层 v6
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet_display.c
 *
 * v6 新增：
 *   - 每个成长阶段独立像素图（幼犬/少年/青年/成年体型递增）
 *   - 获得经验后"扭屁股"动画（自动检测XP增加触发）
 *   - 丰富白天背景：太阳 + 2朵云彩
 *   - 丰富夜晚背景：月亮 + 6颗星星
 *   - DOG_PX=2（每格2×2像素，画面精细）
 */

#include "aqi_pet.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char* TAG = "AqiDisp";

/* =========================================================
   布局（240×240屏幕）
   ========================================================= */
#define SCREEN_W     240
#define SCREEN_H     240
#define STATUS_H      30
#define TEXT_H        45
#define PET_AREA_Y   (STATUS_H)
#define PET_AREA_H   (SCREEN_H - STATUS_H - TEXT_H)

#define DOG_PX        2            /* 每格 = 2×2 真实像素 */
#define DOG_GRID_SZ   32
#define DOG_CANVAS_W  (DOG_GRID_SZ * DOG_PX)   /* 64 */
#define DOG_CANVAS_H  (DOG_GRID_SZ * DOG_PX)   /* 64 */

#define DOG_X   ((SCREEN_W - DOG_CANVAS_W) / 2)
#define DOG_Y   (PET_AREA_Y + (PET_AREA_H - DOG_CANVAS_H) / 2)

/* =========================================================
   颜色
   ========================================================= */
#define COL_DAY_SKY      lv_color_hex(0x87CEEB)
#define COL_NIGHT_SKY    lv_color_hex(0x0D1B3E)
#define COL_DAY_GROUND   lv_color_hex(0x5DBB5D)
#define COL_NIGHT_GROUND lv_color_hex(0x1A3A1A)
#define COL_SUN          lv_color_hex(0xFFD700)
#define COL_CLOUD        lv_color_hex(0xFFFFFF)
#define COL_MOON         lv_color_hex(0xFFFACD)
#define COL_STAR         lv_color_hex(0xFFFFEE)
#define COL_STATUS_BG    lv_color_hex(0x2D2D2D)
#define COL_STATUS_TEXT  lv_color_hex(0xFFFFFF)
#define COL_XP_BAR_BG    lv_color_hex(0x555555)
#define COL_XP_BAR_FG    lv_color_hex(0x00D4AA)
#define COL_TEXT_BG      lv_color_hex(0x1C1C2E)
#define COL_TEXT_FG      lv_color_hex(0xF0F0F0)

/* =========================================================
   LVGL 对象
   ========================================================= */
static lv_obj_t* g_screen     = NULL;
static lv_obj_t* g_bg_obj     = NULL;
static lv_obj_t* g_sun        = NULL;
static lv_obj_t* g_cloud1     = NULL;
static lv_obj_t* g_cloud2     = NULL;
static lv_obj_t* g_moon       = NULL;
static lv_obj_t* g_stars[6]   = {NULL};
static lv_obj_t* g_status_bar = NULL;
static lv_obj_t* g_lv_label   = NULL;
static lv_obj_t* g_xp_fill    = NULL;
static lv_obj_t* g_dog_canvas = NULL;
static lv_obj_t* g_text_area  = NULL;
static lv_obj_t* g_text_label = NULL;

static uint8_t g_dog_buf[DOG_CANVAS_W * DOG_CANVAS_H * 2];

static TimerHandle_t g_msg_timer = NULL;
static char g_default_msg[64]   = "汪汪！今天要做什么呢？";
static bool g_is_night           = false;
static uint8_t g_frame_counter  = 0;
static uint8_t g_anim_frame     = 0;

/* 扭屁股动画 */
static uint8_t  g_wiggle_ticks = 0;   /* >0 时播放扭屁股 */
static uint32_t g_last_xp      = 0;   /* 上一帧的XP，用于检测增加 */

/* =========================================================
   32×32 像素网格（经 Python/PIL 可视化验证）

   字符含义：
     .  背景透明
     B  深棕轮廓
     C  身体主色（随阶段变化）
     L  面部/腹部浅色（随阶段变化）
     E  眼白
     P  瞳孔
     K  粉鼻
     T  尾巴色
     A  垂耳深棕（固定色，不随阶段变）
   ========================================================= */

/* ── 幼犬：圆滚滚小奶狗，头大身小腿超短 ── */
static const char PUPPY_GRID[32][33] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..............BBBBBB............",  /* 头顶 */
    ".............BCCCCCCB...........",
    "..........BBBCCLLLLCCBBB........",  /* 垂耳接头 */
    "..........BAABCLLEELLCBAB.......",  /* 眼睛 */
    "..........BAABCLLPPLLLCBAB......",  /* 瞳孔 */
    "..........BAABCLLEELLLLCBAB.....",
    "..........BAABCLLLLLLLLCBAB.....",
    "..........BAABCLLKKKLLLCBAB.....",  /* 鼻子 */
    "..........BAABBBBBBBBBBBAB......",  /* 头底 */
    "..........BAAB.........BAB......",  /* 垂耳悬空 */
    "...........BBB.........BBB......",  /* 耳尖 */
    ".............BCCCCCCB...........",  /* 身体 */
    ".............BCLLLLCB..BTTB.....",  /* 身体+尾 */
    ".............BCLLLLCB...BTB.....",
    ".............BCCCCCCCB..........",
    ".............BCBBBBBCB..........",  /* 腿隔断 */
    ".............BCB...BCB..........",  /* 腿（超短） */
    ".............BBB...BBB..........",  /* 爪子 */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

/* ── 少年犬：中等体型，已有少年感 ── */
static const char TEEN_GRID[32][33] = {
    "................................",
    "................................",
    "................................",
    "................................",
    "..............BBBBBB............",  /* 头顶 */
    ".............BCCCCCCB...........",
    "............BCCCCCCCCCB.........",
    "........BBBCCCLLLLLLLCCCBBB.....",  /* 垂耳接头 */
    "........BAABCCLLEELLEELLCCBAB...",  /* 眼睛 */
    "........BAABCCLLPPLLPPLLCCBAB...",  /* 瞳孔 */
    "........BAABCCLLEELLEELLCCBAB...",
    "........BAABCCLLLLLLLLLLCCBAB...",
    "........BAABCCLLLKKKLLLLCCBAB...",  /* 鼻子 */
    "........BAABBBBBBBBBBBBBBBAB....",  /* 头底 */
    "........BAAB...............BAB..",  /* 垂耳悬空 */
    ".........BBB...............BBB..",  /* 耳尖 */
    "..........BCCCCCCCCCCCB.........",  /* 身体 */
    "..........BCLLLLLLLLLLCB.BTTTB..",  /* 身体+尾 */
    "..........BCLLLLLLLLLLCB..BTTB..",
    "..........BCLLLLLLLLLLCB...BTB..",
    "..........BCCCCCCCCCCCCCB.......",
    "..........BCBBBBBBBBBBBBCB......",  /* 腿隔断 */
    "..........BCB.......BCB.........",  /* 腿 */
    "..........BCB.......BCB.........",
    "..........BCB.......BCB.........",
    "..........BBB.......BBB.........",  /* 爪子 */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

/* ── 青年犬：高大魁梧，已有成熟轮廓 ── */
static const char YOUNG_GRID[32][33] = {
    "................................",
    "................................",
    "................................",
    "..............BBBBBBBB..........",  /* 头顶 */
    ".............BCCCCCCCCB.........",
    "............BCCCCCCCCCCCB.......",
    ".......BBBCCCCLLLLLLLLLLCCBBB...",  /* 垂耳接头 */
    ".......BAABCCLLEEELLLEEELCCBAB..",  /* 眼睛 */
    ".......BAABCCLLEPPLLLEPPLCCBAB..",  /* 瞳孔 */
    ".......BAABCCLLEEELLLEEELCCBAB..",
    ".......BAABCCLLLLLLLLLLLLCCBAB..",
    ".......BAABCCLLLKKKKLLLLLLCCBAB.",  /* 鼻子 */
    ".......BAABBBBBBBBBBBBBBBBBAB...",  /* 头底 */
    ".......BAAB.................BAB.",  /* 垂耳悬空 */
    "........BBB.................BBB.",  /* 耳尖 */
    "..........BCCCCCCCCCCCCCB.......",  /* 身体 */
    "..........BCLLLLLLLLLLLLCB.BTTB.",  /* 身体+尾 */
    "..........BCLLLLLLLLLLLLCB..BTB.",
    "..........BCLLLLLLLLLLLLCB......",
    "..........BCLLLLLLLLLLLLCB......",
    "..........BCCCCCCCCCCCCCCB......",
    "..........BCBBBBBBBBBBBBBCB.....",  /* 腿隔断 */
    "..........BCB.........BCB.......",  /* 腿 */
    "..........BCB.........BCB.......",
    "..........BCB.........BCB.......",
    "..........BBB.........BBB.......",  /* 爪子 */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

/* ── 成年犬：最大体型，威武沉稳 ── */
static const char ADULT_GRID[32][33] = {
    "................................",
    "................................",
    "................................",
    "............BBBBBBBB............",  /* 头顶 */
    "...........BCCCCCCCCCB..........",
    "........BBBCCLLLLLLLCCBBB.......",  /* 垂耳接头 */
    "........BAABCLLEEELLLLCBAB......",  /* 眼睛 */
    "........BAABCLLEPPELLLCBAB......",  /* 瞳孔 */
    "........BAABCLLEEELLLLCBAB......",
    "........BAABCLLLLLLLLLCBAB......",
    "........BAABCLLLKKKLLCBBAB......",  /* 鼻子 */
    "........BAABCLLLLLLLLCBBAB......",
    "........BAABBBBBBBBBBBBAB.......",  /* 头底 */
    "........BAAB...........BAB......",  /* 垂耳悬空 */
    ".........BBB...........BBB......",  /* 耳尖 */
    "...........BCCCCCCCCCCB.........",  /* 身体 */
    "...........BCLLLLLLLLCB.BTTTTB..",  /* 身体+尾 */
    "...........BCLLLLLLLLCB..BTTTB..",
    "...........BCLLLLLLLLCB...BTTB..",
    "...........BCLLLLLLLLCB....BTB..",
    "...........BCLLLLLLLLCB.........",
    "...........BCCCCCCCCCCCB........",
    "...........BCBBBBBBBBBCB........",  /* 腿隔断 */
    "...........BCB......BCB.........",  /* 腿 */
    "...........BCB......BCB.........",
    "...........BCB......BCB.........",
    "...........BBB......BBB.........",  /* 爪子 */
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

/* ── 蛋（帧0=完整，帧1=裂纹） ── */
static const char EGG_GRID[2][32][33] = {
  { /* 帧0：完整椭圆蛋 */
    "................................",
    "................................",
    "................................",
    "............BBBBBBB.............",
    "...........BSSSSSSSB............",
    "..........BHSSSSSSSBB...........",
    "..........BHHSSSSSSSSB..........",
    ".........BBHHSSSSSSSSSB.........",
    ".........BSSSSSSSSSSSSB.........",
    "........BBSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BBSSSSSSSSSSSSB.........",
    ".........BSSSSSSSSSSSB..........",
    ".........BSSSSSSSSSSSB..........",
    ".........BBSSSSSSSSSBB..........",
    "..........BBSSSSSSSBB...........",
    "...........BBBSSSSBBB...........",
    "............BBBBBBB.............",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
  },
  { /* 帧1：裂纹蛋 */
    "................................",
    "................................",
    "................................",
    "............BBBBBBB.............",
    "...........BSSSSSSSB............",
    "..........BHSSSSSScBB...........",
    "..........BHHSSSSSScSB..........",
    ".........BBHHSSSSScSSB..........",
    ".........BSSSSSSSScSSB..........",
    "........BBSSSSSSSSSccB..........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BSSSSSSSSSSSSSB.........",
    "........BBSSSSSSSSSSSSB.........",
    ".........BSSSSSSSSSSSB..........",
    ".........BSSSSSSSSSSSB..........",
    ".........BBSSSSSSSSSBB..........",
    "..........BBSSSSSSSBB...........",
    "...........BBBSSSSBBB...........",
    "............BBBBBBB.............",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
  }
};

/* =========================================================
   绘图：把字符格(ax,ay)填成指定颜色
   ========================================================= */
static void _px(lv_obj_t* c, int ax, int ay, lv_color_t col)
{
    for (int dy = 0; dy < DOG_PX; dy++)
        for (int dx = 0; dx < DOG_PX; dx++)
            lv_canvas_set_px(c, ax*DOG_PX+dx, ay*DOG_PX+dy, col, LV_OPA_COVER);
}

/* =========================================================
   绘制蛋
   ========================================================= */
static void _draw_egg(lv_obj_t* canvas, lv_color_t bg, int frame)
{
    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);
    const char (*grid)[33] = EGG_GRID[frame & 1];

    lv_color_t S = lv_color_hex(0xFFF5DC);
    lv_color_t B = lv_color_hex(0xC8A050);
    lv_color_t H = lv_color_hex(0xFFFFFF);
    lv_color_t cr= lv_color_hex(0x643C14);

    for (int y = 0; y < DOG_GRID_SZ; y++)
        for (int x = 0; x < DOG_GRID_SZ; x++) {
            char ch = grid[y][x];
            lv_color_t col;
            switch (ch) {
                case 'S': col=S;  break;
                case 'B': col=B;  break;
                case 'H': col=H;  break;
                case 'c': col=cr; break;
                default:  continue;
            }
            _px(canvas, x, y, col);
        }
}

/* =========================================================
   绘制狗（根据阶段选不同网格+颜色）
   ========================================================= */
static void _draw_dog(lv_obj_t* canvas, PetStage stage, PetMood mood, lv_color_t bg)
{
    const PetState* st = pet_get_state();

    /* 升阶白闪 */
    if (st->stage_up_flash && (st->stage_up_ticks % 2 == 0)) {
        lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        return;
    }

    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);

    if (stage == PET_STAGE_EGG) {
        _draw_egg(canvas, bg, g_anim_frame);
        return;
    }

    /* 选网格 */
    const char (*grid)[33];
    switch (stage) {
        case PET_STAGE_PUPPY: grid = PUPPY_GRID; break;
        case PET_STAGE_TEEN:  grid = TEEN_GRID;  break;
        case PET_STAGE_YOUNG: grid = YOUNG_GRID; break;
        default:              grid = ADULT_GRID; break;
    }

    /* 颜色随阶段变深，体现成长 */
    lv_color_t col_C, col_L, col_T;
    switch (stage) {
        case PET_STAGE_PUPPY:
            col_C = lv_color_hex(0xC88230);
            col_L = lv_color_hex(0xF8D7AF);
            col_T = lv_color_hex(0xAA6820);
            break;
        case PET_STAGE_TEEN:
            col_C = lv_color_hex(0xA06828);
            col_L = lv_color_hex(0xE0B882);
            col_T = lv_color_hex(0x885018);
            break;
        case PET_STAGE_YOUNG:
            col_C = lv_color_hex(0x7A4A18);
            col_L = lv_color_hex(0xC09060);
            col_T = lv_color_hex(0x603810);
            break;
        default: /* ADULT */
            col_C = lv_color_hex(0x4A2808);
            col_L = lv_color_hex(0x906040);
            col_T = lv_color_hex(0x381800);
            break;
    }

    lv_color_t col_B = lv_color_hex(0x2A1608);
    lv_color_t col_A = lv_color_hex(0x5A2D08);
    lv_color_t col_E = lv_color_hex(0xFFFFFF);
    lv_color_t col_P = lv_color_hex(0x140A02);
    lv_color_t col_K = lv_color_hex(0xDC6464);

    for (int y = 0; y < DOG_GRID_SZ; y++)
        for (int x = 0; x < DOG_GRID_SZ; x++) {
            char ch = grid[y][x];
            lv_color_t col;
            switch (ch) {
                case 'B': col=col_B; break;
                case 'C': col=col_C; break;
                case 'L': col=col_L; break;
                case 'E': col=col_E; break;
                case 'P': col=col_P; break;
                case 'K': col=col_K; break;
                case 'T': col=col_T; break;
                case 'A': col=col_A; break;
                default:  continue;
            }
            _px(canvas, x, y, col);
        }

    /* 开心/兴奋：角落星星 */
    if (mood == PET_MOOD_HAPPY || mood == PET_MOOD_EXCITED) {
        lv_color_t star = lv_color_hex(0xFFDD00);
        _px(canvas, 28, 1, star); _px(canvas, 29, 2, star); _px(canvas, 30, 1, star);
        if (mood == PET_MOOD_EXCITED) {
            _px(canvas, 1, 1, star); _px(canvas, 2, 2, star); _px(canvas, 3, 1, star);
        }
    }
    /* 困了：ZZZ */
    if (mood == PET_MOOD_SLEEPY) {
        lv_color_t zzz = lv_color_hex(0xAAAAAA);
        _px(canvas, 28, 1, zzz); _px(canvas, 29, 2, zzz); _px(canvas, 30, 3, zzz);
    }
}

/* =========================================================
   背景切换（日/夜）
   ========================================================= */
static void _draw_background(bool night)
{
    g_is_night = night;
    if (!g_bg_obj) return;

    lv_obj_set_style_bg_color(g_bg_obj, night ? COL_NIGHT_SKY : COL_DAY_SKY, 0);

    /* 地面 */
    lv_obj_t* ground = lv_obj_get_child(g_bg_obj, 0);
    if (ground) lv_obj_set_style_bg_color(ground, night ? COL_NIGHT_GROUND : COL_DAY_GROUND, 0);

    /* 白天元素 */
    if (g_sun)    lv_obj_set_style_opa(g_sun,    night ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (g_cloud1) lv_obj_set_style_opa(g_cloud1, night ? LV_OPA_TRANSP : LV_OPA_70,    0);
    if (g_cloud2) lv_obj_set_style_opa(g_cloud2, night ? LV_OPA_TRANSP : LV_OPA_70,    0);

    /* 夜晚元素 */
    if (g_moon)   lv_obj_set_style_opa(g_moon,   night ? LV_OPA_COVER  : LV_OPA_TRANSP, 0);
    for (int i = 0; i < 6; i++)
        if (g_stars[i]) lv_obj_set_style_opa(g_stars[i], night ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

/* =========================================================
   状态栏刷新
   ========================================================= */
static void _update_status_bar(const PetState* st)
{
    uint32_t xp_min, xp_max;
    const char* name;
    switch (st->saved.stage) {
        case PET_STAGE_EGG:   xp_min=0;   xp_max=PET_XP_EGG_MAX;    name="蛋";   break;
        case PET_STAGE_PUPPY: xp_min=15;  xp_max=PET_XP_STAGE1_MAX; name="幼犬"; break;
        case PET_STAGE_TEEN:  xp_min=75;  xp_max=PET_XP_STAGE2_MAX; name="少年"; break;
        case PET_STAGE_YOUNG: xp_min=125; xp_max=PET_XP_STAGE3_MAX; name="青年"; break;
        default:              xp_min=175; xp_max=270;                name="成年"; break;
    }
    uint32_t cur   = st->saved.total_xp > xp_min ? st->saved.total_xp - xp_min : 0;
    uint32_t range = xp_max - xp_min;

    char buf[48];
    snprintf(buf, sizeof(buf), "%s %lu/%lu", name, (unsigned long)cur, (unsigned long)range);
    if (g_lv_label) lv_label_set_text(g_lv_label, buf);

    if (g_xp_fill && range > 0) {
        int w = (int)(((uint64_t)cur * 160) / range);
        lv_obj_set_width(g_xp_fill, w > 160 ? 160 : w);
    }
}

/* =========================================================
   消息定时器
   ========================================================= */
static void _msg_timer_cb(TimerHandle_t t)
{
    (void)t;
    if (g_text_label) lv_label_set_text(g_text_label, g_default_msg);
}

/* =========================================================
   初始化
   ========================================================= */
void pet_display_init(lv_obj_t* parent)
{
    if (!parent) parent = lv_scr_act();
    g_screen = parent;

    lv_obj_set_style_bg_color(parent, COL_DAY_SKY, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* ── 背景区 ── */
    g_bg_obj = lv_obj_create(parent);
    lv_obj_set_size(g_bg_obj, SCREEN_W, PET_AREA_H);
    lv_obj_set_pos(g_bg_obj, 0, STATUS_H);
    lv_obj_set_style_border_width(g_bg_obj, 0, 0);
    lv_obj_set_style_pad_all(g_bg_obj, 0, 0);
    lv_obj_set_style_radius(g_bg_obj, 0, 0);
    lv_obj_set_style_bg_color(g_bg_obj, COL_DAY_SKY, 0);
    lv_obj_set_scroll_dir(g_bg_obj, LV_DIR_NONE);

    /* 地面 */
    lv_obj_t* ground = lv_obj_create(g_bg_obj);
    lv_obj_set_size(ground, SCREEN_W, 22);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ground, COL_DAY_GROUND, 0);
    lv_obj_set_style_border_width(ground, 0, 0);
    lv_obj_set_style_radius(ground, 0, 0);

    /* ☀️ 太阳（白天显示） */
    g_sun = lv_obj_create(g_bg_obj);
    lv_obj_set_size(g_sun, 26, 26);
    lv_obj_set_pos(g_sun, 190, 8);
    lv_obj_set_style_radius(g_sun, 13, 0);
    lv_obj_set_style_bg_color(g_sun, COL_SUN, 0);
    lv_obj_set_style_border_width(g_sun, 0, 0);
    lv_obj_set_style_shadow_width(g_sun, 12, 0);
    lv_obj_set_style_shadow_color(g_sun, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_shadow_opa(g_sun, LV_OPA_50, 0);

    /* ☁️ 云彩1 */
    g_cloud1 = lv_obj_create(g_bg_obj);
    lv_obj_set_size(g_cloud1, 52, 16);
    lv_obj_set_pos(g_cloud1, 10, 12);
    lv_obj_set_style_radius(g_cloud1, 8, 0);
    lv_obj_set_style_bg_color(g_cloud1, COL_CLOUD, 0);
    lv_obj_set_style_border_width(g_cloud1, 0, 0);
    lv_obj_set_style_opa(g_cloud1, LV_OPA_70, 0);

    /* ☁️ 云彩2 */
    g_cloud2 = lv_obj_create(g_bg_obj);
    lv_obj_set_size(g_cloud2, 40, 14);
    lv_obj_set_pos(g_cloud2, 120, 22);
    lv_obj_set_style_radius(g_cloud2, 7, 0);
    lv_obj_set_style_bg_color(g_cloud2, COL_CLOUD, 0);
    lv_obj_set_style_border_width(g_cloud2, 0, 0);
    lv_obj_set_style_opa(g_cloud2, LV_OPA_70, 0);

    /* 🌙 月亮（夜晚显示，初始隐藏） */
    g_moon = lv_obj_create(g_bg_obj);
    lv_obj_set_size(g_moon, 28, 28);
    lv_obj_set_pos(g_moon, 185, 8);
    lv_obj_set_style_radius(g_moon, 14, 0);
    lv_obj_set_style_bg_color(g_moon, COL_MOON, 0);
    lv_obj_set_style_border_width(g_moon, 0, 0);
    lv_obj_set_style_opa(g_moon, LV_OPA_TRANSP, 0);

    /* ⭐ 星星（6颗，夜晚显示，初始隐藏） */
    static const int STAR_X[] = { 15, 60, 100, 145, 30, 170};
    static const int STAR_Y[] = { 10, 25,  8,  20, 38,  35};
    for (int i = 0; i < 6; i++) {
        g_stars[i] = lv_obj_create(g_bg_obj);
        lv_obj_set_size(g_stars[i], 3, 3);
        lv_obj_set_pos(g_stars[i], STAR_X[i], STAR_Y[i]);
        lv_obj_set_style_radius(g_stars[i], 1, 0);
        lv_obj_set_style_bg_color(g_stars[i], COL_STAR, 0);
        lv_obj_set_style_border_width(g_stars[i], 0, 0);
        lv_obj_set_style_opa(g_stars[i], LV_OPA_TRANSP, 0);
    }

    /* ── 状态栏 ── */
    g_status_bar = lv_obj_create(parent);
    lv_obj_set_size(g_status_bar, SCREEN_W, STATUS_H);
    lv_obj_set_pos(g_status_bar, 0, 0);
    lv_obj_set_style_bg_color(g_status_bar, COL_STATUS_BG, 0);
    lv_obj_set_style_border_width(g_status_bar, 0, 0);
    lv_obj_set_style_radius(g_status_bar, 0, 0);
    lv_obj_set_style_pad_all(g_status_bar, 2, 0);

    g_lv_label = lv_label_create(g_status_bar);
    lv_obj_set_style_text_color(g_lv_label, COL_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(g_lv_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_lv_label, LV_ALIGN_LEFT_MID, 4, -5);
    lv_label_set_text(g_lv_label, "蛋 0/15");

    lv_obj_t* xp_bg = lv_obj_create(g_status_bar);
    lv_obj_set_size(xp_bg, 160, 5);
    lv_obj_align(xp_bg, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(xp_bg, COL_XP_BAR_BG, 0);
    lv_obj_set_style_border_width(xp_bg, 0, 0);
    lv_obj_set_style_radius(xp_bg, 2, 0);

    g_xp_fill = lv_obj_create(g_status_bar);
    lv_obj_set_size(g_xp_fill, 0, 5);
    lv_obj_align(g_xp_fill, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(g_xp_fill, COL_XP_BAR_FG, 0);
    lv_obj_set_style_border_width(g_xp_fill, 0, 0);
    lv_obj_set_style_radius(g_xp_fill, 2, 0);

    /* ── 宠物画布（64×64） ── */
    g_dog_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(g_dog_canvas, g_dog_buf,
                         DOG_CANVAS_W, DOG_CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(g_dog_canvas, DOG_X, DOG_Y);
    lv_canvas_fill_bg(g_dog_canvas, COL_DAY_SKY, LV_OPA_COVER);

    /* ── 底部文字区 ── */
    g_text_area = lv_obj_create(parent);
    lv_obj_set_size(g_text_area, SCREEN_W, TEXT_H);
    lv_obj_set_pos(g_text_area, 0, SCREEN_H - TEXT_H);
    lv_obj_set_style_bg_color(g_text_area, COL_TEXT_BG, 0);
    lv_obj_set_style_border_width(g_text_area, 0, 0);
    lv_obj_set_style_radius(g_text_area, 0, 0);
    lv_obj_set_style_pad_all(g_text_area, 4, 0);

    g_text_label = lv_label_create(g_text_area);
    lv_obj_set_style_text_color(g_text_label, COL_TEXT_FG, 0);
    lv_obj_set_style_text_font(g_text_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(g_text_label, SCREEN_W - 8);
    lv_label_set_long_mode(g_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(g_text_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(g_text_label, g_default_msg);

    g_msg_timer = xTimerCreate("pet_msg", pdMS_TO_TICKS(3000),
                               pdFALSE, NULL, _msg_timer_cb);

    /* 初始化XP跟踪 */
    const PetState* st = pet_get_state();
    g_last_xp = st->saved.total_xp;

    ESP_LOGI(TAG, "pet_display_init OK v6");
}

/* =========================================================
   每帧刷新（200ms）
   ========================================================= */
void pet_display_update(void)
{
    /* 层叠顺序：确保宠物UI在最顶层 */
    if (g_bg_obj)     lv_obj_move_foreground(g_bg_obj);
    if (g_dog_canvas) lv_obj_move_foreground(g_dog_canvas);
    if (g_status_bar) lv_obj_move_foreground(g_status_bar);
    if (g_text_area)  lv_obj_move_foreground(g_text_area);

    if (!g_dog_canvas) return;

    /* 动画帧（约600ms切帧） */
    if (++g_frame_counter >= 3) {
        g_frame_counter = 0;
        g_anim_frame ^= 1;
    }

    const PetState* st = pet_get_state();

    /* 日夜背景切换 */
    bool night = (pet_get_time_period() == TIME_NIGHT);
    if (night != g_is_night) _draw_background(night);
    lv_color_t bg = night ? COL_NIGHT_SKY : COL_DAY_SKY;

    /* 升阶闪光倒计时 */
    if (st->stage_up_flash) {
        PetState* mst = (PetState*)st;
        if (mst->stage_up_ticks > 0) mst->stage_up_ticks--;
        else { mst->stage_up_flash = false; mst->mood = PET_MOOD_IDLE; }
    }

    /* ── 检测XP增加 → 触发扭屁股 ── */
    if (st->saved.total_xp > g_last_xp && g_last_xp > 0) {
        g_wiggle_ticks = 12;  /* 12帧 × 200ms ≈ 2.4秒 */
    }
    g_last_xp = st->saved.total_xp;

    /* ── 计算位置偏移 ── */
    int x_off = 0, y_off = 0;

    if (g_wiggle_ticks > 0) {
        /* 扭屁股：左右快速摆动，奇偶帧各偏 ±5px */
        x_off = (g_wiggle_ticks % 2 == 0) ? 5 : -5;
        g_wiggle_ticks--;
    } else {
        /* 普通动画（各阶段节奏不同） */
        switch (st->saved.stage) {
            case PET_STAGE_EGG:
                x_off = g_anim_frame ? 4 : -4;   /* 蛋：左右摇摆 */
                break;
            case PET_STAGE_PUPPY:
                y_off = g_anim_frame ? -5 : 0;   /* 幼犬：活泼大弹跳 */
                break;
            case PET_STAGE_TEEN:
                y_off = g_anim_frame ? -4 : 0;   /* 少年：中弹跳 */
                break;
            case PET_STAGE_YOUNG:
                y_off = g_anim_frame ? -3 : 0;   /* 青年：小弹跳 */
                break;
            case PET_STAGE_ADULT:
                y_off = g_anim_frame ? -2 : 0;   /* 成年：轻踏步 */
                break;
            default: break;
        }
        if (st->mood == PET_MOOD_HAPPY || st->mood == PET_MOOD_EXCITED)
            y_off -= 2;
    }

    lv_obj_set_pos(g_dog_canvas, DOG_X + x_off, DOG_Y + y_off);

    /* 绘制宠物 */
    _draw_dog(g_dog_canvas, st->saved.stage, st->mood, bg);

    /* 刷新状态栏 */
    _update_status_bar(st);
}

/* =========================================================
   任务完成提示
   ========================================================= */
void pet_display_show_task_done(TaskType task, uint32_t xp_gained)
{
    char msg[80];
    snprintf(msg, sizeof(msg), "太棒了！%s完成，获得%lu点能量！",
             pet_task_name(task), (unsigned long)xp_gained);
    pet_display_show_message(msg, 4000);
}

void pet_display_show_message(const char* msg, uint32_t timeout_ms)
{
    if (!g_text_label) return;
    lv_label_set_text(g_text_label, msg);
    if (timeout_ms > 0 && g_msg_timer) {
        xTimerChangePeriod(g_msg_timer, pdMS_TO_TICKS(timeout_ms), 0);
        xTimerReset(g_msg_timer, 0);
    }
}

/* =========================================================
   对话模式
   ========================================================= */
void pet_display_set_talking(bool talking)
{
    if (!g_dog_canvas) return;
    if (talking) {
        lv_obj_add_flag(g_dog_canvas, LV_OBJ_FLAG_HIDDEN);
        pet_display_show_message("阿奇在听...", 0);
        pet_set_mood(PET_MOOD_TALKING);
    } else {
        lv_obj_clear_flag(g_dog_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_text_label, g_default_msg);
        pet_set_mood(PET_MOOD_IDLE);
    }
}
