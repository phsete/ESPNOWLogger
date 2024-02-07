#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
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
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

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

static const char *TAG = "mqttws_example";
esp_mqtt_client_handle_t mqtt_client;
int retry_num=0;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
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
}

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
    int msg_id = esp_mqtt_client_publish(mqtt_client, "/soilmoisture", value_str, 0, 0, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

/**
 * from https://medium.com/@fatehsali517/how-to-connect-esp32-to-wifi-using-esp-idf-iot-development-framework-d798dc89f0d6
*/
void wifi_connection()
{
    esp_netif_init(); //network interdace initialization
    esp_event_loop_create_default(); //responsible for handling and dispatching events
    esp_netif_create_default_wifi_sta(); //sets up necessary data structs for wifi station interface
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
    strcpy((char*)wifi_configuration.sta.ssid, CONFIG_EXAMPLE_WIFI_SSID); // copy chars from hardcoded configs to struct
    strcpy((char*)wifi_configuration.sta.password,CONFIG_EXAMPLE_WIFI_PASSWORD);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);//setting up configs when event ESP_IF_WIFI_STA
    esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(DEFAULT_PS_MODE);
    esp_wifi_set_mode(WIFI_MODE_STA);//station mode selected
    esp_wifi_start();//start connection with configurations provided in funtion
    esp_wifi_connect(); //connect with saved ssid and pass
    printf( "wifi_init_softap finished. SSID:%s  password:%s", CONFIG_EXAMPLE_WIFI_SSID,CONFIG_EXAMPLE_WIFI_PASSWORD);
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

    wifi_connection();
    // printf("BEFORE ESP NOW INIT\n");
    initESP_NOW();
    // printf("AFTER ESP NOW INIT\n");

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();

    printf("Hello:receiver:%s\n", CONFIG_ESP_LOGGER_VERSION);

    while (1) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    };
}