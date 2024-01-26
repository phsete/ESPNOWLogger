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
#include "main.h"
#include "uuid.h"

#if CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif /*CONFIG_POWER_SAVE_MODEM*/

#define MAC_LENGTH 18

uint8_t broadcastAddress[] = {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t peerAddress[] = {0x40, 0x4C, 0xCA, 0x41, 0x2A, 0xC4};
esp_now_peer_info_t peerInfo;

typedef struct MyMessageType {
    int value;
    char uuid[37];
    char mac_address[17];
};
struct MyMessageType received_message;

void onReceiveData(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    uint8_t *mac = recv_info->src_addr;
    // printf("Data(%d): %s\n", sizeof(data), data);

    // printf("Received: %s\n", data);

    // Extract CRC from received data
    char crc_char[9];
    strncpy(crc_char, (const char*)(data + strlen((const char*)data) - 8), 8);
    crc_char[8] = '\0';
    uint32_t received_crc;
    sscanf(crc_char, "%lx", &received_crc);

    // Extract message without CRC
    char message[strlen((const char*)data)];
    strncpy(message, (const char*)data, strlen((const char*)data));
    int data_size = strlen((const char*)data);
    message[data_size-8] = '\0';

    // Calculate CRC over the extracted message
    uint32_t calculated_crc = esp_crc32_le(0, (uint8_t *)message, strlen(message));

    // Compare CRC values
    bool is_crc_equal = (received_crc == calculated_crc);

    // Extract other fields from the message
    char value_str[5];
    char uuid[UUID_STR_LEN + 1];
    char mac_address[MAC_LENGTH + 1];
    sscanf(message, "%[^;];%[^;];%s", value_str, uuid, mac_address);

    // Uncomment the following lines for debugging
    // printf("Message: %s\n", message);
    // printf("Received CRC: %lx\n", received_crc);
    // printf("Calculated CRC: %lx\n", calculated_crc);
    // printf("CRCs equal: %d\n", is_crc_equal);

    // printf("Value: %d\n", atoi(value_str));
    // printf("UUID: %s\n", uuid);
    // printf("MAC: %s\n", mac_address);

    // Print Result
    printf("RECV:%s;%s;%s;%d\n", value_str, uuid, mac_address, is_crc_equal);
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
}

static void initESP_NOW() {
    if (esp_now_init() != ESP_OK) {
        // printf("Error initializing ESP-NOW\n");
        return;
    }
    // printf("BEFORE ESP PEER MEMCPY\n");
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    // printf("AFTER ESP PEER MEMCPY\n");
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        printf("Failed to add peer\n");
        return;
    }
    // printf("AFTER ESP ADD PEER\n");

    esp_now_register_recv_cb(onReceiveData);
}

void app_main()
{
    // printf("STRUCT SIZE: %d \n", sizeof(struct struct_message));
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }

    example_wifi_init();
    // printf("BEFORE ESP NOW INIT\n");
    initESP_NOW();
    // printf("AFTER ESP NOW INIT\n");

    #if CONFIG_NO_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_NONE
        printf("Hello:receiver:%s-NO_SLEEP-EXAMPLE_POWER_SAVE_NONE\n", CONFIG_ESP_LOGGER_VERSION);
    #elif CONFIG_NO_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
        printf("Hello:receiver:%s-NO_SLEEP-EXAMPLE_POWER_SAVE_MIN_MODEM\n", CONFIG_ESP_LOGGER_VERSION);
    #elif CONFIG_NO_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
        printf("Hello:receiver:%s-NO_SLEEP-EXAMPLE_POWER_SAVE_MAX_MODEM\n", CONFIG_ESP_LOGGER_VERSION);
    #elif CONFIG_DEEP_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_NONE
        printf("Hello:receiver:%s-DEEP_SLEEP-EXAMPLE_POWER_SAVE_NONE\n", CONFIG_ESP_LOGGER_VERSION);
    #elif CONFIG_DEEP_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
        printf("Hello:receiver:%s-DEEP_SLEEP-EXAMPLE_POWER_SAVE_MIN_MODEM\n", CONFIG_ESP_LOGGER_VERSION);
    #elif CONFIG_DEEP_SLEEP && CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
        printf("Hello:receiver:%s-DEEP_SLEEP-EXAMPLE_POWER_SAVE_MAX_MODEM\n", CONFIG_ESP_LOGGER_VERSION);
    #endif

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    };
}