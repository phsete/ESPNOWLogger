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

uint8_t broadcastAddress[] = {0xFF, 0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t peerAddress[] = {0x40, 0x4C, 0xCA, 0x41, 0x2A, 0xC4};
esp_now_peer_info_t peerInfo;

typedef struct struct_message {
    char a[85];
} struct_message;

struct_message my_data;

void onReceiveData(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    uint8_t *mac = recv_info->src_addr;
    memcpy(&my_data, data, sizeof(my_data));
    // printf("** Data Received **\n\n");
    // printf("Received from MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // printf("Length: %d byte(s)\n", len);
    printf("Data: %s\n", my_data.a);
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

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    };
}