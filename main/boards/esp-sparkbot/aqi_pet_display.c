/**
 * aqi_pet_display.c  —  阿奇宠物养成：LVGL 9.x 显示层 v3
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet_display.c
 *
 * 变更（v3）：
 *   - 新增蛋阶段像素画（带裂纹2帧动画）
 *   - 各阶段弹跳动画（canvas位置上下偏移）
 *   - 升阶白色闪光动画
 *   - 修复所有UI层级（bg/dog/statusbar/textarea全部移到最前）
 *   - 动画帧计数器（每3次200ms = 600ms切换一帧）
 */

#include "aqi_pet.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char* TAG = "AqiDisp";

/* =========================================================
   布局常量（240×240屏幕）
   ========================================================= */

#define SCREEN_W    240
#define SCREEN_H    240
#define STATUS_H     30
#define TEXT_H       45
#define PET_AREA_Y  (STATUS_H)
#define PET_AREA_H  (SCREEN_H - STATUS_H - TEXT_H)   // 165px

#define DOG_CANVAS_W  96
#define DOG_CANVAS_H  96
#define DOG_PX         6   // 每个艺术像素 = 6×6 真实像素

#define DOG_X   ((SCREEN_W - DOG_CANVAS_W) / 2)      // 72
#define DOG_Y   (PET_AREA_Y + (PET_AREA_H - DOG_CANVAS_H) / 2)  // 64

/* =========================================================
   颜色
   ========================================================= */

#define COL_DAY_SKY      lv_color_hex(0x87CEEB)
#define COL_NIGHT_SKY    lv_color_hex(0x1A1A3A)
#define COL_DAY_GROUND   lv_color_hex(0x90EE90)
#define COL_NIGHT_GROUND lv_color_hex(0x2E4A2E)
#define COL_NIGHT_MOON   lv_color_hex(0xFFFACD)
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
static lv_obj_t* g_status_bar = NULL;
static lv_obj_t* g_lv_label   = NULL;
static lv_obj_t* g_xp_fill    = NULL;
static lv_obj_t* g_dog_canvas = NULL;
static lv_obj_t* g_text_area  = NULL;
static lv_obj_t* g_text_label = NULL;

static uint8_t g_dog_buf[DOG_CANVAS_W * DOG_CANVAS_H * 2];  // RGB565

static TimerHandle_t g_msg_timer = NULL;
static char g_default_msg[64]   = "汪汪！今天要做什么呢？";
static bool g_is_night           = false;

/* 动画帧计数（每3次200ms = 600ms切换一帧） */
static uint8_t  g_frame_counter = 0;
static uint8_t  g_anim_frame    = 0;

/* =========================================================
   像素绘制工具
   ========================================================= */

/* 画一个艺术像素（DOG_PX×DOG_PX 真实像素） */
static void _px(lv_obj_t* c, int ax, int ay, lv_color_t col)
{
    for (int dy = 0; dy < DOG_PX; dy++)
        for (int dx = 0; dx < DOG_PX; dx++)
            lv_canvas_set_px(c, ax * DOG_PX + dx, ay * DOG_PX + dy, col, LV_OPA_COVER);
}

/* 画一个艺术矩形 */
static void _rect(lv_obj_t* c, int ax, int ay, int aw, int ah, lv_color_t col)
{
    for (int dy = 0; dy < ah; dy++)
        for (int dx = 0; dx < aw; dx++)
            _px(c, ax + dx, ay + dy, col);
}

/* =========================================================
   蛋像素画（16×16艺术坐标系）
   两帧：frame0=正常蛋，frame1=带裂纹
   ========================================================= */

