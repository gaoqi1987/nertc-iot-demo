#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/shell_task.h>
#include <components/event.h>
#include <components/netif_types.h>
#include "bk_rtos_debug.h"
#include "audio_config.h"
#include "nertc.h"
#include "aud_intf.h"
#include "aud_intf_types.h"
#include <driver/media_types.h>
#include <driver/lcd.h>
#include <modules/wifi.h>
#include "modules/wifi_types.h"
#include "media_app.h"
#include "lcd_act.h"
#include "components/bk_uid.h"
#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif
#include "app_event.h"
#include "audio_process.h"
#include "cli.h"

#include "audio_engine.h"
#include "video_engine.h"
#include <driver/aon_rtc.h>

#define TAG "nertc_main"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

bool g_connected_flag = false;
bool g_agent_offline = true;
bool nertc_runing = false;

static char g_nertc_app_key[64] = CONFIG_NERTC_APPKEY;
static char g_nertc_device_id[64] = CONFIG_NERTC_DEVICE_ID;
static char g_nertc_channel_name[64] = {0};
static char g_nertc_custom_setting[128] = CONFIG_NERTC_CUSTOM_SETTING;
static char g_nertc_license[768] = CONFIG_NERTC_DEFAULT_TEST_LICENSE;
static uint32_t g_nertc_server_aec_enabled = CONFIG_NERTC_SERVER_AEC;
static uint32_t g_nertc_uid = CONFIG_NERTC_TEST_UID;

static beken_thread_t  g_nertc_thread_hdl = NULL;
static beken_semaphore_t g_nertc_sem = NULL;
static bool g_nertc_enable_audio = true;

static nertc_config_t g_nertc_config = DEFAULT_NERTC_CONFIG();
static nertc_option_t g_nertc_option = DEFAULT_NERTC_OPTION();

extern bool smart_config_running;
extern uint8_t ir_mode_switching;
extern bk_err_t video_turn_off(void);
extern bk_err_t video_turn_on(void);

static void cli_nertc_help(void)
{
    LOGI("nertc_test {start|stop appkey channel_name}\n");
    LOGI("nertc_debug {dump_mic_data value}\n");
}

static void memory_free_show(void)
{
    uint32_t total_size, free_size, mini_size;

    LOGI("%-5s   %-5s   %-5s   %-5s   %-5s\r\n", "name", "total", "free", "minimum", "peak");

    total_size = rtos_get_total_heap_size();
    free_size  = rtos_get_free_heap_size();
    mini_size  = rtos_get_minimum_free_heap_size();
    LOGI("heap:\t%d\t%d\t%d\t%d\r\n",  total_size, free_size, mini_size, total_size - mini_size);

#if CONFIG_PSRAM_AS_SYS_MEMORY
    total_size = rtos_get_psram_total_heap_size();
    free_size  = rtos_get_psram_free_heap_size();
    mini_size  = rtos_get_psram_minimum_free_heap_size();
    LOGI("psram:\t%d\t%d\t%d\t%d\r\n", total_size, free_size, mini_size, total_size - mini_size);
#endif
}

