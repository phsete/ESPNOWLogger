#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#if CONFIG_ONESHOT
#include "esp_adc/adc_oneshot.h"
#elif CONFIG_CONTINUOUS
#include "esp_adc/adc_continuous.h"
#endif
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "main.h"
#include "uuid.h"
#include <esp_sleep.h>
#include "mqtt_client.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (5) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_4_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (1) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY          (1000000) // Frequency in Hertz. Set frequency at 5 kHz

#if CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  10        /* Time ESP32 will go to sleep (in seconds) */

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channel
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_2
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_0
#define _EXAMPLE_ADC_UNIT_STR(unit)         #unit
#define EXAMPLE_ADC_UNIT_STR(unit)          _EXAMPLE_ADC_UNIT_STR(unit)
#define EXAMPLE_ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)        ((p_data)->type2.data)

#define MAC_LENGTH 18

#define SAMPLE_AMOUNT 5
#define SKIP_AMOUNT 25

const static char *TAG = "ADC";

RTC_DATA_ATTR int bootCount = 0;

typedef struct MyMessageType {
    int value;
    char uuid[UUID_STR_LEN];
    char mac_address[MAC_LENGTH];
};
struct MyMessageType message_to_send;

static int adc_raw[SAMPLE_AMOUNT];

uint8_t broadcastAddress[] = {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t peerAddress[] = {0x40, 0x4C, 0xCA, 0x41, 0x16, 0xBC};
esp_now_peer_info_t peerInfo;
bool wifi_ready = false;
bool espnow_ready = false;
bool mqtt_ready = false;
bool button_pressed = false;

esp_mqtt_client_handle_t mqtt_client;
int retry_num=0;

esp_netif_t * my_sta;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// Should be used when multiple types of data are sent
// typedef struct struct_message {
//     char a[85];
// } struct_message;

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : printf("Wakeup caused by external signal using RTC_IO\n"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : printf("Wakeup caused by external signal using RTC_CNTL\n"); break;
    case ESP_SLEEP_WAKEUP_TIMER : printf("Wakeup caused by timer\n"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : printf("Wakeup caused by touchpad\n"); break;
    case ESP_SLEEP_WAKEUP_ULP : printf("Wakeup caused by ULP program\n"); break;
    default : printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void onReceiveData(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    uint8_t *mac = recv_info->src_addr;
    printf("** Data Received **\n\n");
    printf("Received from MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("Length: %d byte(s)\n", len);
    printf("Data: %d\n\n", data[0]);
}

/* WiFi should start before using ESPNOW */
static void example_wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(ESPNOW_WIFI_MODE);
    esp_wifi_start();
    esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(DEFAULT_PS_MODE);
    
    wifi_ready = true;
}

void initESP_NOW() {
    if (esp_now_init() != ESP_OK) {
        printf("Error initializing ESP-NOW\n");
        return;
    }
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        printf("Failed to add peer\n");
        return;
    }

    espnow_ready = true;

    // esp_now_register_recv_cb(onReceiveData);
}

void send_message(int value, char uuid_str[UUID_STR_LEN], char mac_address[MAC_LENGTH]) {
    // Create message
    char message[sizeof(int) + UUID_STR_LEN + MAC_LENGTH];
    sprintf(message, "%d;%s;%s", value, uuid_str, mac_address);

    // Calculate CRC
    uint32_t crc = esp_crc32_le(0, (uint8_t *)message, strlen(message));

    // Create message with CRC
    char message_crc[sizeof(message) + sizeof(crc) * 2 + 1];
    sprintf(message_crc, "%s%08lx", message, crc);

    // Uncomment the following lines for debugging
    // printf("Message: %s\n", message);
    // printf("CRC: %lx\n", crc);
    // printf("Message with CRC: %s\n", message_crc);

    // Send message using esp_now_send
    esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&message_crc, sizeof(message_crc));

    // Check result
    if (result == ESP_OK) {
        printf("Data sent successfully\n");
    } else {
        printf("Error sending the data\n");
    }
}

void mac_unparse(const uint8_t mac_address[6], char *mac_str)
{
    snprintf(mac_str, MAC_LENGTH, "%02X-%02X-%02X-%02X-%02X-%02X", mac_address[0],mac_address[1],mac_address[2],mac_address[3],mac_address[4],mac_address[5]);
}

#if CONFIG_CONTINUOUS
static TaskHandle_t s_task_handle;

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    //Notify that ADC continuous driver has done enough number of conversions
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}
#endif


#if CONFIG_ONESHOT
adc_oneshot_unit_handle_t
#elif CONFIG_CONTINUOUS
adc_continuous_handle_t
#endif
init_adc()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
#if CONFIG_ONESHOT
    //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));