static void _draw_egg(lv_obj_t* canvas, lv_color_t bg, int anim_frame)
{
    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);

    lv_color_t shell  = lv_color_hex(0xFFF0D0);  // 蛋壳 米白
    lv_color_t border = lv_color_hex(0xC8A050);  // 轮廓 浅棕
    lv_color_t shine  = lv_color_hex(0xFFFFFF);  // 高光 白
    lv_color_t crack  = lv_color_hex(0x8B4513);  // 裂纹 深棕

    /* 蛋形（椭圆近似，上窄下宽）艺术坐标：
       行1: cols 5~10
       行2: cols 4~11
       行3~9: cols 3~12
       行10: cols 4~11
       行11: cols 5~10
    */
    struct { int x, y, w; } rows[] = {
        {5,1,6}, {4,2,8},
        {3,3,10},{3,4,10},{3,5,10},{3,6,10},{3,7,10},{3,8,10},{3,9,10},
        {4,10,8},{5,11,6}
    };
    for (int r = 0; r < 11; r++) {
        for (int dx = 0; dx < rows[r].w; dx++)
            _px(canvas, rows[r].x + dx, rows[r].y, shell);
    }

    /* 轮廓 */
    struct { int x, y; } border_px[] = {
        {5,1},{6,1},{7,1},{8,1},{9,1},{10,1},
        {4,2},{11,2},{3,3},{12,3},{3,9},{12,9},
        {4,10},{11,10},{5,11},{6,11},{7,11},{8,11},{9,11},{10,11}
    };
    for (int i = 0; i < 20; i++)
        _px(canvas, border_px[i].x, border_px[i].y, border);

    /* 高光（左上角） */
    _px(canvas, 5, 3, shine);
    _px(canvas, 6, 3, shine);
    _px(canvas, 5, 4, shine);

    /* frame1：添加裂纹 */
    if (anim_frame == 1) {
        _px(canvas, 8, 4, crack);
        _px(canvas, 7, 5, crack);
        _px(canvas, 8, 5, crack);
        _px(canvas, 8, 6, crack);
        _px(canvas, 9, 6, crack);
    }
}

/* =========================================================
   狗像素画 PixelDef 系统
   ========================================================= */

typedef struct { int x; int y; char c; } PixelDef;

/* 幼犬（小棕狗） */
static const PixelDef PUPPY_PIXELS[] = {
    {2,0,'P'},{3,0,'P'},{2,1,'B'},{3,1,'B'},
    {11,0,'P'},{12,0,'P'},{11,1,'B'},{12,1,'B'},
    {4,1,'B'},{5,1,'B'},{6,1,'B'},{7,1,'B'},{8,1,'B'},{9,1,'B'},{10,1,'B'},
    {2,2,'B'},{13,2,'B'},{2,3,'B'},{13,3,'B'},{2,4,'B'},{13,4,'B'},
    {3,5,'B'},{4,5,'B'},{5,5,'B'},{6,5,'B'},{7,5,'B'},
    {8,5,'B'},{9,5,'B'},{10,5,'B'},{11,5,'B'},{12,5,'B'},
    {3,2,'C'},{4,2,'C'},{5,2,'C'},{6,2,'C'},{7,2,'C'},
    {8,2,'C'},{9,2,'C'},{10,2,'C'},{11,2,'C'},{12,2,'C'},
    {3,3,'C'},{4,3,'L'},{5,3,'L'},{6,3,'L'},{7,3,'L'},
    {8,3,'L'},{9,3,'L'},{10,3,'L'},{11,3,'C'},{12,3,'C'},
    {3,4,'C'},{4,4,'L'},{5,4,'L'},{6,4,'L'},{7,4,'L'},
    {8,4,'L'},{9,4,'L'},{10,4,'L'},{11,4,'C'},{12,4,'C'},
    {5,3,'E'},{5,4,'E'},{9,3,'E'},{9,4,'E'},
    {7,4,'N'},{8,4,'N'},
    {4,6,'B'},{5,6,'C'},{6,6,'C'},{7,6,'C'},{8,6,'C'},{9,6,'C'},{10,6,'C'},{11,6,'B'},
    {3,7,'B'},{4,7,'L'},{5,7,'L'},{6,7,'L'},{7,7,'L'},{8,7,'L'},{9,7,'L'},{10,7,'L'},{11,7,'B'},
    {3,8,'B'},{4,8,'L'},{5,8,'L'},{6,8,'L'},{7,8,'L'},{8,8,'L'},{9,8,'L'},{10,8,'L'},{11,8,'B'},
    {3,9,'B'},{4,9,'C'},{5,9,'C'},{6,9,'C'},{7,9,'C'},{8,9,'C'},{9,9,'C'},{10,9,'C'},{11,9,'B'},
    {4,10,'B'},{5,10,'B'},{6,10,'B'},{7,10,'B'},{8,10,'B'},{9,10,'B'},{10,10,'B'},
    {4,11,'C'},{5,11,'C'},{4,12,'C'},{5,12,'C'},{4,13,'B'},{5,13,'B'},
    {9,11,'C'},{10,11,'C'},{9,12,'C'},{10,12,'C'},{9,13,'B'},{10,13,'B'},
    {12,7,'T'},{13,6,'T'},{13,5,'T'},{12,5,'T'},
};
#define PUPPY_COUNT (int)(sizeof(PUPPY_PIXELS)/sizeof(PUPPY_PIXELS[0]))

