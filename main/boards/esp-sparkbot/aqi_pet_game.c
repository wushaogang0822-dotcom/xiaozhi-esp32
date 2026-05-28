/**
 * aqi_pet_game.c  —  阿奇宠物养成：游戏逻辑
 *
 * 放置路径：main/boards/esp-sparkbot/aqi_pet_game.c
 *
 * 功能：
 *   - NVS 持久化（经验值、阶段）
 *   - 每日任务系统（每天 0 点重置）
 *   - 经验值 & 进化阶段计算
 *   - 时间事件：问候、卫生询问、睡前提醒
 */

#include "aqi_pet.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

/* =========================================================
   内部常量
   ========================================================= */

static const char* TAG       = "AqiPet";
static const char* NVS_NS    = "aqi_pet";      // NVS 命名空间
static const char* NVS_XP    = "total_xp";
static const char* NVS_DAY   = "last_day";
static const char* NVS_STAGE = "stage";

/* 每 30 秒自动保存一次 */
#define AUTO_SAVE_INTERVAL_MS  (30 * 1000)

/* =========================================================
   全局状态
   ========================================================= */

static PetState g_pet;
static TimerHandle_t g_save_timer  = NULL;
static TimerHandle_t g_tick_timer  = NULL;

/* 时间事件回调：存最近一次产生的事件字符串 */
static char g_pending_event[128] = {0};

/* =========================================================
   辅助函数：时间
   ========================================================= */

/** 返回今天是 epoch 第几天（UTC，用于每日重置判断） */
static uint32_t _today_day(void)
{
    time_t now = time(NULL);
    return (uint32_t)(now / 86400);
}

/** 获取本地时间 */
static struct tm _localtime_now(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t;
}

/* =========================================================
   辅助函数：阶段计算
   ========================================================= */

static PetStage _xp_to_stage(uint32_t xp)
{
    if (xp < PET_XP_STAGE1_MAX) return PET_STAGE_PUPPY;
    if (xp < PET_XP_STAGE2_MAX) return PET_STAGE_TEEN;
    if (xp < PET_XP_STAGE3_MAX) return PET_STAGE_YOUNG;
    return PET_STAGE_ADULT;
}

/* =========================================================
   NVS 存取
   ========================================================= */

static void _nvs_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS open failed (first run?), using defaults");
        return;
    }

    uint32_t val = 0;
    if (nvs_get_u32(h, NVS_XP, &val) == ESP_OK)
        g_pet.saved.total_xp = val;

    if (nvs_get_u32(h, NVS_DAY, &val) == ESP_OK)
        g_pet.saved.last_reset_day = val;

    uint8_t stage = 0;
    if (nvs_get_u8(h, NVS_STAGE, &stage) == ESP_OK)
        g_pet.saved.stage = (PetStage)stage;

    nvs_close(h);
    ESP_LOGI(TAG, "Loaded: xp=%lu stage=%d last_day=%lu",
             g_pet.saved.total_xp, g_pet.saved.stage, g_pet.saved.last_reset_day);
}

void pet_game_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u32(h, NVS_XP,    g_pet.saved.total_xp);
    nvs_set_u32(h, NVS_DAY,   g_pet.saved.last_reset_day);
    nvs_set_u8 (h, NVS_STAGE, (uint8_t)g_pet.saved.stage);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved: xp=%lu stage=%d", g_pet.saved.total_xp, g_pet.saved.stage);
}

/* =========================================================
   每日重置
   ========================================================= */

static void _daily_reset(void)
{
    uint32_t today = _today_day();
    if (today <= g_pet.saved.last_reset_day) return;  // 今天已重置过

    ESP_LOGI(TAG, "Daily reset (day %lu → %lu)", g_pet.saved.last_reset_day, today);
    g_pet.saved.last_reset_day = today;
    memset(g_pet.tasks_done, 0, sizeof(g_pet.tasks_done));
    memset(g_pet.greeted,    0, sizeof(g_pet.greeted));
    g_pet.hygiene_asked  = false;
    g_pet.sleep_reminded = false;

    // 每日首次启动自动完成 TASK_FIRST_BOOT
    pet_complete_task(TASK_FIRST_BOOT);
}

/* =========================================================
   定时器回调
   ========================================================= */

static void _auto_save_cb(TimerHandle_t xTimer)
{
    pet_game_save();
}

/* 每 1 秒 tick，检查时间事件 */
static void _tick_cb(TimerHandle_t xTimer)
{
    pet_tick_time_events();
}

/* =========================================================
   初始化
   ========================================================= */

void pet_game_init(void)
{
    memset(&g_pet, 0, sizeof(g_pet));

    /* NVS 初始化（如果 main 里已初始化可省略） */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    _nvs_load();
    g_pet.saved.stage = _xp_to_stage(g_pet.saved.total_xp);
    g_pet.mood = PET_MOOD_IDLE;

    /* 每日重置检查（开机时） */
    _daily_reset();

    /* 自动保存定时器 */
    g_save_timer = xTimerCreate("pet_save", pdMS_TO_TICKS(AUTO_SAVE_INTERVAL_MS),
                                pdTRUE, NULL, _auto_save_cb);
    if (g_save_timer) xTimerStart(g_save_timer, 0);

    /* 时间事件定时器（每秒） */
    g_tick_timer = xTimerCreate("pet_tick", pdMS_TO_TICKS(1000),
                                pdTRUE, NULL, _tick_cb);
    if (g_tick_timer) xTimerStart(g_tick_timer, 0);

    ESP_LOGI(TAG, "pet_game_init OK. XP=%lu Stage=%d",
             g_pet.saved.total_xp, g_pet.saved.stage);
}