#elif CONFIG_CONTINUOUS
    //-------------ADC1 Init---------------//
    adc_continuous_handle_t adc1_handle;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = 100,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 611,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    adc_digi_pattern_config_t adc_pattern[1] = {0};
    dig_cfg.pattern_num = 1;
    adc_pattern[0].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[0].channel = EXAMPLE_ADC1_CHAN0;
    adc_pattern[0].unit = ADC_UNIT_1;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adc1_handle, &dig_cfg));

    //-------------ADC1 Start---------------//
     adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc1_handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(adc1_handle));
#endif

    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Set duty to 50%
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    return adc1_handle;
}

int compare (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}

#if CONFIG_WIFI
/**
 * from https://medium.com/@fatehsali517/how-to-connect-esp32-to-wifi-using-esp-idf-iot-development-framework-d798dc89f0d6
*/
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,void *event_data){
    if(event_id == WIFI_EVENT_STA_START)
    {
    printf("WIFI CONNECTING....\n");
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
    printf("WiFi CONNECTED\n");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
    printf("WiFi lost connection\n");
    if(retry_num<5){esp_wifi_connect();retry_num++;printf("Retrying to Connect...\n");}
    }
    else if (event_id == IP_EVENT_STA_GOT_IP)
    {
    printf("Wifi got IP...\n\n");
    }
}

/**
 * from https://medium.com/@fatehsali517/how-to-connect-esp32-to-wifi-using-esp-idf-iot-development-framework-d798dc89f0d6
*/
void wifi_connection()
{
    esp_netif_init(); //network interdace initialization
    esp_event_loop_create_default(); //responsible for handling and dispatching events
    my_sta = esp_netif_create_default_wifi_sta(); //sets up necessary data structs for wifi station interface
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();//sets up wifi wifi_init_config struct with default values
    esp_wifi_init(&wifi_initiation); //wifi initialised with dafault wifi_initiation
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);//creating event handler register for wifi
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);//creating event handler register for ip event
    wifi_config_t wifi_configuration ={ //struct wifi_config_t var wifi_configuration
    .sta= {
        .ssid = "",
        .password= "", /*we are sending a const char of ssid and password which we will strcpy in following line so leaving it blank*/ 
    }//also this part is used if you donot want to use Kconfig.projbuild
    };
    strcpy((char*)wifi_configuration.sta.ssid, CONFIG_WIFI_SSID); // copy chars from hardcoded configs to struct
    strcpy((char*)wifi_configuration.sta.password,CONFIG_WIFI_PASSWORD);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);//setting up configs when event ESP_IF_WIFI_STA
    esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(DEFAULT_PS_MODE);
    esp_wifi_set_mode(WIFI_MODE_STA);//station mode selected
    esp_wifi_start();//start connection with configurations provided in funtion
    esp_wifi_connect(); //connect with saved ssid and pass
    printf( "wifi_init_softap finished. SSID:%s", CONFIG_WIFI_SSID);

    wifi_ready = true;
}

/*
 * @brief Event handler registered to receive MQTT events (from https://github.com/espressif/esp-idf/blob/a5b261f699808efdacd287adbded5b718dffd14e/examples/protocols/mqtt/ws/main/app_main.c)
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/**
 * from https://github.com/espressif/esp-idf/blob/a5b261f699808efdacd287adbded5b718dffd14e/examples/protocols/mqtt/ws/main/app_main.c
*/
static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    mqtt_ready = true;
}
#endif



void do_loop(
#if CONFIG_ONESHOT
    adc_oneshot_unit_handle_t
#elif CONFIG_CONTINUOUS
    adc_continuous_handle_t
#endif
    adc_handle)
{
    printf("LOG:ADC_READ\n");
    int adc_median;
    int trash;
#if CONFIG_ONESHOT
    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    for (int i = 0; i < SKIP_AMOUNT; i += 1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, EXAMPLE_ADC1_CHAN0, &trash));
        ESP_LOGI(TAG, "TRASHING: ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, trash);
    }
    for (int i = 0; i < SAMPLE_AMOUNT; i += 1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[i]));
        ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[i]);
    }
    qsort(adc_raw, SAMPLE_AMOUNT, sizeof(int), compare);
    adc_median = adc_raw[SAMPLE_AMOUNT/2];
