/**
 * aqi_pet.h  —  阿奇宠物养成系统主头文件
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet.h
 *
 * 依赖：
 *   - ESP-IDF 5.x
 *   - LVGL 8.x
 *   - xiaozhi-esp32 框架
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   常量：经验值阈值
   ========================================================= */

#define PET_XP_STAGE1_MAX   100u   // 幼犬  0 ~ 99
#define PET_XP_STAGE2_MAX   300u   // 少年犬 100 ~ 299
#define PET_XP_STAGE3_MAX   600u   // 青年犬 300 ~ 599
                                    // 600+ = 成年犬

/* =========================================================
   常量：任务经验值奖励
   ========================================================= */

#define XP_FIRST_BOOT       5u     // 每日首次启动
#define XP_ENGLISH_WORD     15u    // 跟读英语单词
#define XP_ENGLISH_SENTENCE 20u    // 跟读英语短句
#define XP_POETRY           25u    // 背一句古诗
#define XP_BRUSH_TEETH      15u    // 刷牙打卡
#define XP_WASH_NOSE        15u    // 洗鼻子打卡

/* =========================================================
   时间节点（24h）
   ========================================================= */

#define GREET_MORNING_HOUR      6    // 06:00 早上好 触发窗口
#define GREET_NOON_HOUR        12    // 12:00 中午好
#define GREET_AFTERNOON_HOUR   14    // 14:00 下午好
#define GREET_NIGHT_HOUR       20    // 20:00 晚上好
#define SLEEP_REMIND_HOUR      21    // 21:30 睡前提醒
#define SLEEP_REMIND_MINUTE    30
#define HYGIENE_CHECK_HOUR     20    // 20:00 询问刷牙/洗鼻子
#define NIGHT_BG_START_HOUR    19    // 19:00 切换夜晚背景

/* =========================================================
   枚举类型
   ========================================================= */

/** 每日任务类型 */
typedef enum {
    TASK_FIRST_BOOT      = 0,  ///< 每日首次启动
    TASK_ENGLISH_WORD    = 1,  ///< 跟读英语单词
    TASK_ENGLISH_SENT    = 2,  ///< 跟读英语短句
    TASK_POETRY          = 3,  ///< 背古诗
    TASK_BRUSH_TEETH     = 4,  ///< 刷牙
    TASK_WASH_NOSE       = 5,  ///< 洗鼻子
    TASK_MAX             = 6
} TaskType;

/** 宠物心情（影响动画帧） */
typedef enum {
    PET_MOOD_IDLE     = 0,  ///< 待机
    PET_MOOD_HAPPY    = 1,  ///< 开心（完成任务后）
    PET_MOOD_EXCITED  = 2,  ///< 超级开心（升阶）
    PET_MOOD_SLEEPY   = 3,  ///< 困了（晚上提醒）
    PET_MOOD_TALKING  = 4,  ///< 对话中
} PetMood;

/** 宠物进化阶段 */
typedef enum {
    PET_STAGE_PUPPY   = 0,  ///< 幼犬   0~99 XP
    PET_STAGE_TEEN    = 1,  ///< 少年   100~299 XP
    PET_STAGE_YOUNG   = 2,  ///< 青年   300~599 XP
    PET_STAGE_ADULT   = 3,  ///< 成年   600+ XP
} PetStage;

/** 时间段（决定背景 + 问候语） */
typedef enum {
    TIME_MORNING   = 0,  ///< 06:00~11:59
    TIME_NOON      = 1,  ///< 12:00~13:59
    TIME_AFTERNOON = 2,  ///< 14:00~18:59
    TIME_NIGHT     = 3,  ///< 19:00~05:59
} TimePeriod;

/* =========================================================
   核心数据结构
   ========================================================= */

/** 宠物持久化状态（存 NVS） */
typedef struct {
    uint32_t  total_xp;                 ///< 累计经验值
    PetStage  stage;                    ///< 当前阶段
    uint32_t  last_reset_day;           ///< 上次每日重置的日期（epoch/天）
} PetSavedState;

/** 宠物运行时状态（RAM，不持久化） */
typedef struct {
    PetSavedState saved;                ///< 持久化部分
    PetMood       mood;                 ///< 当前心情
    bool          tasks_done[TASK_MAX]; ///< 当日任务完成情况
    bool          greeted[4];          ///< 四个时间段问候是否已发
    bool          hygiene_asked;        ///< 今晚卫生习惯是否已询问
    bool          sleep_reminded;       ///< 今晚睡前提醒是否已发
    uint8_t       anim_frame;           ///< 当前动画帧 (0/1)
    uint32_t      happy_ticks_left;     ///< 开心动画剩余 tick 数
} PetState;

/* =========================================================
   函数声明 — 游戏逻辑  (aqi_pet_game.c)
   ========================================================= */

/** 初始化宠物系统（从 NVS 加载数据，注册定时器） */
void pet_game_init(void);

/** 手动保存状态到 NVS（通常由定时器自动调用） */
void pet_game_save(void);

/** 获取当前宠物状态指针（只读） */
const PetState* pet_get_state(void);

/**
 * 完成一个任务，发放经验值。
 * @param task  任务类型
 * @return      实际获得的 XP（0 表示今日已完成或无效）
 */
uint32_t pet_complete_task(TaskType task);

/** 强制设置心情（如对话开始时） */
void pet_set_mood(PetMood mood);

/** 每秒调用一次，处理时间事件（问候/提醒）并返回需要播报的文字，NULL 表示无事件 */
const char* pet_tick_time_events(void);

/** 返回当前时间段 */
TimePeriod pet_get_time_period(void);

/** 返回对应时间段的问候语（中文） */
const char* pet_get_greeting(TimePeriod period);

/** 返回任务的显示名（用于屏幕提示） */
const char* pet_task_name(TaskType task);

/* =========================================================
   函数声明 — 显示层  (aqi_pet_display.c)
   ========================================================= */

/**
 * 初始化宠物 UI（在 LVGL 已初始化之后调用）。
 * @param parent  LVGL 父对象，传 NULL 则用 lv_scr_act()
 */
void pet_display_init(lv_obj_t* parent);

/** 每帧刷新 UI（建议在 lv_timer 里以 200ms 调用一次） */
void pet_display_update(void);

/** 显示任务完成动效 + 文字（约 2 秒后自动消失） */
void pet_display_show_task_done(TaskType task, uint32_t xp_gained);

/** 显示临时提示文字（如问候语、睡前提醒），timeout_ms=0 则永久 */
void pet_display_show_message(const char* msg, uint32_t timeout_ms);

/** 切换到对话模式（隐藏宠物，显示波形/文字） */
void pet_display_set_talking(bool talking);

#ifdef __cplusplus
}
#endif
