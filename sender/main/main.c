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
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "main.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (5) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_12_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (128) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY          (10000) // Frequency in Hertz. Set frequency at 5 kHz

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
//ADC1 Channel
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_2

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_0

const static char *TAG = "ADC";

static int adc_raw[2][10];
static int voltage[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);


uint8_t broadcastAddress[] = {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t peerAddress[] = {0x40, 0x4C, 0xCA, 0x41, 0x16, 0xBC};
esp_now_peer_info_t peerInfo;
bool wifi_ready = false;
bool espnow_ready = false;
bool button_pressed = false;

// Should be used when multiple types of data are sent
// typedef struct struct_message {
//     char a[85];
// } struct_message;

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

void send_message(int value) {
    char text[85];
    snprintf(&text, 10, "%d", value);
    // struct_message x = {a: {text}}; // Should be used when multiple types of data are sent
    esp_err_t result = esp_now_send(peerAddress, (uint8_t *) &text, sizeof(int));

    if (result == ESP_OK) {
        printf("Data sent successfully\n");
    }
    else {
        printf("Error sending the data\n");
    }
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
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

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
    ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw[0][0]);
    
    // Start WiFi now since WiFi and ADC are not able to run simultaneously!
    example_wifi_init();
    initESP_NOW();

    printf("Hello:sender:%s\n", CONFIG_ESP_LOGGER_VERSION);

    while(!wifi_ready || !espnow_ready) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    printf("READY\n");

    printf("ADC_VALUE:%d\n", adc_raw[0][0]);
    send_message(adc_raw[0][0]);
}