/**
 * aqi_pet_display.c  —  阿奇宠物养成：LVGL 显示层
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet_display.c
 *
 * 屏幕布局（240×240）：
 * ┌─────────────────────────┐
 * │  Lv:★★★☆  XP 450/600   │  ← 30px 状态栏
 * │                         │
 * │    [背景：白天/夜晚]     │
 * │                         │
 * │       [阿奇 96×96]      │  ← 像素风格狗（居中）
 * │                         │
 * │   [任务提示 / 对话文字]  │  ← 45px 文字区
 * └─────────────────────────┘
 *
 * 像素狗绘制说明：
 *   用 LVGL lv_canvas + lv_draw_rect 在小画布上画"像素块"，
 *   每个"像素"= 6×6 真实像素，狗的画幅约 16×16 像素 = 96×96 px。
 *   4 个进化阶段通过改变体型和颜色体现。
 *
 * 依赖：LVGL 8.x
 */

#include "aqi_pet.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char* TAG = "AqiDisp";

/* =========================================================
   布局常量
   ========================================================= */

#define SCREEN_W     240
#define SCREEN_H     240
#define STATUS_H      30   // 顶部状态栏高度
#define TEXT_H        45   // 底部文字区高度
#define PET_AREA_Y   (STATUS_H)
#define PET_AREA_H   (SCREEN_H - STATUS_H - TEXT_H)  // 165px

/* 像素狗画布大小 */
#define DOG_CANVAS_W  96
#define DOG_CANVAS_H  96
#define DOG_PX         6   // 每个"像素"的实际像素数

/* 狗的居中位置 */
#define DOG_X   ((SCREEN_W - DOG_CANVAS_W) / 2)
#define DOG_Y   (PET_AREA_Y + (PET_AREA_H - DOG_CANVAS_H) / 2)

/* =========================================================
   颜色定义（白天 / 夜晚）
   ========================================================= */

// 白天背景
#define COL_DAY_SKY     lv_color_hex(0x87CEEB)  // 天蓝
#define COL_DAY_GROUND  lv_color_hex(0x90EE90)  // 草绿
// 夜晚背景
#define COL_NIGHT_SKY   lv_color_hex(0x1A1A3A)  // 深蓝紫
#define COL_NIGHT_MOON  lv_color_hex(0xFFFACD)  // 月黄
#define COL_NIGHT_STAR  lv_color_hex(0xFFFFFF)  // 白星
#define COL_NIGHT_GROUND lv_color_hex(0x2E4A2E) // 暗绿草地

// 状态栏
#define COL_STATUS_BG   lv_color_hex(0x2D2D2D)
#define COL_STATUS_TEXT lv_color_hex(0xFFFFFF)
#define COL_XP_BAR_BG   lv_color_hex(0x555555)
#define COL_XP_BAR_FG   lv_color_hex(0x00D4AA)  // 青绿 XP 条

// 文字区
#define COL_TEXT_BG     lv_color_hex(0x1C1C2E)
#define COL_TEXT_FG     lv_color_hex(0xF0F0F0)

// 狗的颜色（按进化阶段变化）
static const lv_color_t DOG_BODY_COLOR[4] = {
    /* PUPPY */ {.full = 0},   // 初始化后赋值
    /* TEEN  */ {.full = 0},
    /* YOUNG */ {.full = 0},
    /* ADULT */ {.full = 0},
};

/* =========================================================
   LVGL 对象
   ========================================================= */

static lv_obj_t* g_screen       = NULL;
static lv_obj_t* g_bg_obj       = NULL;  // 背景容器
static lv_obj_t* g_status_bar   = NULL;  // 顶部状态栏
static lv_obj_t* g_xp_bar       = NULL;  // XP 进度条
static lv_obj_t* g_lv_label     = NULL;  // "Lv:★" 标签
static lv_obj_t* g_dog_canvas   = NULL;  // 宠物画布
static lv_obj_t* g_text_area    = NULL;  // 底部文字区
static lv_obj_t* g_text_label   = NULL;  // 文字标签

/* 狗画布像素缓冲区（RGB565，96×96） */
static lv_color_t g_dog_buf[DOG_CANVAS_W * DOG_CANVAS_H];