static void nertc_user_notify_msg_handle(nertc_msg_t *p_msg)
{
    switch (p_msg->msg_code)
    {
        case NERTC_MSG_JOIN_CHANNEL_RESULT:
            LOGI("Join channel result: %d\n", p_msg->code);
            g_connected_flag = p_msg->code == 0 ? true : false;
            if (g_connected_flag)
            {
                network_reconnect_stop_timeout_check();
                app_event_send_msg(APP_EVT_AGENT_JOINED, 0);
                g_agent_offline = false;
                smart_config_running = false;
            } else {
                g_agent_offline = true;
                app_event_send_msg(APP_EVT_AGENT_OFFLINE, 0);
            }
            break;
        case NERTC_MSG_REJOIN_CHANNEL_RESULT:
            LOGI("Rejoin channel result: %d\n", p_msg->code);
            g_connected_flag = p_msg->code == 0 ? true : false;
            if (g_agent_offline == false)
                network_reconnect_stop_timeout_check();
            app_event_send_msg(APP_EVT_RTC_REJOIN_SUCCESS, 0);
            break;
        case NERTC_MSG_USER_JOINED:
            LOGI("User Joined uid:%llu\n", p_msg->uid);
            break;
        case NERTC_MSG_USER_LEAVED:
            LOGI("User Offline uid:%llu\n", p_msg->uid);
            g_agent_offline = true;
            app_event_send_msg(APP_EVT_AGENT_OFFLINE, 0);
            if (g_connected_flag == true && !ir_mode_switching)
               app_event_send_msg(APP_EVT_AGENT_DEVICE_REMOVE, 0);
            break;
        case NERTC_MSG_CONNECTION_LOST:
            LOGE("Lost connection. Please check wifi status.\n");
            g_agent_offline = true;
            app_event_send_msg(APP_EVT_AGENT_OFFLINE, 0);
            app_event_send_msg(APP_EVT_RTC_CONNECTION_LOST, 0);
            if (g_connected_flag == true && !ir_mode_switching)
               app_event_send_msg(APP_EVT_AGENT_DEVICE_REMOVE, 0);
            g_connected_flag = false;
            break;
        default:
            break;
    }
}

static int nertc_audio_rx_data_handle(uint64_t uid, nertc_sdk_audio_encoded_frame_t* encoded_frame, bool is_mute_packet)
{
    bk_err_t ret = BK_OK;

    // #if CONFIG_DEBUG_DUMP
    // if(rx_spk_data_flag)
    // {
    //     uint32_t decoder_type = bk_aud_get_decoder_type();
    //     uint8_t dump_file_type = dbg_dump_get_dump_file_type((uint8_t)decoder_type);
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_DUMP_FILE_TYPE(DUMP_TYPE_RX_SPK,0,dump_file_type);
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_DATA_FLOW_LEN(DUMP_TYPE_RX_SPK,0,size);
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_TIMESTAMP(DUMP_TYPE_RX_SPK);
    //     #if CONFIG_DEBUG_DUMP_DATA_TYPE_EXTENSION
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_DATA_FLOW_SAMP_RATE(DUMP_TYPE_RX_SPK,0,bk_aud_get_dac_sample_rate());
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_DATA_FLOW_FRAME_IN_MS(DUMP_TYPE_RX_SPK,0,bk_aud_get_dec_frame_len_in_ms());
    //     #endif
    //     DEBUG_DATA_DUMP_BY_UART_HEADER(DUMP_TYPE_RX_SPK);
    //     DEBUG_DATA_DUMP_UPDATE_HEADER_SEQ_NUM(DUMP_TYPE_RX_SPK);
    //     DEBUG_DATA_DUMP_BY_UART_DATA(data, size);
    // }
    // #endif//CONFIG_DEBUG_DUMP

    ret = bk_aud_intf_write_spk_data((uint8_t *)encoded_frame->data, (uint32_t)encoded_frame->length);
    if (ret != BK_OK)
    {
        LOGE("write spk data fail \r\n");
    }

    return ret;
}

