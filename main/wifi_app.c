#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"

#include "wifi_app.h"
#include "http_server.h"

/** @brief Tag utilizada nas mensagens de log do módulo Wi-Fi. */
static const char TAG[] = "wifi_app";

/** @brief Fila principal de mensagens da aplicação Wi-Fi. */
static QueueHandle_t wifi_app_queue_handle;

/** @brief Interface de rede da estação Wi-Fi. */
esp_netif_t* esp_netif_sta = NULL;
/** @brief Interface de rede do Access Point Wi-Fi. */
esp_netif_t* esp_netif_ap = NULL;

/**
 * @brief Handler de eventos da aplicação Wi-Fi.
 *
 * @param arg Ponteiro para argumento de contexto do handler.
 * @param event_base Base do evento recebido.
 * @param event_id Identificador do evento recebido.
 * @param event_data Ponteiro para os dados associados ao evento.
 */
static void wifi_app_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
                break;

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
                break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
                break;
        }
    }
}

/**
 * @brief Inicializa o registro dos handlers de eventos Wi-Fi e IP.
 */
static void wifi_app_event_handler_init(void)
{
    /* Loop de eventos do driver Wi-Fi. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Registro dos handlers de conexão. */
    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, &instance_wifi_event));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, &instance_ip_event));
}

/**
 * @brief Inicializa a pilha TCP/IP e a configuração padrão do Wi-Fi.
 */
static void wifi_app_default_wifi_init(void)
{
    /* Inicializa a pilha TCP/IP. */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Configuração padrão do Wi-Fi. A operação deve ocorrer nesta ordem. */
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    esp_netif_sta = esp_netif_create_default_wifi_sta();
    esp_netif_ap = esp_netif_create_default_wifi_ap();
}

/**
 * @brief Configura o Access Point Wi-Fi e define o endereço IP estático do SoftAP.
 */
static void wifi_app_soft_ap_config(void)
{
    /* Configuração do ponto de acesso Wi-Fi. */
    wifi_config_t ap_config =
    {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .password = WIFI_AP_PASSWORD,
            .channel = WIFI_AP_CHANNEL,
            .ssid_hidden = WIFI_AP_SSID_HIDDEN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .beacon_interval = WIFI_AP_BEACON_INTERVAL,
        },
    };

    /* Configuração do servidor DHCP do Access Point. */
    esp_netif_ip_info_t ap_ip_info;
    memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));

    esp_netif_dhcps_stop(esp_netif_ap);                                         ///> Must stop the server before any updates
    inet_pton(AF_INET, WIFI_AP_IP, &ap_ip_info.ip);                             ///> Assing access point's static IP, gateway and netmask
    inet_pton(AF_INET, WIFI_AP_GATEWAY, &ap_ip_info.gw);
    inet_pton(AF_INET, WIFI_AP_NETMASK, &ap_ip_info.netmask);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));          ///> Statically configure the network interface
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));                       ///> Start the DHCP server (for connecting stations)

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));                        ///> Setting the mode as Access Point / Station Mode
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));           ///> Set the configuration defined at ap_config
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_AP_BANDWIDTH));     ///> Default bandwidth 20MHz
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_STA_POWER_SAVE));                      ///> Power save set to "NONE"
}

/**
 * @brief Task principal da aplicação Wi-Fi.
 *
 * @param pvParameter Parâmetro opcional da task, não utilizado.
 */
static void wifi_app_task(void *pvParameter)
{
    wifi_app_queue_message_t msg;

    (void)pvParameter;

    /* Inicializa o handler de eventos. */
    wifi_app_event_handler_init();

    /* Inicializa a pilha TCP/IP e a configuração Wi-Fi. */
    wifi_app_default_wifi_init();

    /* Configura o SoftAP. */
    wifi_app_soft_ap_config();

    /* Inicializa o Wi-Fi. */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Envia a primeira mensagem de inicialização do servidor HTTP. */
    wifi_app_send_message(WIFI_APP_MSG_START_HTTP_SERVER);

    for(;;)
    {
        if (xQueueReceive(wifi_app_queue_handle, &msg, portMAX_DELAY))
        {
            switch (msg.msgID)
            {
                case WIFI_APP_MSG_START_HTTP_SERVER:
                    ESP_LOGI(TAG, "WIFI_APP_MSG_START_HTTP_SERVER");
                    http_server_start();
                    break;

                case WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER:
                    ESP_LOGI(TAG, "WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER");
                    break;

                case WIFI_APP_MSG_STA_CONNECTED_GOT_IP:
                    ESP_LOGI(TAG, "WIFI_APP_MSG_STA_CONNECTED_GOT_IP");
                    break;

                default:
                    break;
            }
        }
    }
}

/**
 * @brief Envia uma mensagem para a fila principal da aplicação Wi-Fi.
 *
 * @param msgID Identificador da mensagem a ser enviada.
 *
 * @return pdTRUE se a mensagem foi enviada com sucesso.
 * @return pdFALSE em caso de falha no envio.
 */
BaseType_t wifi_app_send_message(wifi_app_message_e msgID)
{
	wifi_app_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(wifi_app_queue_handle, &msg, portMAX_DELAY);
}

/**
 * @brief Inicializa a aplicação Wi-Fi e cria sua task principal.
 */
void wifi_app_start(void)
{
    ESP_LOGI(TAG, "STARTING WIFI APPLICATION");

    /* Desabilita as mensagens padrão de log do driver Wi-Fi. */
    esp_log_level_set("wifi", ESP_LOG_NONE);

    /* Cria a fila de mensagens da aplicação. */
    wifi_app_queue_handle = xQueueCreate(3, sizeof(wifi_app_queue_message_t));

    /* Cria a task principal da aplicação Wi-Fi. */
    xTaskCreatePinnedToCore(&wifi_app_task,
                            "wifi_app_task",
                            WIFI_APP_TASK_STACK_SIZE,
                            NULL,
                            WIFI_APP_TASK_PRIORITY,
                            NULL,
                            WIFI_APP_TASK_CORE_ID);
}
