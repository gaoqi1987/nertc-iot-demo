#include <cstring>

#include "alarm/alarm_manager.h"
#include "alarm/ccronexpr.h"

#ifdef ALARM_ENABLE_FILE_STORAGE
#include <fstream>
#include <sys/stat.h>
#include "esp_spiffs.h"
#include "cJSON.h"
#endif // ALARM_ENABLE_FILE_STORAGE

#define ALARM "Alarm"

// LV_FONT_DECLARE(font_noto_thin_16_1_no_korean);

AlarmManager::AlarmManager() {
    ESP_LOGI(ALARM, "AlarmManager init");
    ring_flag_ = false;
    running_flag_ = false;

#ifdef ALARM_ENABLE_FILE_STORAGE
    save_task_handle_ = nullptr;
    save_queue_ = nullptr;

    if (!InitSpiffs()) {
        ESP_LOGE(ALARM, "Failed to initialize SPIFFS");
    } else {
        // SPIFFS初始化成功后，加载保存的闹钟
        LoadAlarmsFromFile();
        
        // 初始化异步保存任务
        if (!InitSaveTask()) {
            ESP_LOGW(ALARM, "Failed to initialize save task, will use synchronous saving");
        }
    }
#endif // ALARM_ENABLE_FILE_STORAGE

    // 建立一个时钟
    esp_timer_create_args_t timer_args = {
        .callback = [](void *arg)
        {
            AlarmManager *alarm_manager = (AlarmManager *)arg;
            alarm_manager->OnAlarm(); // 闹钟响了
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_timer"};
    esp_timer_create(&timer_args, &timer_);
    time_t now = time(NULL);

    ClearOverdueAlarm(now);

    Alarm *current_alarm_ = GetProximateAlarm(now);
    // 启动闹钟
    if (current_alarm_ != nullptr) {
        int new_timer_time = current_alarm_->time - now;
        ESP_LOGI(ALARM, "begin a alarm at %d, format time %s", new_timer_time, current_alarm_->format_time.c_str());
        esp_timer_start_once(timer_, (int64_t)new_timer_time * 1000000);
        running_flag_ = true;
    }
}

AlarmManager::~AlarmManager() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
    
#ifdef ALARM_ENABLE_FILE_STORAGE
    // 清理异步保存任务
    DestroySaveTask();
#endif // ALARM_ENABLE_FILE_STORAGE
}

#ifdef ALARM_ENABLE_FILE_STORAGE
bool AlarmManager::InitSpiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "custom",  // 分区名称
        .max_files = 5,              // 最大打开文件数
        .format_if_mount_failed = true // 如果挂载失败则格式化
    };
    bool is_mounted = esp_spiffs_mounted(conf.partition_label);
    if (is_mounted) {
        ESP_LOGI(ALARM, "分区 '%s' 已经被挂载，无需重复注册。", conf.partition_label);
    }
    else {
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(ALARM, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(ALARM, "Failed to find SPIFFS partition");
            } else {
                ESP_LOGE(ALARM, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
            }
            return false;
        }
    }
    
    size_t total = 0, used = 0;
    auto ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(ALARM, "Partition size: total: %d, used: %d", total, used);
        spiffs_used_ = used;
        spiffs_total_ = total;
        spiffs_inited_ = true;
        return true;
    } else {
        ESP_LOGE(ALARM, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return false;
    }
    return true;
}
#endif // ALARM_ENABLE_FILE_STORAGE