/* 消息自动消失定时器 */
static TimerHandle_t g_msg_timer  = NULL;
static char g_default_msg[64]    = "汪汪！今天要做什么呢？";

/* =========================================================
   辅助：在画布上画一个"像素块"
   ========================================================= */

static void draw_pixel(lv_obj_t* canvas, int px, int py, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color   = color;
    dsc.radius     = 0;
    dsc.border_width = 0;
    lv_canvas_draw_rect(canvas,
                        px * DOG_PX, py * DOG_PX,
                        DOG_PX, DOG_PX,
                        &dsc);
}

/* =========================================================
   像素狗绘制
   =========================================================
   16×16 格点坐标系（左上角 0,0）
   画布 96×96 px（每格 6×6 px）

   字母含义：
     B = 黑色轮廓
     C = 身体主色（按阶段变化）
     L = 浅色（腹部/面部高光）
     E = 眼睛（白）
     N = 鼻子（黑）
     P = 粉（舌头/耳内）
     T = 尾巴
     . = 透明（背景色）
   ========================================================= */

/* 透明像素用背景色填充，此函数需知道背景色 */
typedef struct { int x; int y; char c; } PixelDef;

/* ---- 幼犬（阶段0）：小圆头大耳朵 ---- */
static const PixelDef PUPPY_PIXELS[] = {
    // 耳朵（左右）
    {2,1,'B'},{3,1,'B'},{3,0,'P'},{2,0,'P'},
    {11,1,'B'},{12,1,'B'},{12,0,'P'},{11,0,'P'},
    // 头部外轮廓
    {4,1,'B'},{5,1,'B'},{6,1,'B'},{7,1,'B'},{8,1,'B'},{9,1,'B'},{10,1,'B'},
    {2,2,'B'},{13,2,'B'},
    {2,3,'B'},{13,3,'B'},
    {2,4,'B'},{13,4,'B'},
    {3,5,'B'},{4,5,'B'},{5,5,'B'},{6,5,'B'},{7,5,'B'},
    {8,5,'B'},{9,5,'B'},{10,5,'B'},{11,5,'B'},{12,5,'B'},
    // 头部填充
    {3,2,'C'},{4,2,'C'},{5,2,'C'},{6,2,'C'},{7,2,'C'},
    {8,2,'C'},{9,2,'C'},{10,2,'C'},{11,2,'C'},{12,2,'C'},
    {3,3,'C'},{4,3,'L'},{5,3,'L'},{6,3,'L'},{7,3,'L'},
    {8,3,'L'},{9,3,'L'},{10,3,'L'},{11,3,'C'},{12,3,'C'},
    {3,4,'C'},{4,4,'L'},{5,4,'L'},{6,4,'L'},{7,4,'L'},
    {8,4,'L'},{9,4,'L'},{10,4,'L'},{11,4,'C'},{12,4,'C'},
    // 眼睛
    {5,3,'E'},{5,4,'E'},{9,3,'E'},{9,4,'E'},
    // 鼻子
    {7,4,'N'},{8,4,'N'},
    // 嘴/舌
    {7,5,'P'},{8,5,'P'},
    // 身体
    {4,6,'B'},{5,6,'C'},{6,6,'C'},{7,6,'C'},{8,6,'C'},{9,6,'C'},{10,6,'C'},{11,6,'B'},
    {3,7,'B'},{4,7,'C'},{5,7,'L'},{6,7,'L'},{7,7,'L'},{8,7,'L'},{9,7,'L'},{10,7,'C'},{11,7,'B'},{12,7,'B'},
    {3,8,'B'},{4,8,'C'},{5,8,'L'},{6,8,'L'},{7,8,'L'},{8,8,'L'},{9,8,'L'},{10,8,'C'},{11,8,'C'},{12,8,'B'},
    {3,9,'B'},{4,9,'C'},{5,9,'C'},{6,9,'C'},{7,9,'C'},{8,9,'C'},{9,9,'C'},{10,9,'C'},{11,9,'C'},{12,9,'B'},
    {4,10,'B'},{5,10,'B'},{6,10,'B'},{7,10,'B'},{8,10,'B'},{9,10,'B'},{10,10,'B'},{11,10,'B'},
    // 四肢
    {4,11,'B'},{5,11,'C'},{8,11,'C'},{9,11,'B'},{10,11,'C'},{11,11,'B'},
    {4,12,'B'},{5,12,'C'},{8,12,'C'},{9,12,'B'},{10,12,'C'},{11,12,'B'},
    {4,13,'B'},{5,13,'B'},{8,13,'B'},{9,13,'B'},{10,13,'B'},{11,13,'B'},
    // 尾巴（右侧）
    {12,8,'T'},{13,7,'T'},{13,6,'T'},{12,6,'T'},
};
#define PUPPY_PIXEL_COUNT (sizeof(PUPPY_PIXELS)/sizeof(PUPPY_PIXELS[0]))