/* 少年/青年/成年（更宽，蓝项圈） */
static const PixelDef TEEN_PIXELS[] = {
    {1,0,'P'},{2,0,'P'},{1,1,'B'},{2,1,'B'},
    {12,0,'P'},{13,0,'P'},{12,1,'B'},{13,1,'B'},
    {3,1,'B'},{4,1,'B'},{5,1,'B'},{6,1,'B'},{7,1,'B'},{8,1,'B'},{9,1,'B'},{10,1,'B'},{11,1,'B'},
    {1,2,'B'},{14,2,'B'},{1,3,'B'},{14,3,'B'},{1,4,'B'},{14,4,'B'},
    {2,5,'B'},{3,5,'B'},{4,5,'B'},{5,5,'B'},{6,5,'B'},
    {7,5,'B'},{8,5,'B'},{9,5,'B'},{10,5,'B'},{11,5,'B'},{12,5,'B'},{13,5,'B'},
    {2,2,'C'},{3,2,'C'},{4,2,'C'},{5,2,'C'},{6,2,'C'},{7,2,'C'},
    {8,2,'C'},{9,2,'C'},{10,2,'C'},{11,2,'C'},{12,2,'C'},{13,2,'C'},
    {2,3,'C'},{3,3,'L'},{4,3,'L'},{5,3,'L'},{6,3,'L'},{7,3,'L'},
    {8,3,'L'},{9,3,'L'},{10,3,'L'},{11,3,'L'},{12,3,'L'},{13,3,'C'},
    {2,4,'C'},{3,4,'L'},{4,4,'L'},{5,4,'L'},{6,4,'L'},{7,4,'L'},
    {8,4,'L'},{9,4,'L'},{10,4,'L'},{11,4,'L'},{12,4,'L'},{13,4,'C'},
    {4,3,'E'},{4,4,'E'},{10,3,'E'},{10,4,'E'},
    {7,4,'N'},{8,4,'N'},
    {4,5,'K'},{5,5,'K'},{6,5,'K'},{7,5,'K'},{8,5,'K'},{9,5,'K'},{10,5,'K'},
    {3,6,'B'},{4,6,'C'},{5,6,'C'},{6,6,'C'},{7,6,'C'},{8,6,'C'},{9,6,'C'},{10,6,'C'},{11,6,'C'},{12,6,'B'},
    {3,7,'B'},{4,7,'L'},{5,7,'L'},{6,7,'L'},{7,7,'L'},{8,7,'L'},{9,7,'L'},{10,7,'L'},{11,7,'L'},{12,7,'B'},
    {3,8,'B'},{4,8,'L'},{5,8,'L'},{6,8,'L'},{7,8,'L'},{8,8,'L'},{9,8,'L'},{10,8,'L'},{11,8,'L'},{12,8,'B'},
    {3,9,'B'},{4,9,'C'},{5,9,'C'},{6,9,'C'},{7,9,'C'},{8,9,'C'},{9,9,'C'},{10,9,'C'},{11,9,'C'},{12,9,'B'},
    {4,10,'B'},{5,10,'B'},{6,10,'B'},{7,10,'B'},{8,10,'B'},{9,10,'B'},{10,10,'B'},{11,10,'B'},
    {4,11,'C'},{5,11,'C'},{4,12,'C'},{5,12,'C'},{4,13,'B'},{5,13,'B'},
    {9,11,'C'},{10,11,'C'},{9,12,'C'},{10,12,'C'},{9,13,'B'},{10,13,'B'},
    {13,7,'T'},{14,6,'T'},{14,5,'T'},{13,5,'T'},
};
#define TEEN_COUNT (int)(sizeof(TEEN_PIXELS)/sizeof(TEEN_PIXELS[0]))