#elif CONFIG_CONTINUOUS
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[256] = {0};
    memset(result, 0xcc, 256);

    char unit[] = EXAMPLE_ADC_UNIT_STR(ADC_UNIT_1);

    ret = adc_continuous_read(adc_handle, result, 256, &ret_num, 10);

    if (ret == ESP_OK) {
        ESP_LOGI("ADC", "ret is %x, ret_num is %"PRIu32" bytes", ret, ret_num);
        for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
            uint32_t chan_num = EXAMPLE_ADC_GET_CHANNEL(p);
            uint32_t data = EXAMPLE_ADC_GET_DATA(p);
            /* Check the channel number validation, the data is invalid if the channel num exceed the maximum channel */
            if (chan_num < SOC_ADC_CHANNEL_NUM(EXAMPLE_ADC_UNIT)) {
                ESP_LOGI(TAG, "Unit: %s, Channel: %"PRIu32", Value: %"PRIx32, unit, chan_num, data);
            } else {
                ESP_LOGW(TAG, "Invalid data [%s_%"PRIu32"_%"PRIx32"]", unit, chan_num, data);
            }
        }
        /**
         * Because printing is slow, so every time you call `ulTaskNotifyTake`, it will immediately return.
         * To avoid a task watchdog timeout, add a delay here. When you replace the way you process the data,
         * usually you don't need this delay (as this task will block for a while).
         */
        vTaskDelay(1);
    } else {
        ESP_LOGE("ADC", "read not ok: %s", esp_err_to_name(ret));
    }
#endif
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_median);
    
#if CONFIG_ESP_NOW
    // Start WiFi now since WiFi and ADC are not able to run simultaneously!
    printf("LOG:WIFI_INIT\n");
    example_wifi_init();
    printf("LOG:ESPNOW_INIT\n");
    initESP_NOW();

    printf("Hello:sender:%s\n", CONFIG_ESP_LOGGER_VERSION);
    
    printf("LOG:WAIT_FOR_WIFI_AND_ESPNOW_READY\n");
    while(!wifi_ready || !espnow_ready) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // Generate UUID for identification of sent data
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);

    uint8_t mac_address[6] = {0};
    esp_read_mac(mac_address, ESP_MAC_WIFI_STA);
    char mac_str[MAC_LENGTH];
    mac_unparse(mac_address, mac_str);

    printf("LOG:READY\n");
    printf("READY\n");

    printf("ADC_VALUE:%d;%s;%s\n", adc_median, uuid_str, mac_str);
    send_message(adc_median, uuid_str, mac_str);

    // deinit wifi to properly read ADC next time
    esp_wifi_stop();
    esp_wifi_deinit();    
#elif CONFIG_WIFI
    printf("LOG:WIFI_INIT\n");
    wifi_connection();
    printf("LOG:MQTT_INIT\n");
    mqtt_app_start();

    printf("Hello:sender:%s\n", CONFIG_ESP_LOGGER_VERSION);
    
    printf("LOG:WAIT_FOR_WIFI_AND_MQTT_READY\n");
    while(!wifi_ready || !mqtt_ready) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    printf("LOG:READY\n");
    printf("READY\n");

    printf("ADC_VALUE:%d\n", adc_median);
    char value_str[5];
    sprintf(value_str, "%d", adc_median);
    esp_mqtt_client_publish(mqtt_client, "/soilmoisture", value_str, 0, 0, 0);

    // deinit wifi to properly read ADC next time
    esp_wifi_stop();
    esp_wifi_deinit();  
    esp_netif_destroy(my_sta);
#endif
}

void app_main()
{
    printf("LOG:MAIN_ENTRY\n");

    #if !CONFIG_NO_SLEEP
        //Increment boot number and print it every reboot
        ++bootCount;
        printf("LOG:BOOT;%d\n", bootCount);

        //Print the wakeup reason for ESP32
        print_wakeup_reason();

        /*
        First we configure the wake up source
        We set our ESP32 to wake up every 10 seconds
        */
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
        printf("Setup ESP32 to sleep for every %d Seconds\n", TIME_TO_SLEEP);

#if CONFIG_ONESHOT
        adc_oneshot_unit_handle_t adc_handle = init_adc();
#elif CONFIG_CONTINUOUS
        s_task_handle = xTaskGetCurrentTaskHandle();
        adc_continuous_handle_t adc_handle = init_adc();
#endif
        do_loop(adc_handle);

        printf("Going to sleep now\n");
        fflush(stdout);
        esp_deep_sleep_start();
    #else
#if CONFIG_ONESHOT
        adc_oneshot_unit_handle_t adc_handle = init_adc();
#elif CONFIG_CONTINUOUS
        s_task_handle = xTaskGetCurrentTaskHandle();
        adc_continuous_handle_t adc_handle = init_adc();
#endif
        while(true) {
            do_loop(adc_handle);
            // wait for 10 seconds and send data again
            vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
        }
    #endif
}