/* ---- 少年犬（阶段1）：比幼犬宽一格，加项圈 ---- */
static const PixelDef TEEN_PIXELS[] = {
    // 耳朵
    {1,1,'B'},{2,1,'B'},{2,0,'P'},{1,0,'P'},
    {12,1,'B'},{13,1,'B'},{13,0,'P'},{12,0,'P'},
    // 头部
    {3,1,'B'},{4,1,'B'},{5,1,'B'},{6,1,'B'},{7,1,'B'},{8,1,'B'},{9,1,'B'},{10,1,'B'},{11,1,'B'},
    {1,2,'B'},{14,2,'B'},
    {1,3,'B'},{14,3,'B'},
    {1,4,'B'},{14,4,'B'},
    {2,5,'B'},{3,5,'B'},{4,5,'B'},{5,5,'B'},{6,5,'B'},
    {7,5,'B'},{8,5,'B'},{9,5,'B'},{10,5,'B'},{11,5,'B'},{12,5,'B'},{13,5,'B'},
    // 头部填充
    {2,2,'C'},{3,2,'C'},{4,2,'C'},{5,2,'C'},{6,2,'C'},{7,2,'C'},
    {8,2,'C'},{9,2,'C'},{10,2,'C'},{11,2,'C'},{12,2,'C'},{13,2,'C'},
    {2,3,'C'},{3,3,'L'},{4,3,'L'},{5,3,'L'},{6,3,'L'},{7,3,'L'},
    {8,3,'L'},{9,3,'L'},{10,3,'L'},{11,3,'L'},{12,3,'L'},{13,3,'C'},
    {2,4,'C'},{3,4,'L'},{4,4,'L'},{5,4,'L'},{6,4,'L'},{7,4,'L'},
    {8,4,'L'},{9,4,'L'},{10,4,'L'},{11,4,'L'},{12,4,'L'},{13,4,'C'},
    // 眼睛
    {4,3,'E'},{4,4,'E'},{10,3,'E'},{10,4,'E'},
    // 鼻
    {7,4,'N'},{8,4,'N'},
    // 项圈（蓝色）——用 'K' 代表蓝色
    {4,5,'K'},{5,5,'K'},{6,5,'K'},{7,5,'K'},{8,5,'K'},{9,5,'K'},{10,5,'K'},
    // 身体
    {3,6,'B'},{4,6,'C'},{5,6,'C'},{6,6,'C'},{7,6,'C'},{8,6,'C'},{9,6,'C'},{10,6,'C'},{11,6,'C'},{12,6,'B'},
    {3,7,'B'},{4,7,'L'},{5,7,'L'},{6,7,'L'},{7,7,'L'},{8,7,'L'},{9,7,'L'},{10,7,'L'},{11,7,'L'},{12,7,'B'},
    {3,8,'B'},{4,8,'L'},{5,8,'L'},{6,8,'L'},{7,8,'L'},{8,8,'L'},{9,8,'L'},{10,8,'L'},{11,8,'L'},{12,8,'B'},
    {3,9,'B'},{4,9,'C'},{5,9,'C'},{6,9,'C'},{7,9,'C'},{8,9,'C'},{9,9,'C'},{10,9,'C'},{11,9,'C'},{12,9,'B'},
    {4,10,'B'},{5,10,'B'},{6,10,'B'},{7,10,'B'},{8,10,'B'},{9,10,'B'},{10,10,'B'},{11,10,'B'},
    // 四肢
    {4,11,'C'},{5,11,'C'},{4,12,'C'},{5,12,'C'},{4,13,'B'},{5,13,'B'},
    {9,11,'C'},{10,11,'C'},{9,12,'C'},{10,12,'C'},{9,13,'B'},{10,13,'B'},
    // 尾巴
    {12,7,'T'},{13,6,'T'},{13,5,'T'},{12,5,'T'},
};
#define TEEN_PIXEL_COUNT (sizeof(TEEN_PIXELS)/sizeof(TEEN_PIXELS[0]))