/* =========================================================
   绘制像素狗
   ========================================================= */

static void _draw_dog(lv_obj_t* canvas, PetStage stage, PetMood mood,
                      lv_color_t bg, int anim_frame)
{
    /* 升阶闪光：白色填充 */
    const PetState* st = pet_get_state();
    if (st->stage_up_flash && (st->stage_up_ticks % 2 == 0)) {
        lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        return;
    }

    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);

    /* 蛋阶段 */
    if (stage == PET_STAGE_EGG) {
        _draw_egg(canvas, bg, anim_frame);
        return;
    }

    /* 狗阶段颜色 */
    lv_color_t col_body, col_light, col_outline, col_tail;
    switch (stage) {
        case PET_STAGE_PUPPY:
            col_body    = lv_color_hex(0xD2955A);
            col_light   = lv_color_hex(0xF0C898);
            col_outline = lv_color_hex(0x3A2010);
            col_tail    = lv_color_hex(0xC08040);
            break;
        case PET_STAGE_TEEN:
            col_body    = lv_color_hex(0xA07040);
            col_light   = lv_color_hex(0xD4A870);
            col_outline = lv_color_hex(0x2A1008);
            col_tail    = lv_color_hex(0x906030);
            break;
        case PET_STAGE_YOUNG:
            col_body    = lv_color_hex(0x6B4226);
            col_light   = lv_color_hex(0xB07840);
            col_outline = lv_color_hex(0x1A0A04);
            col_tail    = lv_color_hex(0x5A3218);
            break;
        default: /* ADULT */
            col_body    = lv_color_hex(0x3D1F0A);
            col_light   = lv_color_hex(0x7A4020);
            col_outline = lv_color_hex(0x0A0400);
            col_tail    = lv_color_hex(0x2D1206);
            break;
    }

    lv_color_t col_eye  = lv_color_hex(0xFFFFFF);
    lv_color_t col_nose = lv_color_hex(0x1A0A00);
    lv_color_t col_pink = lv_color_hex(0xFF9999);
    lv_color_t col_blue = lv_color_hex(0x3399FF);

    const PixelDef* pixels = (stage == PET_STAGE_PUPPY) ? PUPPY_PIXELS : TEEN_PIXELS;
    int count = (stage == PET_STAGE_PUPPY) ? PUPPY_COUNT : TEEN_COUNT;

    for (int i = 0; i < count; i++) {
        lv_color_t col;
        switch (pixels[i].c) {
            case 'B': col = col_outline; break;
            case 'C': col = col_body;    break;
            case 'L': col = col_light;   break;
            case 'E': col = col_eye;     break;
            case 'N': col = col_nose;    break;
            case 'P': col = col_pink;    break;
            case 'T': col = col_tail;    break;
            case 'K': col = col_blue;    break;
            default:  col = bg;          break;
        }
        _px(canvas, pixels[i].x, pixels[i].y, col);
    }

    /* 开心/升阶：星星 */
    if (mood == PET_MOOD_HAPPY || mood == PET_MOOD_EXCITED) {
        lv_color_t star = lv_color_hex(0xFFDD00);
        _px(canvas, 14, 0, star);
        _px(canvas, 15, 1, star);
        _px(canvas, 14, 2, star);
        if (mood == PET_MOOD_EXCITED) {
            _px(canvas, 0, 0, star);
            _px(canvas, 1, 1, star);
        }
    }

    /* 困了：ZZZ */
    if (mood == PET_MOOD_SLEEPY) {
        lv_color_t zzz = lv_color_hex(0xAAAAAA);
        _px(canvas, 13, 0, zzz);
        _px(canvas, 14, 1, zzz);
        _px(canvas, 15, 2, zzz);
    }
}