/* =========================================================
   公开 API
   ========================================================= */

const PetState* pet_get_state(void)
{
    return &g_pet;
}

uint32_t pet_complete_task(TaskType task)
{
    if (task >= TASK_MAX) return 0;
    if (g_pet.tasks_done[task]) {
        ESP_LOGI(TAG, "Task %d already done today", task);
        return 0;
    }

    static const uint32_t XP_TABLE[TASK_MAX] = {
        XP_FIRST_BOOT,         // TASK_FIRST_BOOT
        XP_ENGLISH_WORD,       // TASK_ENGLISH_WORD
        XP_ENGLISH_SENTENCE,   // TASK_ENGLISH_SENT
        XP_POETRY,             // TASK_POETRY
        XP_BRUSH_TEETH,        // TASK_BRUSH_TEETH
        XP_WASH_NOSE,          // TASK_WASH_NOSE
    };

    uint32_t xp = XP_TABLE[task];
    g_pet.tasks_done[task] = true;
    g_pet.saved.total_xp  += xp;

    PetStage old_stage = g_pet.saved.stage;
    g_pet.saved.stage = _xp_to_stage(g_pet.saved.total_xp);

    if (g_pet.saved.stage > old_stage) {
        g_pet.mood = PET_MOOD_EXCITED;
        ESP_LOGI(TAG, "Stage up! %d → %d", old_stage, g_pet.saved.stage);
    } else {
        g_pet.mood = PET_MOOD_HAPPY;
        g_pet.happy_ticks_left = 10; // 10 个动画帧开心
    }

    ESP_LOGI(TAG, "Task %d done, +%lu XP, total=%lu", task, xp, g_pet.saved.total_xp);
    return xp;
}

void pet_set_mood(PetMood mood)
{
    g_pet.mood = mood;
}

/* =========================================================
   时间事件
   ========================================================= */

TimePeriod pet_get_time_period(void)
{
    struct tm t = _localtime_now();
    int h = t.tm_hour;
    if (h >= 6  && h < 12) return TIME_MORNING;
    if (h >= 12 && h < 14) return TIME_NOON;
    if (h >= 14 && h < 19) return TIME_AFTERNOON;
    return TIME_NIGHT;
}

const char* pet_get_greeting(TimePeriod period)
{
    switch (period) {
        case TIME_MORNING:   return "早上好！新的一天开始啦，阿奇等你来玩哦！";
        case TIME_NOON:      return "中午好！吃饭了吗？记得多喝水哦！";
        case TIME_AFTERNOON: return "下午好！来跟阿奇一起学点新东西吧！";
        case TIME_NIGHT:     return "晚上好！今天表现怎么样？阿奇想听你说说！";
        default:             return "你好呀！";
    }
}

const char* pet_task_name(TaskType task)
{
    switch (task) {
        case TASK_FIRST_BOOT:   return "每日启动";
        case TASK_ENGLISH_WORD: return "跟读英语单词";
        case TASK_ENGLISH_SENT: return "跟读英语短句";
        case TASK_POETRY:       return "背诵古诗";
        case TASK_BRUSH_TEETH:  return "刷牙打卡";
        case TASK_WASH_NOSE:    return "洗鼻子打卡";
        default:                return "未知任务";
    }
}

/**
 * 每秒调用，检查并产生时间事件。
 * 返回需要播报的字符串，NULL 表示无事件。
 * 注意：字符串存在静态缓冲区，下次调用会覆盖。
 */
const char* pet_tick_time_events(void)
{
    g_pending_event[0] = '\0';

    struct tm t = _localtime_now();
    int h = t.tm_hour;
    int m = t.tm_min;
    int s = t.tm_sec;

    /* 只在每分钟 0 秒时触发，避免重复 */
    if (s != 0) return NULL;

    TimePeriod period = pet_get_time_period();

    /* ---- 时间段问候（每天每段只一次） ---- */
    int greet_idx = -1;
    if (h == GREET_MORNING_HOUR   && m == 0) greet_idx = 0;
    if (h == GREET_NOON_HOUR      && m == 0) greet_idx = 1;
    if (h == GREET_AFTERNOON_HOUR && m == 0) greet_idx = 2;
    if (h == GREET_NIGHT_HOUR     && m == 0) greet_idx = 3;

    if (greet_idx >= 0 && !g_pet.greeted[greet_idx]) {
        g_pet.greeted[greet_idx] = true;
        g_pet.mood = PET_MOOD_HAPPY;
        snprintf(g_pending_event, sizeof(g_pending_event), "%s", pet_get_greeting(period));
        return g_pending_event;
    }

    /* ---- 20:00 卫生习惯询问 ---- */
    if (h == HYGIENE_CHECK_HOUR && m == 0 && !g_pet.hygiene_asked) {
        g_pet.hygiene_asked = true;
        snprintf(g_pending_event, sizeof(g_pending_event),
                 "晚上啦！阿奇想问问你，今天有没有刷牙？有没有洗鼻子？");
        return g_pending_event;
    }

    /* ---- 21:30 睡前提醒 ---- */
    if (h == SLEEP_REMIND_HOUR && m == SLEEP_REMIND_MINUTE && !g_pet.sleep_reminded) {
        g_pet.sleep_reminded = true;
        g_pet.mood = PET_MOOD_SLEEPY;
        snprintf(g_pending_event, sizeof(g_pending_event),
                 "打哈欠～ 阿奇困啦，你也该去睡觉咯！明天见！晚安！");
        return g_pending_event;
    }

    /* ---- 每日重置（0:00） ---- */
    if (h == 0 && m == 0) {
        _daily_reset();
    }

    return NULL;
}
