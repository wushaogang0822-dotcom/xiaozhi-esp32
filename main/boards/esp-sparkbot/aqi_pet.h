/**
 * aqi_pet.h  —  阿奇宠物养成系统主头文件 v2
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet.h
 *
 * 变更（v2）：
 *   - 新增 PET_STAGE_EGG 阶段（0~14 XP）
 *   - 调整 XP 阈值 → 2天可养成成年犬
 *   - 新增 pet_on_ai_message() 函数声明
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
   经验值阈值（2天养成设计）
   每日最大 XP = 5+15+20+25+15+15 = 95
   Day1: 0→95  : 蛋→幼犬→少年犬
   Day2: 95→190: 少年犬→青年犬→成年犬
   ========================================================= */

#define PET_XP_EGG_MAX      15u   // 蛋    0~14 XP
#define PET_XP_STAGE1_MAX   75u   // 幼犬  15~74 XP
#define PET_XP_STAGE2_MAX  125u   // 少年  75~124 XP
#define PET_XP_STAGE3_MAX  175u   // 青年  125~174 XP
                                   // 175+ = 成年犬

/* 任务经验值奖励 */
#define XP_FIRST_BOOT       5u
#define XP_ENGLISH_WORD     15u
#define XP_ENGLISH_SENTENCE 20u
#define XP_POETRY           25u
#define XP_BRUSH_TEETH      15u
#define XP_WASH_NOSE        15u

/* AI 任务码（嵌入 AI 回复末尾，固件扫描触发XP，TTS通常不朗读方括号内容） */
#define AI_CODE_WORD    "[XP:WORD]"
#define AI_CODE_SENT    "[XP:SENT]"
#define AI_CODE_POEM    "[XP:POEM]"
#define AI_CODE_BRUSH   "[XP:BRUSH]"
#define AI_CODE_NOSE    "[XP:NOSE]"

/* 时间节点 */
#define GREET_MORNING_HOUR      6
#define GREET_NOON_HOUR        12
#define GREET_AFTERNOON_HOUR   14
#define GREET_NIGHT_HOUR       20
#define SLEEP_REMIND_HOUR      21
#define SLEEP_REMIND_MINUTE    30
#define HYGIENE_CHECK_HOUR     20
#define NIGHT_BG_START_HOUR    19

/* =========================================================
   枚举
   ========================================================= */

typedef enum {
    TASK_FIRST_BOOT      = 0,
    TASK_ENGLISH_WORD    = 1,
    TASK_ENGLISH_SENT    = 2,
    TASK_POETRY          = 3,
    TASK_BRUSH_TEETH     = 4,
    TASK_WASH_NOSE       = 5,
    TASK_MAX             = 6
} TaskType;

typedef enum {
    PET_MOOD_IDLE     = 0,
    PET_MOOD_HAPPY    = 1,
    PET_MOOD_EXCITED  = 2,
    PET_MOOD_SLEEPY   = 3,
    PET_MOOD_TALKING  = 4,
} PetMood;

typedef enum {
    PET_STAGE_EGG   = 0,  // 蛋     0~14 XP
    PET_STAGE_PUPPY = 1,  // 幼犬   15~74 XP
    PET_STAGE_TEEN  = 2,  // 少年   75~124 XP
    PET_STAGE_YOUNG = 3,  // 青年   125~174 XP
    PET_STAGE_ADULT = 4,  // 成年   175+ XP
    PET_STAGE_MAX   = 5,
} PetStage;

typedef enum {
    TIME_MORNING   = 0,
    TIME_NOON      = 1,
    TIME_AFTERNOON = 2,
    TIME_NIGHT     = 3,
} TimePeriod;

/* =========================================================
   数据结构
   ========================================================= */

typedef struct {
    uint32_t  total_xp;
    PetStage  stage;
    uint32_t  last_reset_day;
} PetSavedState;

typedef struct {
    PetSavedState saved;
    PetMood       mood;
    bool          tasks_done[TASK_MAX];
    bool          greeted[4];
    bool          hygiene_asked;
    bool          sleep_reminded;
    uint8_t       anim_frame;
    uint32_t      happy_ticks_left;
    bool          stage_up_flash;
    uint8_t       stage_up_ticks;
} PetState;

/* =========================================================
   函数声明 — 游戏逻辑 (aqi_pet_game.c)
   ========================================================= */

void        pet_game_init(void);
void        pet_game_save(void);
const PetState* pet_get_state(void);
uint32_t    pet_complete_task(TaskType task);
void        pet_set_mood(PetMood mood);
const char* pet_tick_time_events(void);
TimePeriod  pet_get_time_period(void);
const char* pet_get_greeting(TimePeriod period);
const char* pet_task_name(TaskType task);

/**
 * 处理 AI 回复文字，扫描 [XP:XXX] 任务码并自动加经验值。
 * 在 esp_sparkbot_board.cc 的 SetChatMessage 回调中调用。
 */
void pet_on_ai_message(const char* text);

/* =========================================================
   函数声明 — 显示层 (aqi_pet_display.c)
   ========================================================= */

void pet_display_init(lv_obj_t* parent);
void pet_display_update(void);
void pet_display_show_task_done(TaskType task, uint32_t xp_gained);
void pet_display_show_message(const char* msg, uint32_t timeout_ms);
void pet_display_set_talking(bool talking);

#ifdef __cplusplus
}
#endif