/* =========================================================
   背景
   ========================================================= */

static void _draw_background(bool night)
{
    g_is_night = night;
    if (!g_bg_obj) return;
    lv_obj_set_style_bg_color(g_bg_obj, night ? COL_NIGHT_SKY : COL_DAY_SKY, 0);

    lv_obj_t* ground = lv_obj_get_child(g_bg_obj, 0);
    if (ground) lv_obj_set_style_bg_color(ground, night ? COL_NIGHT_GROUND : COL_DAY_GROUND, 0);

    lv_obj_t* moon = lv_obj_get_child(g_bg_obj, 1);
    if (moon) lv_obj_set_style_opa(moon, night ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

/* =========================================================
   状态栏刷新
   ========================================================= */

static void _update_status_bar(const PetState* st)
{
    uint32_t xp_min, xp_max;
    const char* stage_name;
    switch (st->saved.stage) {
        case PET_STAGE_EGG:   xp_min=0;   xp_max=PET_XP_EGG_MAX;    stage_name="蛋"; break;
        case PET_STAGE_PUPPY: xp_min=15;  xp_max=PET_XP_STAGE1_MAX; stage_name="幼犬"; break;
        case PET_STAGE_TEEN:  xp_min=75;  xp_max=PET_XP_STAGE2_MAX; stage_name="少年"; break;
        case PET_STAGE_YOUNG: xp_min=125; xp_max=PET_XP_STAGE3_MAX; stage_name="青年"; break;
        default:              xp_min=175; xp_max=270;                stage_name="成年"; break;
    }
    uint32_t cur   = (st->saved.total_xp > xp_min) ? (st->saved.total_xp - xp_min) : 0;
    uint32_t range = xp_max - xp_min;

    char buf[48];
    snprintf(buf, sizeof(buf), "%s  %lu/%lu", stage_name, cur, range);
    if (g_lv_label) lv_label_set_text(g_lv_label, buf);

    if (g_xp_fill && range > 0) {
        int w = (int)(((uint64_t)cur * 160) / range);
        if (w > 160) w = 160;
        lv_obj_set_width(g_xp_fill, w);
    }
}

/* =========================================================
   消息定时器
   ========================================================= */

static void _msg_timer_cb(TimerHandle_t t)
{
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

    /* 背景区 */
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
    lv_obj_set_size(ground, SCREEN_W, 28);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ground, COL_DAY_GROUND, 0);
    lv_obj_set_style_border_width(ground, 0, 0);
    lv_obj_set_style_radius(ground, 0, 0);

    /* 月亮 */
    lv_obj_t* moon = lv_obj_create(g_bg_obj);
    lv_obj_set_size(moon, 30, 30);
    lv_obj_set_pos(moon, 180, 10);
    lv_obj_set_style_radius(moon, 15, 0);
    lv_obj_set_style_bg_color(moon, COL_NIGHT_MOON, 0);
    lv_obj_set_style_border_width(moon, 0, 0);
    lv_obj_set_style_opa(moon, LV_OPA_TRANSP, 0);

    /* 状态栏 */
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
    lv_label_set_text(g_lv_label, "蛋  0/15");

    /* XP 条背景 */
    lv_obj_t* xp_bg = lv_obj_create(g_status_bar);
    lv_obj_set_size(xp_bg, 160, 5);
    lv_obj_align(xp_bg, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(xp_bg, COL_XP_BAR_BG, 0);
    lv_obj_set_style_border_width(xp_bg, 0, 0);
    lv_obj_set_style_radius(xp_bg, 2, 0);

    /* XP 条填充 */
    g_xp_fill = lv_obj_create(g_status_bar);
    lv_obj_set_size(g_xp_fill, 0, 5);
    lv_obj_align(g_xp_fill, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(g_xp_fill, COL_XP_BAR_FG, 0);
    lv_obj_set_style_border_width(g_xp_fill, 0, 0);
    lv_obj_set_style_radius(g_xp_fill, 2, 0);

    /* 宠物画布 */
    g_dog_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(g_dog_canvas, g_dog_buf,
                         DOG_CANVAS_W, DOG_CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(g_dog_canvas, DOG_X, DOG_Y);
    lv_canvas_fill_bg(g_dog_canvas, COL_DAY_SKY, LV_OPA_COVER);

    /* 文字区 */
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

    ESP_LOGI(TAG, "pet_display_init OK (LVGL9 v3)");
}

/* =========================================================
   每帧刷新（200ms调用一次）
   ========================================================= */

void pet_display_update(void)
{
    /* 1. 把所有层按顺序移到最前面，覆盖 xiaozhi 原有 UI */
    if (g_bg_obj)     lv_obj_move_foreground(g_bg_obj);
    if (g_dog_canvas) lv_obj_move_foreground(g_dog_canvas);
    if (g_status_bar) lv_obj_move_foreground(g_status_bar);
    if (g_text_area)  lv_obj_move_foreground(g_text_area);

    if (!g_dog_canvas) return;

    /* 2. 动画帧计数（每3次 = 600ms 切换） */
    g_frame_counter++;
    if (g_frame_counter >= 3) {
        g_frame_counter = 0;
        g_anim_frame = g_anim_frame ? 0 : 1;
    }

    const PetState* st = pet_get_state();

    /* 3. 背景切换（日/夜） */
    bool night = (pet_get_time_period() == TIME_NIGHT);
    if (night != g_is_night) _draw_background(night);
    lv_color_t bg = night ? COL_NIGHT_SKY : COL_DAY_SKY;

    /* 4. 心情超时复位 */
    if (st->happy_ticks_left == 0 && st->mood == PET_MOOD_HAPPY) {
        pet_set_mood(PET_MOOD_IDLE);
    }

    /* 5. 升阶闪光倒计时 */
    if (st->stage_up_flash) {
        PetState* mst = (PetState*)st;  // 需要修改，cast掉const
        if (mst->stage_up_ticks > 0) {
            mst->stage_up_ticks--;
        } else {
            mst->stage_up_flash = false;
            mst->mood = PET_MOOD_IDLE;
        }
    }

    /* 6. 弹跳动画：canvas Y 位置偏移
       蛋：横向摇摆（X偏移）
       幼犬/少年：轻弹跳（Y偏移3px）
       青年：中弹跳（Y偏移4px）
       成年：重踏步（Y偏移2px，更慢感觉） */
    int x_off = 0, y_off = 0;
    switch (st->saved.stage) {
        case PET_STAGE_EGG:
            x_off = (g_anim_frame == 1) ? 3 : -3;  // 左右摇摆
            break;
        case PET_STAGE_PUPPY:
        case PET_STAGE_TEEN:
            y_off = (g_anim_frame == 1) ? -3 : 0;  // 轻弹跳
            break;
        case PET_STAGE_YOUNG:
            y_off = (g_anim_frame == 1) ? -4 : 0;  // 中弹跳
            break;
        case PET_STAGE_ADULT:
            y_off = (g_anim_frame == 1) ? -2 : 0;  // 威风踏步
            break;
        default: break;
    }
    /* 开心时额外弹跳 */
    if (st->mood == PET_MOOD_HAPPY || st->mood == PET_MOOD_EXCITED) {
        y_off -= 2;
    }
    lv_obj_set_pos(g_dog_canvas, DOG_X + x_off, DOG_Y + y_off);

    /* 7. 绘制宠物 */
    _draw_dog(g_dog_canvas, st->saved.stage, st->mood, bg, g_anim_frame);

    /* 8. 刷新状态栏 */
    _update_status_bar(st);
}

/* =========================================================
   任务完成提示
   ========================================================= */

void pet_display_show_task_done(TaskType task, uint32_t xp_gained)
{
    char msg[80];
    snprintf(msg, sizeof(msg), "太棒了！完成%s，获得%lu点能量！",
             pet_task_name(task), xp_gained);
    pet_display_show_message(msg, 4000);
}

/* =========================================================
   临时消息
   ========================================================= */

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