/* 青年/成年共用稍大的狗型（简化，实际可进一步细化） */
/* 这里用 TEEN 的像素放大1格作为示例，产品迭代时可替换 */

/* =========================================================
   绘制单只狗
   ========================================================= */

static void _draw_dog(lv_obj_t* canvas, PetStage stage, PetMood mood,
                      lv_color_t bg_color)
{
    /* 先清空画布为背景色 */
    lv_canvas_fill_bg(canvas, bg_color, LV_OPA_COVER);

    /* 按阶段选颜色 */
    lv_color_t col_body, col_light, col_outline, col_tail;
    switch (stage) {
        case PET_STAGE_PUPPY:
            col_body    = lv_color_hex(0xD2955A); // 浅棕
            col_light   = lv_color_hex(0xF0C898); // 浅米
            col_outline = lv_color_hex(0x3A2010); // 深棕黑
            col_tail    = lv_color_hex(0xC08040); // 棕
            break;
        case PET_STAGE_TEEN:
            col_body    = lv_color_hex(0xA07040); // 中棕
            col_light   = lv_color_hex(0xD4A870); // 浅棕
            col_outline = lv_color_hex(0x2A1008); // 黑棕
            col_tail    = lv_color_hex(0x906030); // 深棕
            break;
        case PET_STAGE_YOUNG:
            col_body    = lv_color_hex(0x6B4226); // 深棕
            col_light   = lv_color_hex(0xB07840); // 中棕
            col_outline = lv_color_hex(0x1A0A04); // 近黑
            col_tail    = lv_color_hex(0x5A3218); // 深棕
            break;
        case PET_STAGE_ADULT:
        default:
            col_body    = lv_color_hex(0x3D1F0A); // 黑棕
            col_light   = lv_color_hex(0x7A4020); // 棕
            col_outline = lv_color_hex(0x0A0400); // 黑
            col_tail    = lv_color_hex(0x2D1206); // 极深棕
            break;
    }

    lv_color_t col_eye   = lv_color_hex(0xFFFFFF);
    lv_color_t col_nose  = lv_color_hex(0x1A0A00);
    lv_color_t col_pink  = lv_color_hex(0xFF9999);
    lv_color_t col_blue  = lv_color_hex(0x3399FF); // 项圈

    const PixelDef* pixels;
    int count;
    if (stage == PET_STAGE_PUPPY) {
        pixels = PUPPY_PIXELS;
        count  = PUPPY_PIXEL_COUNT;
    } else {
        pixels = TEEN_PIXELS;
        count  = TEEN_PIXEL_COUNT;
    }

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
            default:  col = bg_color;    break;
        }
        draw_pixel(canvas, pixels[i].x, pixels[i].y, col);
    }

    /* 开心动画：给狗加一颗小星星 */
    if (mood == PET_MOOD_HAPPY || mood == PET_MOOD_EXCITED) {
        lv_color_t star = lv_color_hex(0xFFDD00);
        draw_pixel(canvas, 14, 0, star);
        draw_pixel(canvas, 15, 1, star);
        draw_pixel(canvas, 14, 2, star);
        draw_pixel(canvas, 13, 1, star);
    }

    /* 困了：画 ZZZ */
    if (mood == PET_MOOD_SLEEPY) {
        lv_color_t zzz = lv_color_hex(0xAAAAAA);
        draw_pixel(canvas, 13, 0, zzz);
        draw_pixel(canvas, 14, 1, zzz);
        draw_pixel(canvas, 15, 2, zzz);
    }
}

/* =========================================================
   背景绘制
   ========================================================= */

static bool g_is_night = false;