void AlarmManager::AddAlarm(const Alarm& alarm) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (alarms_.size() > 10) {
            ESP_LOGE(ALARM, "Too many alarms");
            return;
        }
    }

    if (IsAlarmExist(alarm.id)) {
        ESP_LOGE(ALARM, "Alarm id %d already exist", alarm.id);
        return;
    }

    Alarm new_alarm = alarm; // 一个新的闹钟
    time_t now = time(NULL);

    if (new_alarm.alarm_type == ALARM_ONE_OFF) {
        int seconde_from_now = new_alarm.next_trigger_ts - new_alarm.current_ts;
        if (seconde_from_now <= 0) {
            ESP_LOGE(ALARM, "Invalid alarm time");
            return;
        }
        ESP_LOGI(ALARM, "Alarm %s will trigger after %d", alarm.name.c_str(), seconde_from_now);
        new_alarm.time = now + seconde_from_now;
        // 格式化时间
        struct tm timeinfo;
        localtime_r(&new_alarm.time, &timeinfo);
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        new_alarm.format_time = buffer;
        std::lock_guard<std::mutex> lock(mutex_);
        alarms_.push_back(new_alarm);
    } else if (new_alarm.alarm_type == ALARM_RECURRING) {
        time_t next = GetNextTriggerTime(now, new_alarm.cron_expression);
        if (next == -1) {
            ESP_LOGE(ALARM, "Invalid cron expression");
            return;
        } else {
            new_alarm.time = next;
            std::lock_guard<std::mutex> lock(mutex_);
            // 格式化时间
            struct tm timeinfo;
            localtime_r(&new_alarm.time, &timeinfo);
            char buffer[20];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
            new_alarm.format_time = buffer;
            alarms_.push_back(new_alarm);
        }
    }

    Alarm *alarm_first = GetProximateAlarm(now);
    ESP_LOGI(ALARM, "Alarm %s set at %lld, now first %lld", alarm.name.c_str(), (long long)alarm.time, (long long)alarm_first->time);
    if (running_flag_ == true) {
        esp_timer_stop(timer_);
    }
    current_alarm_id_ = alarm_first->id;

    running_flag_ = true;

    int seconde_from_now = alarm_first->time - now;
    ESP_LOGI(ALARM, "begin a alarm at %d, format time %s", seconde_from_now, alarm_first->format_time.c_str());
    esp_timer_start_once(timer_, (int64_t)seconde_from_now * 1000000); // 当前一定有时钟, 所以不需要清除标志

#ifdef ALARM_ENABLE_FILE_STORAGE
    // 使用异步保存闹钟到文件
    if (!SaveAlarmsToFileAsync()) {
        ESP_LOGW(ALARM, "Async save failed, trying synchronous save");
        SaveAlarmsToFile();
    }
#endif // ALARM_ENABLE_FILE_STORAGE
}

// 设置闹钟
AlarmError AlarmManager::SetAlarm(int seconde_from_now, std::string alarm_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (alarms_.size() >= 1) {
        ESP_LOGE(ALARM, "Too many alarms");
        return ALARM_ERROR_TOO_MANY_ALARMS;
    }
    if (seconde_from_now <= 0) {
        ESP_LOGE(ALARM, "Invalid alarm time");
        return ALARM_ERROR_INVALID_ALARM_TIME;
    }

    Alarm alarm; // 一个新的闹钟
    alarm.name = alarm_name;
    time_t now = time(NULL);
    alarm.time = now + seconde_from_now;
    alarms_.push_back(alarm);

    Alarm *alarm_first = GetProximateAlarm(now);
    ESP_LOGI(ALARM, "Alarm %s set at %lld, now first %lld", alarm.name.c_str(), (long long)alarm.time, (long long)alarm_first->time);
    if (running_flag_ == true) {
        esp_timer_stop(timer_);
    }

    running_flag_ = true;

    seconde_from_now = alarm_first->time - now;
    ESP_LOGI(ALARM, "begin a alarm at %d, format time %s", seconde_from_now, alarm_first->format_time.c_str());
    esp_timer_start_once(timer_, (int64_t)seconde_from_now * 1000000); // 当前一定有时钟, 所以不需要清除标志
    return ALARM_ERROR_NONE;
}
// 获取闹钟列表状态
std::string AlarmManager::GetAlarmsStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string status;
    for (size_t i = 0; i < alarms_.size(); ++i) {
        status += alarms_[i].name + " at " + std::to_string(alarms_[i].time);
        if (i != alarms_.size() - 1) {
            status += ", ";
        }
    }
    return status;
}

