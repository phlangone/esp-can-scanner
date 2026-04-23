#include <stdio.h>

#include "nvs_flash.h"

#include "twai_app.h"
#include "wifi_app.h"


void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Start TWAI
    twai_app_config_t initial_cfg = {
        .baudrate = 500000,
        .mode = TWAI_APP_MODE_NORMAL,
        .filter_enabled = false,
        .filter = 0,
        .mask = 0
    };
    ESP_ERROR_CHECK(twai_app_init(&initial_cfg));    

    // Start WiFi
    wifi_app_start();
}
