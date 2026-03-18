#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

// 定义此宏以启用闹钟文件持久化功能（SPIFFS读写）
// 注释掉此宏可完全屏蔽文件相关操作，适用于不需要持久化或调试阶段
// #define ALARM_ENABLE_FILE_STORAGE

#include <esp_log.h>

#include <string>
#include <vector>
#include "time.h"
#include <mutex>
#include <atomic>
#include "settings.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "lvgl.h"

// 前向声明
typedef struct cJSON cJSON;

// LV_FONT_DECLARE(font_noto_thin_16_1_no_korean);


enum AlarmError {
    ALARM_ERROR_NONE = 0,
    ALARM_ERROR_TOO_MANY_ALARMS = 1,
    ALARM_ERROR_INVALID_ALARM_TIME = 2,
    ALARM_ERROR_INVALID_ALARM_MANAGER = 3,
};

enum AlarmType {
    ALARM_ONE_OFF = 1,
    ALARM_RECURRING = 2,
};

struct Alarm {
    std::string name;
    time_t time;
    time_t current_ts;
    time_t next_trigger_ts;
    int id;
    int status;
    AlarmType alarm_type;
    std::string cron_expression;
    std::string format_time;
};

#ifdef ALARM_ENABLE_FILE_STORAGE
// 文件保存任务消息类型
enum SaveTaskMessage {
    SAVE_TASK_SAVE_ALARMS = 1,
    SAVE_TASK_EXIT = 2
};
#endif // ALARM_ENABLE_FILE_STORAGE


class AlarmManager {
public:
    AlarmManager();
    ~AlarmManager();

    void AddAlarm(const Alarm& alarm);
    // 设置闹钟
    AlarmError SetAlarm(int seconde_from_now, std::string alarm_name);
    // 获取闹钟列表状态
    std::string GetAlarmsStatus();

    void ClearAll();

#ifdef ALARM_ENABLE_FILE_STORAGE
    // JSON序列化和文件读写公共接口
    bool SaveAlarmsToFile();
    bool LoadAlarmsFromFile();
    std::string GetAlarmsJson();
    
    // 异步保存接口
    bool SaveAlarmsToFileAsync();
#endif // ALARM_ENABLE_FILE_STORAGE

    void SetAlarmCallback(std::function<void(const std::string& name, const std::string& format_time)> callback);

private:
    // 清除过时的闹钟（外部调用，需要加锁）
    void ClearOverdueAlarm(time_t now);
    // 清除过时的闹钟（内部调用，已持有锁）
    void ClearOverdueAlarmInternal(time_t now);

    // 获取从现在开始第一个响的闹钟（外部调用，需要加锁）
    Alarm *GetProximateAlarm(time_t now);
    // 获取从现在开始第一个响的闹钟（内部调用，已持有锁）
    Alarm *GetProximateAlarmInternal(time_t now);

    // 通过ID获取闹钟（外部调用，需要加锁）
    Alarm *GetAlarmById(int id);
    // 通过ID获取闹钟（内部调用，已持有锁）
    Alarm *GetAlarmByIdInternal(int id);

    time_t GetNextTriggerTime(time_t now, const std::string& cron_expression);
    bool IsAlarmExist(int id);
    // 闹钟响了的处理函数
    void OnAlarm();

    // 闹钟是不是响了的标志位
    bool IsRing();
    // 清除闹钟标志位
    void ClearRing();

#ifdef ALARM_ENABLE_FILE_STORAGE
    // 初始化spiffs
    bool InitSpiffs();

    // JSON序列化和文件读写内部辅助功能
    cJSON* AlarmToJson(const Alarm& alarm);
    bool JsonToAlarm(const cJSON* json, Alarm& alarm);

    // 文件保存任务相关
    static void SaveTaskFunction(void* parameters);
    bool InitSaveTask();
    void DestroySaveTask();

    // 内部保存方法（已持有锁）
    bool SaveAlarmsToFileInternal();
#endif // ALARM_ENABLE_FILE_STORAGE

    std::vector<Alarm> alarms_; // 闹钟列表
    std::mutex mutex_; // 互斥锁
    esp_timer_handle_t timer_; // 定时器

    std::atomic<bool> ring_flag_{false}; 
    std::atomic<bool> running_flag_{false};
    std::atomic<int> current_alarm_id_{0};

#ifdef ALARM_ENABLE_FILE_STORAGE
    std::atomic<int> spiffs_used_{0};
    std::atomic<int> spiffs_total_{0};
    std::atomic<bool> spiffs_inited_{false};
    
    // 文件保存任务相关
    TaskHandle_t save_task_handle_;
    QueueHandle_t save_queue_;
    std::atomic<bool> save_task_running_{false};
#endif // ALARM_ENABLE_FILE_STORAGE

    std::function<void(const std::string& name, const std::string& format_time)> callback_;
};

#endif // ALARM_MANAGER_H