static void _draw_background(bool night)
{
    g_is_night = night;
    if (!g_bg_obj) return;

    /* 天空 */
    lv_color_t sky = night ? COL_NIGHT_SKY : COL_DAY_SKY;
    lv_obj_set_style_bg_color(g_bg_obj, sky, 0);

    /* 地面条（下 40px） */
    /* 已在 init 时创建，这里通过子对象更新 */
    lv_obj_t* ground = lv_obj_get_child(g_bg_obj, 0);
    if (ground) {
        lv_color_t gcol = night ? COL_NIGHT_GROUND : COL_DAY_GROUND;
        lv_obj_set_style_bg_color(ground, gcol, 0);
    }

    /* 月亮（夜晚才显示） */
    lv_obj_t* moon = lv_obj_get_child(g_bg_obj, 1);
    if (moon) {
        lv_obj_set_style_opa(moon, night ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
}

/* =========================================================
   状态栏刷新
   ========================================================= */

static lv_obj_t* g_xp_fill = NULL;

static void _update_status_bar(const PetState* st)
{
    /* 阶段星星 */
    int stars     = (int)st->saved.stage + 1;  // 1~4 颗星
    char star_str[16];
    int  i = 0;
    for (; i < stars && i < 4; i++) star_str[i] = '*';
    star_str[i] = '\0';

    /* XP 范围 */
    uint32_t xp_min, xp_max;
    switch (st->saved.stage) {
        case PET_STAGE_PUPPY: xp_min=0;   xp_max=PET_XP_STAGE1_MAX; break;
        case PET_STAGE_TEEN:  xp_min=100; xp_max=PET_XP_STAGE2_MAX; break;
        case PET_STAGE_YOUNG: xp_min=300; xp_max=PET_XP_STAGE3_MAX; break;
        default:              xp_min=600; xp_max=999; break;
    }
    uint32_t cur_xp = st->saved.total_xp > xp_min ? st->saved.total_xp - xp_min : 0;
    uint32_t range  = xp_max - xp_min;

    char label_text[64];
    snprintf(label_text, sizeof(label_text),
             "Lv%d %s  %lu/%lu XP", (int)st->saved.stage + 1, star_str, cur_xp, range);

    if (g_lv_label) lv_label_set_text(g_lv_label, label_text);

    /* XP 进度条宽度 */
    if (g_xp_fill) {
        int bar_w = (int)(((uint64_t)cur_xp * 160) / (range > 0 ? range : 1));
        if (bar_w > 160) bar_w = 160;
        lv_obj_set_width(g_xp_fill, bar_w);
    }
}

/* =========================================================
   消息定时器（自动清除临时消息）
   ========================================================= */

static void _msg_timer_cb(TimerHandle_t xTimer)
{
    if (g_text_label) {
        lv_label_set_text(g_text_label, g_default_msg);
    }
}

/* =========================================================
   初始化
   ========================================================= */

void pet_display_init(lv_obj_t* parent)
{
    if (!parent) parent = lv_scr_act();
    g_screen = parent;

    lv_obj_set_style_bg_color(parent, lv_color_hex(0x87CEEB), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* ---- 背景区域 ---- */
    g_bg_obj = lv_obj_create(parent);
    lv_obj_set_size(g_bg_obj, SCREEN_W, SCREEN_H - STATUS_H - TEXT_H);
    lv_obj_set_pos(g_bg_obj, 0, STATUS_H);
    lv_obj_set_style_border_width(g_bg_obj, 0, 0);
    lv_obj_set_style_pad_all(g_bg_obj, 0, 0);
    lv_obj_set_style_radius(g_bg_obj, 0, 0);
    lv_obj_set_style_bg_color(g_bg_obj, COL_DAY_SKY, 0);
    lv_obj_set_scroll_dir(g_bg_obj, LV_DIR_NONE);

    /* 地面 */
    lv_obj_t* ground = lv_obj_create(g_bg_obj);
    lv_obj_set_size(ground, SCREEN_W, 30);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ground, COL_DAY_GROUND, 0);
    lv_obj_set_style_border_width(ground, 0, 0);
    lv_obj_set_style_radius(ground, 0, 0);

    /* 月亮（默认不可见） */
    lv_obj_t* moon = lv_obj_create(g_bg_obj);
    lv_obj_set_size(moon, 30, 30);
    lv_obj_set_pos(moon, 180, 10);
    lv_obj_set_style_radius(moon, 15, 0);
    lv_obj_set_style_bg_color(moon, COL_NIGHT_MOON, 0);
    lv_obj_set_style_border_width(moon, 0, 0);
    lv_obj_set_style_opa(moon, LV_OPA_TRANSP, 0);

    /* ---- 状态栏 ---- */
    g_status_bar = lv_obj_create(parent);
    lv_obj_set_size(g_status_bar, SCREEN_W, STATUS_H);
    lv_obj_set_pos(g_status_bar, 0, 0);
    lv_obj_set_style_bg_color(g_status_bar, COL_STATUS_BG, 0);
    lv_obj_set_style_border_width(g_status_bar, 0, 0);
    lv_obj_set_style_radius(g_status_bar, 0, 0);
    lv_obj_set_style_pad_all(g_status_bar, 2, 0);

    /* 文字标签 */
    g_lv_label = lv_label_create(g_status_bar);
    lv_obj_set_style_text_color(g_lv_label, COL_STATUS_TEXT, 0);
    lv_obj_set_style_text_font(g_lv_label, &lv_font_montserrat_12, 0);
    lv_obj_align(g_lv_label, LV_ALIGN_LEFT_MID, 4, -5);
    lv_label_set_text(g_lv_label, "Lv1 *  0/100 XP");

    /* XP 进度条背景 */
    lv_obj_t* xp_bg = lv_obj_create(g_status_bar);
    lv_obj_set_size(xp_bg, 160, 5);
    lv_obj_align(xp_bg, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(xp_bg, COL_XP_BAR_BG, 0);
    lv_obj_set_style_border_width(xp_bg, 0, 0);
    lv_obj_set_style_radius(xp_bg, 2, 0);

    /* XP 进度条填充 */
    g_xp_fill = lv_obj_create(g_status_bar);
    lv_obj_set_size(g_xp_fill, 0, 5);
    lv_obj_align(g_xp_fill, LV_ALIGN_LEFT_MID, 4, 7);
    lv_obj_set_style_bg_color(g_xp_fill, COL_XP_BAR_FG, 0);
    lv_obj_set_style_border_width(g_xp_fill, 0, 0);
    lv_obj_set_style_radius(g_xp_fill, 2, 0);

    /* ---- 宠物画布 ---- */
    g_dog_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(g_dog_canvas, g_dog_buf,
                         DOG_CANVAS_W, DOG_CANVAS_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(g_dog_canvas, DOG_X, DOG_Y);
    lv_canvas_fill_bg(g_dog_canvas, COL_DAY_SKY, LV_OPA_COVER);

    /* ---- 文字区 ---- */
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

    /* 消息定时器（单次，初始不启动） */
    g_msg_timer = xTimerCreate("pet_msg", pdMS_TO_TICKS(3000),
                               pdFALSE, NULL, _msg_timer_cb);

    ESP_LOGI(TAG, "pet_display_init OK");
}

/* =========================================================
   每帧刷新
   ========================================================= */

void pet_display_update(void)
{
    if (!g_dog_canvas) return;

    const PetState* st = pet_get_state();

    /* 判断白天/夜晚 */
    TimePeriod tp = pet_get_time_period();
    bool night = (tp == TIME_NIGHT);
    if (night != g_is_night) {
        _draw_background(night);
    }

    /* 背景色（传给狗的透明区域） */
    lv_color_t bg = night ? COL_NIGHT_SKY : COL_DAY_SKY;

    /* 心情开心倒计时 */
    if (st->happy_ticks_left == 0 && st->mood == PET_MOOD_HAPPY) {
        /* 时间到，恢复待机（但不能直接改 const 指针，通过函数） */
        pet_set_mood(PET_MOOD_IDLE);
    }

    /* 画狗 */
    _draw_dog(g_dog_canvas, st->saved.stage, st->mood, bg);

    /* 状态栏 */
    _update_status_bar(st);
}

/* =========================================================
   显示任务完成效果
   ========================================================= */

void pet_display_show_task_done(TaskType task, uint32_t xp_gained)
{
    char msg[80];
    snprintf(msg, sizeof(msg),
             "太棒了！完成「%s」，阿奇获得 %lu XP！",
             pet_task_name(task), xp_gained);
    pet_display_show_message(msg, 3000);
}

/* =========================================================
   临时提示文字
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
   对话模式切换
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