void AlarmManager::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    alarms_.clear();
    if (running_flag_ == true) {
        esp_timer_stop(timer_);
    }
    running_flag_ = false;
    ring_flag_ = false;
#ifdef ALARM_ENABLE_FILE_STORAGE
    // 清空闹钟后保存到文件
    SaveAlarmsToFile();
#endif // ALARM_ENABLE_FILE_STORAGE
}

void AlarmManager::SetAlarmCallback(std::function<void(const std::string& name, const std::string& format_time)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
}

// 清除过时的闹钟（外部调用，需要加锁）
void AlarmManager::ClearOverdueAlarm(time_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearOverdueAlarmInternal(now);
}

// 清除过时的闹钟（内部调用，已持有锁）
void AlarmManager::ClearOverdueAlarmInternal(time_t now) {
    for (auto it = alarms_.begin(); it != alarms_.end();) {
        if (it->time <= now && it->alarm_type == ALARM_ONE_OFF) {
            ESP_LOGI(ALARM, "Removing expired one-off alarm: %s", it->name.c_str());
            it = alarms_.erase(it); // 删除过期的闹钟, 此时it指向下一个元素
        } else {
            it++;
        }
    }
}

// 获取从现在开始第一个响的闹钟（外部调用，需要加锁）
Alarm *AlarmManager::GetProximateAlarm(time_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return GetProximateAlarmInternal(now);
}

// 获取从现在开始第一个响的闹钟（内部调用，已持有锁）
Alarm *AlarmManager::GetProximateAlarmInternal(time_t now) {
    Alarm *proximate_alarm = nullptr;
    for (auto &alarm : alarms_) {
        if (alarm.time > now && (proximate_alarm == nullptr || alarm.time < proximate_alarm->time)) {
            proximate_alarm = &alarm; // 获取当前时间以后第一个发生的时钟句柄
        }
    }
    return proximate_alarm;
}

// 通过ID获取闹钟（外部调用，需要加锁）
Alarm *AlarmManager::GetAlarmById(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return GetAlarmByIdInternal(id);
}

// 通过ID获取闹钟（内部调用，已持有锁）
Alarm *AlarmManager::GetAlarmByIdInternal(int id) {
    for (auto &alarm : alarms_) {
        if (alarm.id == id) {
            return &alarm;
        }
    }
    return nullptr;
}