void nertc_main(void)
{
    bk_err_t ret = BK_OK;
    memory_free_show();

    audio_tras_register_tx_data_func(bk_nertc_audio_data_send);

    g_nertc_config.p_app_key = (char *)psram_malloc(strlen(g_nertc_app_key) + 1);
    os_strcpy((char *)g_nertc_config.p_app_key, g_nertc_app_key);
    g_nertc_config.p_device_id = (char *)psram_malloc(strlen(g_nertc_device_id) + 1);
    os_strcpy((char *)g_nertc_config.p_device_id, g_nertc_device_id);
    g_nertc_config.p_license = (char *)psram_malloc(strlen(g_nertc_license) + 1);
    os_strcpy((char *)g_nertc_config.p_license, g_nertc_license);
    g_nertc_config.p_custom_setting = (char *)psram_malloc(strlen(g_nertc_custom_setting) + 1);
    os_strcpy((char *)g_nertc_config.p_custom_setting, g_nertc_custom_setting);
    g_nertc_config.enable_server_aec = g_nertc_server_aec_enabled;
    ret = bk_nertc_create(&g_nertc_config, (nertc_msg_notify_cb)nertc_user_notify_msg_handle);
    if (ret != BK_OK)
    {
        LOGI("bk_nertc_create fail \r\n");
        return;
    }

    // LOGI("-----start nertc process-----\r\n");
    if (strlen(g_nertc_channel_name))
    {
        g_nertc_option.p_channel_name = (char *)psram_malloc(os_strlen(g_nertc_channel_name) + 1);
        os_strcpy((char *)g_nertc_option.p_channel_name, g_nertc_channel_name);
    }
    else
    {
        static bool srand_initialized = false;
        if (!srand_initialized) {
            srand((unsigned int)bk_aon_rtc_get_ms());
            srand_initialized = true;
        }
        uint32_t random_num = 100000 + ((uint32_t)rand() % 900000);
        char tmp_channel_name[256] = {0};
        snprintf(tmp_channel_name, sizeof(tmp_channel_name), "80%u", random_num);

        if (g_nertc_option.p_channel_name) {
            psram_free(g_nertc_option.p_channel_name);
            g_nertc_option.p_channel_name = NULL;
        }
        size_t len = os_strlen(tmp_channel_name) + 1;
        g_nertc_option.p_channel_name = (char *)psram_malloc(len);
        if (g_nertc_option.p_channel_name == NULL) {
            printf("Failed to allocate memory for channel name\n");
            return;
        }
        os_strcpy(g_nertc_option.p_channel_name, tmp_channel_name);
    }
    g_nertc_option.uid = g_nertc_uid;
    ret = bk_nertc_start(&g_nertc_option);
    if (ret != BK_OK)
    {
        LOGE("bk_nertc_start fail, ret:%d \r\n", ret);
        return;
    }

    nertc_runing = true;

    rtos_set_semaphore(&g_nertc_sem);

    /* wait until we join channel successfully */
    while (!g_connected_flag)
    {
        // memory_free_show();
        // rtos_dump_task_runtime_stats();
        if (!nertc_runing)
        {
            goto exit;
        }
        // LOGI("[rsc] ----- nertc_main -----\r\n");
        rtos_delay_milliseconds(100);
    }

    LOGI("-----nertc_join_channel success----- g_nertc_enable_audio:%d\r\n", g_nertc_enable_audio);

#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
    ret = bk_nertc_register_audio_rx_handle((nertc_audio_rx_data_handler)nertc_audio_rx_data_handle);
    if (ret != BK_OK)
    {
        LOGE("bk_nertc_register_audio_rx_handle fail, ret:%d \r\n", ret);
    }
#else
    /* turn on audio */
    if (g_nertc_enable_audio)
    {
        ret = audio_turn_on();
        if (ret != BK_OK)
        {
            LOGE("%s, %d, audio turn on fail, ret:%d\n", __func__, __LINE__, ret);
            goto exit;
        }
        memory_free_show();
    }
#endif

    while (nertc_runing)
    {
        rtos_delay_milliseconds(100);
        // memory_free_show();
        // rtos_dump_task_runtime_stats();
    }

exit:
#if CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE
    /* deregister callback to handle audio data received from agora rtc */
    bk_nertc_register_audio_rx_handle(NULL);
#else
    /* free audio  */
    if (g_nertc_enable_audio)
    {
        audio_turn_off();
    }
#endif

    /* free nertc */
    /* stop nerc */
    bk_nertc_stop();

    /* destory nertc */
    bk_nertc_destroy();

    if (g_nertc_config.p_app_key)
    {
        psram_free((char *)g_nertc_config.p_app_key);
        g_nertc_config.p_app_key = NULL;
    }
    if (g_nertc_config.p_device_id)
    {
        psram_free((char *)g_nertc_config.p_device_id);
        g_nertc_config.p_device_id = NULL;
    }
    if (g_nertc_config.p_license)
    {
        psram_free((char *)g_nertc_config.p_license);
        g_nertc_config.p_license = NULL;
    }
    if (g_nertc_config.p_custom_setting)
    {
        psram_free((char *)g_nertc_config.p_custom_setting);
        g_nertc_config.p_custom_setting = NULL;
    }

    if (g_nertc_option.p_channel_name)
    {
        psram_free((char *)g_nertc_option.p_channel_name);
        g_nertc_option.p_channel_name = NULL;
    }

    g_nertc_enable_audio = false;

    g_connected_flag = false;

    /* delete task */
    g_nertc_thread_hdl = NULL;

    nertc_runing = false;

    rtos_set_semaphore(&g_nertc_sem);

    LOGE("nertc_main exit\n");
    rtos_delete_thread(NULL);
}

