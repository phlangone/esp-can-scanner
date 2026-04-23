#ifndef MAIN_WIFI_APP_H_
#define MAIN_WIFI_APP_H_

#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

// WiFi application task
#define WIFI_APP_TASK_STACK_SIZE        4096
#define WIFI_APP_TASK_PRIORITY          5
#define WIFI_APP_TASK_CORE_ID           0

// WiFi application settings
#define WIFI_AP_SSID                    "ESP32_AP"         // AP name
#define WIFI_AP_PASSWORD                "password"         // AP password
#define WIFI_AP_CHANNEL                 1                  // AP channel
#define WIFI_AP_SSID_HIDDEN             0                  // AP visibility
#define WIFI_AP_MAX_CONNECTIONS         5                  // AP max clientes
#define WIFI_AP_IP                      "192.168.0.1"      // AP default IP
#define WIFI_AP_GATEWAY                 "192.168.0.1"      // AP default gateway
#define WIFI_AP_NETMASK                 "255.255.255.0"    // AP netmask
#define WIFI_AP_BANDWIDTH               WIFI_BW_HT20       // WiFi bandwidth 20MHz
#define WIFI_STA_POWER_SAVE             WIFI_PS_NONE       // Power save isn't been used
#define WIFI_MAX_SSID_LENGHT            32                 // IEEE standard max 
#define WIFI_MAX_PASSWORD_LENGHT        64                 // IEEE standard max
#define WIFI_MAX_CONNECTION_RETRIES     5                  // Retry number on disconnect
#define WIFI_AP_BEACON_INTERVAL         100                // Recommended 100ms


// netif object for the Station and Access Point
extern esp_netif_t* esp_netif_sta;
extern esp_netif_t* esp_netif_ap;

/**
 * Message for the WiFi application task
 * @note Expand based on your project needs
*/
typedef enum wifi_app_messages
{
    WIFI_APP_MSG_START_HTTP_SERVER = 0,
    WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER,
    WIFI_APP_MSG_STA_CONNECTED_GOT_IP,
} wifi_app_message_e;

/**
 * Structure for the message queue
 * @note Expand if necessary e.g, add another type and parameter required
*/
typedef struct wifi_app_queue_message
{
    wifi_app_message_e msgID;
} wifi_app_queue_message_t;

/**
 * Sends a message to the queue
 * @param msgID message ID from wifi_app_message_e enum.
 * @return pdTRUE if an item was successfully sent to the queue, otherwise pdFALSE.
*/
BaseType_t wifi_app_send_message(wifi_app_message_e msgID);

/**
 * Starts WiFi RTOS task
*/
void wifi_app_start(void);

#endif /* MAIN_WIFI_APP_H_*/