time_t AlarmManager::GetNextTriggerTime(time_t now, const std::string& cron_expression) {
    cron_expr expr;
    const char* err = NULL;
    cron_parse_expr(cron_expression.c_str(), &expr, &err);
    if (err) {
        ESP_LOGE(ALARM, "CRON Error: %s\n", err);
        return -1;
    }

    // ccronexpr 内部使用 gmtime（UTC）解析时间，
    // 而 cron 表达式里填写的是本地时间（如 "0 0 10 * * ?" 表示本地 10:00）。
    // 需要将 now 先加上本地 UTC 偏移，让 cron_next 在 UTC 空间内模拟本地时间计算，
    // 再把结果减回偏移，还原为真实 UTC 时间戳。
    // 使用 now+1 避免 cron_next 返回当前秒导致立即触发的问题。
    //
    // 计算本地时区相对 UTC 的偏移（秒），ESP-IDF 不支持 tm_gmtoff。
    // 方法：用 localtime 和 gmtime 分别解析同一时间戳，再用 mktime 把 localtime
    // 结构体还原为时间戳，两者之差即为本地相对 UTC 的偏移。
    // 东八区(CST): local_ts - utc_ts = +28800
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    // mktime 把 local_tm 按本地时区解释还原为时间戳，结果等于 now
    // 我们需要的是：把 local_tm 当 UTC 解释，得到 "本地时间对应的 UTC 时刻"
    // 等价于：local_hour:local_min:local_sec 在 UTC 轴上的时间戳
    // 即 now + utc_offset（东八区 +28800）
    //
    // 正确计算偏移：localtime(now) 与 gmtime(now) 的结构体差值
    struct tm gm_tm;
    gmtime_r(&now, &gm_tm);
    // 把 local_tm 当 UTC 时间戳计算（忽略 DST 干扰，用 timegm 等价实现）
    // timegm 在 ESP-IDF 中不可用，改用以下等价方式：
    // utc_offset = (local_hour - gm_hour)*3600 + (local_min - gm_min)*60 + (local_sec - gm_sec) + day_diff*86400
    int day_diff = local_tm.tm_mday - gm_tm.tm_mday;
    // 处理跨月边界（如本地是1号0点，UTC是前月最后一天16点）
    if (day_diff > 1) day_diff = -1;
    if (day_diff < -1) day_diff = 1;
    time_t utc_offset = (time_t)(
        (local_tm.tm_hour - gm_tm.tm_hour + day_diff * 24) * 3600 +
        (local_tm.tm_min  - gm_tm.tm_min)  * 60 +
        (local_tm.tm_sec  - gm_tm.tm_sec)
    ); // 东八区 = +28800

    // ccronexpr 用 gmtime 解析时间，cron 表达式填的是本地时间（如 "0 0 10 * * ?" 表示本地 10:00）。
    // 把 now 加上偏移，让 cron_next 在 UTC 空间内按本地时间计算；
    // 再把结果减去偏移，还原为真实 Unix 时间戳。
    // +1 跳过当前秒，避免 cron_next 返回 "现在" 导致立即触发。
    time_t now_local_as_utc = (now + 1) + utc_offset;
    time_t next_local_as_utc = cron_next(&expr, now_local_as_utc);
    time_t next = next_local_as_utc - utc_offset;

    ESP_LOGI(ALARM, "GetNextTriggerTime: now=%lld offset=%lld next=%lld (in %lld s)",
             (long long)now, (long long)utc_offset, (long long)next, (long long)(next - now));
    return next;
}

bool AlarmManager::IsAlarmExist(int id) {
    for (auto &alarm : alarms_) {
        if (alarm.id == id) {
            return true;
        }
    }
    return false;
}

// 闹钟响了的处理函数
void AlarmManager::OnAlarm() {
    // 先在锁内收集所有需要的数据，再在锁外执行回调，避免死锁
    std::string callback_name;
    std::string callback_format_time;
    bool should_callback = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        ring_flag_ = true;
        time_t now = time(NULL);

        // 查找当前触发的闹钟（通过 current_alarm_id_ 查找更准确）
        Alarm *triggered_alarm = GetAlarmByIdInternal(current_alarm_id_);
        if (!triggered_alarm) {
            // 如果通过ID找不到，尝试查找已过期的闹钟
            for (auto &alarm : alarms_) {
                if (alarm.time <= now) {
                    triggered_alarm = &alarm;
                    break;
                }
            }
        }

        if (triggered_alarm && callback_) {
            // 复制数据到局部变量，后续在锁外调用回调
            callback_name = triggered_alarm->name;
            callback_format_time = triggered_alarm->format_time;
            should_callback = true;
            ESP_LOGI(ALARM, "Alarm triggered: %s, current time %s", triggered_alarm->name.c_str(), ctime(&now));
        } else if (!triggered_alarm) {
            ESP_LOGW(ALARM, "OnAlarm called but no triggered alarm found");
        }

        // 处理循环闹钟：更新下次触发时间
        Alarm *current_alarm = GetAlarmByIdInternal(current_alarm_id_);
        if (current_alarm && current_alarm->alarm_type == ALARM_RECURRING) {
            time_t next = GetNextTriggerTime(now, current_alarm->cron_expression);
            if (next > now) {
                current_alarm->time = next;
                // 更新格式化时间
                struct tm timeinfo;
                localtime_r(&current_alarm->time, &timeinfo);
                char buffer[20];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
                current_alarm->format_time = buffer;
                ESP_LOGI(ALARM, "Recurring alarm %s next trigger at %s",
                         current_alarm->name.c_str(), current_alarm->format_time.c_str());
#ifdef ALARM_ENABLE_FILE_STORAGE
                // 循环闹钟时间更新后保存到文件
                SaveAlarmsToFileInternal();
#endif // ALARM_ENABLE_FILE_STORAGE
            }
        }

        // 清除已过期的一次性闹钟
        ClearOverdueAlarmInternal(now);

        // 启动下一个闹钟
        Alarm *next_alarm = GetProximateAlarmInternal(now);
        if (next_alarm != nullptr) {
            int new_timer_time = next_alarm->time - now;
            if (new_timer_time > 0) {
                ESP_LOGI(ALARM, "begin a alarm at %d, format time %s", new_timer_time, next_alarm->format_time.c_str());
                esp_timer_start_once(timer_, (int64_t)new_timer_time * 1000000);
                current_alarm_id_ = next_alarm->id;
            }
        } else {
            running_flag_ = false; // 没有闹钟了
            ESP_LOGI(ALARM, "no alarm now");
        }
    } // 释放锁

    // 在锁外执行回调，防止上层应用回调中再次操作 AlarmManager 导致死锁
    if (should_callback) {
        callback_(callback_name, callback_format_time);
    }
}