bk_err_t nertc_ai_start(void)
{
    if (!nertc_runing)
    {
        LOGI("nertc not start\n");
        return BK_OK;
    }

    bk_err_t ret = bk_nertc_start_ai();
    if (ret != BK_OK)
    {
         LOGE("nertc start ai failed:%d\n", ret);
    }
    else {
         LOGI("nertc start ai success!\n");
    }
    return ret;
}

bk_err_t nertc_ai_stop(void)
{
    if (!nertc_runing)
    {
        LOGI("nertc not start\n");
        return BK_OK;
    }

    bk_err_t ret = bk_nertc_stop_ai();
    if (ret != BK_OK)
    {
         LOGE("nertc stop ai failed:%d\n", ret);
    }
    else {
         LOGI("nertc stop ai success!\n");
    }
    return ret;
}

bk_err_t nertc_stop(void)
{
    if (!nertc_runing)
    {
        LOGI("nertc not start\n");
        return BK_OK;
    }

    nertc_runing = false;

    rtos_get_semaphore(&g_nertc_sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&g_nertc_sem);
    g_nertc_sem = NULL;

    LOGI("nertc stop success!\n");
    return BK_OK;
}

static bk_err_t nertc_start(void)
{
    bk_err_t ret = BK_OK;

    if (nertc_runing)
    {
        LOGI("nertc already start, Please close and then reopens\n");
        return BK_FAIL;
    }

    ret = rtos_init_semaphore(&g_nertc_sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create semaphore fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    ret = rtos_create_thread(&g_nertc_thread_hdl,
                             4,
                             "nertc",
                             (beken_thread_function_t)nertc_main,
                             6 * 1024,
                             NULL);
    if (ret != kNoErr)
    {
        LOGE("%s, %d, create nertc app task fail, ret:%d\n", __func__, __LINE__, ret);
        g_nertc_thread_hdl = NULL;
        goto fail;
    }

    rtos_get_semaphore(&g_nertc_sem, BEKEN_NEVER_TIMEOUT);

    LOGI("create nertc app task complete\n");

    return BK_OK;

fail:

    if (g_nertc_sem)
    {
        rtos_deinit_semaphore(&g_nertc_sem);
        g_nertc_sem = NULL;
    }

    return BK_FAIL;
}

void cli_nertc_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc < 2)
    {
        goto cmd_fail;
    }

    /* audio test */
    if (os_strcmp(argv[1], "start") == 0)
    {
        g_nertc_enable_audio = true;
        nertc_start();
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        nertc_stop();
    }
    else
    {
        goto cmd_fail;
    }

    return;

cmd_fail:
    cli_nertc_help();
}

/* call this api when wifi autoconnect */
extern char *app_id_record;
extern char *channel_name_record;
extern char *token_record;
extern uint32_t uid_record;
void nertc_auto_run(uint8_t reset)
{
    // if (bk_sconf_wakeup_agent(reset)) {
    //     LOGE("%s, wake up agent fail!\n", __func__);
    //     app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
    //     return;
    // }
    bk_sconf_sync_flash();
    if (!nertc_runing)
    {
        g_nertc_enable_audio = true;
        nertc_start();
    }
}

#define NERTC_CMD_CNT   (sizeof(s_nertc_commands) / sizeof(struct cli_command))

extern void cli_nertc_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);

static const struct cli_command s_nertc_commands[] =
{
    {"nertc_test", "nertc_test ...", cli_nertc_test_cmd},
};

int nertc_cli_init(void)
{
    return cli_register_commands(s_nertc_commands, NERTC_CMD_CNT);
}