// 闹钟是不是响了的标志位
bool AlarmManager::IsRing() { return ring_flag_; };
// 清除闹钟标志位
void AlarmManager::ClearRing() {
    ESP_LOGI(ALARM, "clear");
    ring_flag_ = false;
};

#ifdef ALARM_ENABLE_FILE_STORAGE
// 将单个闹钟转换为JSON对象
cJSON* AlarmManager::AlarmToJson(const Alarm& alarm) {
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(ALARM, "Failed to create JSON object");
        return nullptr;
    }
    
    cJSON_AddStringToObject(json, "name", alarm.name.c_str());
    cJSON_AddNumberToObject(json, "time", (double)alarm.time);
    cJSON_AddNumberToObject(json, "current_ts", (double)alarm.current_ts);
    cJSON_AddNumberToObject(json, "next_trigger_ts", (double)alarm.next_trigger_ts);
    cJSON_AddNumberToObject(json, "id", alarm.id);
    cJSON_AddNumberToObject(json, "status", alarm.status);
    cJSON_AddNumberToObject(json, "alarm_type", alarm.alarm_type);
    cJSON_AddStringToObject(json, "cron_expression", alarm.cron_expression.c_str());
    
    return json;
}

// 从JSON对象解析单个闹钟
bool AlarmManager::JsonToAlarm(const cJSON* json, Alarm& alarm) {
    if (!json || !cJSON_IsObject(json)) {
        ESP_LOGE(ALARM, "Invalid JSON object");
        return false;
    }
    
    cJSON* name = cJSON_GetObjectItem(json, "name");
    if (!name || !cJSON_IsString(name)) {
        ESP_LOGE(ALARM, "Missing or invalid 'name' field");
        return false;
    }
    alarm.name = name->valuestring;
    
    cJSON* time = cJSON_GetObjectItem(json, "time");
    if (!time || !cJSON_IsNumber(time)) {
        ESP_LOGE(ALARM, "Missing or invalid 'time' field");
        return false;
    }
    alarm.time = (time_t)time->valuedouble;
    
    cJSON* current_ts = cJSON_GetObjectItem(json, "current_ts");
    if (current_ts && cJSON_IsNumber(current_ts)) {
        alarm.current_ts = (time_t)current_ts->valuedouble;
    } else {
        alarm.current_ts = 0;
    }
    
    cJSON* next_trigger_ts = cJSON_GetObjectItem(json, "next_trigger_ts");
    if (next_trigger_ts && cJSON_IsNumber(next_trigger_ts)) {
        alarm.next_trigger_ts = (time_t)next_trigger_ts->valuedouble;
    } else {
        alarm.next_trigger_ts = 0;
    }
    
    cJSON* id = cJSON_GetObjectItem(json, "id");
    if (id && cJSON_IsNumber(id)) {
        alarm.id = id->valueint;
    } else {
        alarm.id = 0;
    }
    
    cJSON* status = cJSON_GetObjectItem(json, "status");
    if (status && cJSON_IsNumber(status)) {
        alarm.status = status->valueint;
    } else {
        alarm.status = 1; // 默认激活
    }
    
    cJSON* alarm_type = cJSON_GetObjectItem(json, "alarm_type");
    if (alarm_type && cJSON_IsNumber(alarm_type)) {
        alarm.alarm_type = (AlarmType)alarm_type->valueint;
    } else {
        alarm.alarm_type = ALARM_ONE_OFF; // 默认一次性闹钟
    }
    
    cJSON* cron_expression = cJSON_GetObjectItem(json, "cron_expression");
    if (cron_expression && cJSON_IsString(cron_expression)) {
        alarm.cron_expression = cron_expression->valuestring;
    } else {
        alarm.cron_expression = "";
    }
    
    return true;
}

// 将所有闹钟保存到文件
bool AlarmManager::SaveAlarmsToFile() {
    // 检查是否在ISR中调用
    if (xPortInIsrContext()) {
        ESP_LOGE(ALARM, "Cannot save alarms from ISR context");
        return false;
    }
    
    // 检查任务栈空间是否足够
    UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
    if (stack_high_water_mark < 512) {  // 至少需要512字节栈空间
        ESP_LOGW(ALARM, "Low stack space: %d words remaining", stack_high_water_mark);
        // 如果栈空间严重不足，推迟保存操作
        if (stack_high_water_mark < 128) {
            ESP_LOGE(ALARM, "Insufficient stack space for file operation");
            return false;
        }
    }
    
    std::lock_guard<std::mutex> lock(mutex_);

    ESP_LOGI(ALARM, "Saving %zu alarms to file", alarms_.size());
    
    if (!spiffs_inited_) {
        ESP_LOGE(ALARM, "SPIFFS not initialized");
        return false;
    }
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(ALARM, "Failed to create root JSON object");
        return false;
    }
    
    cJSON* alarms_array = cJSON_CreateArray();
    if (!alarms_array) {
        ESP_LOGE(ALARM, "Failed to create alarms array");
        cJSON_Delete(root);
        return false;
    }
    
    // 添加所有闹钟到数组
    for (const auto& alarm : alarms_) {
        cJSON* alarm_json = AlarmToJson(alarm);
        if (alarm_json) {
            cJSON_AddItemToArray(alarms_array, alarm_json);
        } else {
            ESP_LOGW(ALARM, "Failed to convert alarm '%s' to JSON", alarm.name.c_str());
        }
    }
    ESP_LOGI(ALARM, "alarms_array size: %d", cJSON_GetArraySize(alarms_array));
    cJSON_AddItemToObject(root, "alarms", alarms_array);
    
    // 添加元数据
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    cJSON_AddNumberToObject(root, "count", alarms_.size());
    
    // 将JSON转换为字符串
    char* json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(ALARM, "Failed to convert JSON to string");
        cJSON_Delete(root);
        return false;
    }
    
    // 写入文件
    FILE* file = fopen("/spiffs/alarm.txt", "w");
    if (!file) {
        ESP_LOGE(ALARM, "Failed to open file for writing");
        free(json_string);
        cJSON_Delete(root);
        return false;
    }
    
    size_t json_len = strlen(json_string);
    size_t written = fwrite(json_string, 1, json_len, file);
    fclose(file);
    
    bool success = (written == json_len);
    if (success) {
        ESP_LOGI(ALARM, "Successfully saved %zu alarms to /spiffs/alarm.txt (%zu bytes)", 
                alarms_.size(), json_len);
    } else {
        ESP_LOGE(ALARM, "Failed to write complete data to file. Written: %zu, Expected: %zu", 
                written, json_len);
    }
    
    free(json_string);
    cJSON_Delete(root);
    return success;
}

// 从文件加载所有闹钟
bool AlarmManager::LoadAlarmsFromFile() {
    // 注意: 构造函数调用时不需要锁，因为此时对象还未完全构造完成
    // 只有公共接口调用时才需要锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    bool need_lock = lock.owns_lock();
    if (!need_lock) {
        // 如果获取锁失败，假设是从构造函数调用的，直接执行
        lock.release();
    }
    
    if (!spiffs_inited_) {
        ESP_LOGE(ALARM, "SPIFFS not initialized");
        return false;
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat("/spiffs/alarm.txt", &st) != 0) {
        ESP_LOGI(ALARM, "Alarm file does not exist, starting with empty alarm list");
        return true; // 文件不存在不算错误
    }
    
    // 读取文件
    FILE* file = fopen("/spiffs/alarm.txt", "r");
    if (!file) {
        ESP_LOGE(ALARM, "Failed to open file for reading");
        return false;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size <= 0) {
        ESP_LOGI(ALARM, "Empty alarm file");
        fclose(file);
        return true;
    }
    
    // 读取文件内容
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        ESP_LOGE(ALARM, "Failed to allocate memory for file content");
        fclose(file);
        return false;
    }
    
    size_t read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read != (size_t)file_size) {
        ESP_LOGE(ALARM, "Failed to read complete file. Read: %zu, Expected: %ld", read, file_size);
        free(buffer);
        return false;
    }
    
    buffer[file_size] = '\0';
    
    // 解析JSON
    cJSON* root = cJSON_Parse(buffer);
    free(buffer);
    
    if (!root) {
        ESP_LOGE(ALARM, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return false;
    }
    
    // 获取版本信息
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsNumber(version)) {
        ESP_LOGI(ALARM, "Loading alarms from file version %d", version->valueint);
    }
    
    // 获取闹钟数组
    cJSON* alarms_array = cJSON_GetObjectItem(root, "alarms");
    if (!alarms_array || !cJSON_IsArray(alarms_array)) {
        ESP_LOGE(ALARM, "Missing or invalid 'alarms' array");
        cJSON_Delete(root);
        return false;
    }
    
    // 清空现有闹钟列表
    alarms_.clear();
    
    // 解析每个闹钟
    int loaded_count = 0;
    int array_size = cJSON_GetArraySize(alarms_array);
    
    for (int i = 0; i < array_size; i++) {
        cJSON* alarm_json = cJSON_GetArrayItem(alarms_array, i);
        if (!alarm_json) {
            ESP_LOGW(ALARM, "Skipping null alarm at index %d", i);
            continue;
        }
        
        Alarm alarm;
        if (JsonToAlarm(alarm_json, alarm)) {
            alarms_.push_back(alarm);
            loaded_count++;
            ESP_LOGD(ALARM, "Loaded alarm: %s (id=%d, time=%lld)", 
                    alarm.name.c_str(), alarm.id, (long long)alarm.time);
        } else {
            ESP_LOGW(ALARM, "Failed to parse alarm at index %d", i);
        }
    }
    
    ESP_LOGI(ALARM, "Successfully loaded %d alarms from /spiffs/alarm.txt", loaded_count);
    
    cJSON_Delete(root);
    return true;
}

// 获取闹钟信息的JSON字符串（用于调试或API返回）
std::string AlarmManager::GetAlarmsJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(ALARM, "Failed to create root JSON object");
        return "{}";
    }
    
    cJSON* alarms_array = cJSON_CreateArray();
    if (!alarms_array) {
        ESP_LOGE(ALARM, "Failed to create alarms array");
        cJSON_Delete(root);
        return "{}";
    }
    
    // 添加所有闹钟到数组
    for (const auto& alarm : alarms_) {
        cJSON* alarm_json = AlarmToJson(alarm);
        if (alarm_json) {
            cJSON_AddItemToArray(alarms_array, alarm_json);
        }
    }
    
    cJSON_AddItemToObject(root, "alarms", alarms_array);
    cJSON_AddNumberToObject(root, "count", alarms_.size());
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    
    char* json_string = cJSON_Print(root);
    std::string result = json_string ? json_string : "{}";
    
    if (json_string) {
        free(json_string);
    }
    cJSON_Delete(root);
    
    return result;
}

// 异步保存接口
bool AlarmManager::SaveAlarmsToFileAsync() {
    if (!save_task_running_ || !save_queue_) {
        ESP_LOGW(ALARM, "Save task not running, fallback to synchronous save");
        return SaveAlarmsToFile();
    }
    
    SaveTaskMessage msg = SAVE_TASK_SAVE_ALARMS;
    if (xQueueSend(save_queue_, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(ALARM, "Failed to queue save request, fallback to synchronous save");
        return SaveAlarmsToFile();
    }
    
    ESP_LOGI(ALARM, "Alarm save request queued");
    return true;  // 异步操作，假设会成功
}

// 文件保存任务函数
void AlarmManager::SaveTaskFunction(void* parameters) {
    AlarmManager* manager = static_cast<AlarmManager*>(parameters);
    SaveTaskMessage msg;
    
    ESP_LOGI(ALARM, "Save task started");
    
    while (manager->save_task_running_) {
        if (xQueueReceive(manager->save_queue_, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (msg) {
                case SAVE_TASK_SAVE_ALARMS:
                    {
                        ESP_LOGI(ALARM, "Processing save request in save task");
                        bool result = manager->SaveAlarmsToFile();
                        if (!result) {
                            ESP_LOGW(ALARM, "Failed to save alarms in background task");
                        }
                    }
                    break;
                    
                case SAVE_TASK_EXIT:
                    ESP_LOGI(ALARM, "Save task received exit request");
                    manager->save_task_running_ = false;
                    break;
                    
                default:
                    ESP_LOGW(ALARM, "Unknown message in save task: %d", msg);
                    break;
            }
        }
        // 每秒检查一次是否应该退出
    }
    
    ESP_LOGI(ALARM, "Save task exiting");
    vTaskDelete(NULL);
}

// 初始化保存任务
bool AlarmManager::InitSaveTask() {
    // 创建消息队列
    save_queue_ = xQueueCreate(5, sizeof(SaveTaskMessage));
    if (!save_queue_) {
        ESP_LOGE(ALARM, "Failed to create save queue");
        return false;
    }
    
    // 启动任务标志
    save_task_running_ = true;
    
    // 创建任务
    BaseType_t result = xTaskCreate(
        SaveTaskFunction,           // 任务函数
        "AlarmSaveTask",           // 任务名称
        4096,                      // 栈大小（4KB）
        this,                      // 参数
        tskIDLE_PRIORITY + 2,      // 优先级
        &save_task_handle_         // 任务句柄
    );
    
    if (result != pdPASS) {
        ESP_LOGE(ALARM, "Failed to create save task");
        save_task_running_ = false;
        vQueueDelete(save_queue_);
        save_queue_ = nullptr;
        return false;
    }
    
    ESP_LOGI(ALARM, "Save task initialized successfully");
    return true;
}

// 销毁保存任务
void AlarmManager::DestroySaveTask() {
    if (!save_task_running_) {
        return;
    }
    
    ESP_LOGI(ALARM, "Destroying save task");
    
    // 发送退出消息
    if (save_queue_) {
        SaveTaskMessage msg = SAVE_TASK_EXIT;
        xQueueSend(save_queue_, &msg, pdMS_TO_TICKS(1000));
    }
    
    // 等待任务结束
    if (save_task_handle_) {
        // 等待最多2秒
        int wait_count = 20;
        while (save_task_running_ && wait_count-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        if (save_task_running_) {
            ESP_LOGW(ALARM, "Save task did not exit gracefully, force deleting");
            vTaskDelete(save_task_handle_);
        }
    }
    
    // 清理资源
    if (save_queue_) {
        vQueueDelete(save_queue_);
        save_queue_ = nullptr;
    }
    
    save_task_handle_ = nullptr;
    save_task_running_ = false;
    
    ESP_LOGI(ALARM, "Save task destroyed");
}
#endif // ALARM_ENABLE_FILE_STORAGE
