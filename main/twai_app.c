#include <inttypes.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/twai.h"

#include "twai_app.h"

/* Tag used for ESP serial console messages */
static const char TAG[] = "twai_app";

/**
 * @brief Tipos de comando aceitos pela task única dona do TWAI.
 */
typedef enum
{
    TWAI_APP_CMD_SEND = 0,    /**< Solicitação de transmissão de frame. */
    TWAI_APP_CMD_APPLY_CONFIG /**< Solicitação de reconfiguração do driver. */
} twai_app_cmd_type_t;

/**
 * @brief Estrutura interna de comando da aplicação TWAI.
 */
typedef struct
{
    twai_app_cmd_type_t type; /**< Tipo do comando. */
    union
    {
        twai_app_tx_request_t tx; /**< Payload de transmissão. */
        twai_app_config_t cfg;    /**< Payload de configuração. */
    } data;
} twai_app_cmd_t;

/* Queue handle used to manipulate the command queue */
static QueueHandle_t s_cmd_queue = NULL;

/* Mutex used to protect shared status and RX ring access */
static SemaphoreHandle_t s_lock = NULL;

/* TWAI owner task handle */
static TaskHandle_t s_owner_task_handle = NULL;

/* Internal driver state */
static bool s_driver_installed = false;
static twai_app_config_t s_active_cfg = {0};
static twai_app_status_t s_status = {0};

/* RX ring buffer */
static twai_app_rx_frame_t s_rx_ring[TWAI_APP_RX_RING_LEN];
static size_t s_rx_wr = 0;
static size_t s_rx_rd = 0;
static size_t s_rx_count = 0;

/**
 * @brief Obtém o mutex interno da aplicação.
 */
static void twai_app_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

/**
 * @brief Libera o mutex interno da aplicação.
 */
static void twai_app_unlock(void)
{
    xSemaphoreGive(s_lock);
}

/**
 * @brief Converte internamente o estado TWAI para texto.
 *
 * @param state Estado a ser convertido.
 *
 * @return Ponteiro para string constante correspondente ao estado informado.
 */
static const char *state_to_string_internal(twai_app_state_t state)
{
    switch (state)
    {
        case TWAI_APP_STATE_RUNNING:
            return "running";

        case TWAI_APP_STATE_STOPPED:
            return "stopped";

        case TWAI_APP_STATE_BUS_OFF:
            return "bus-off";

        case TWAI_APP_STATE_RECOVERING:
            return "recovering";

        default:
            return "stopped";
    }
}

const char *twai_app_state_to_string(twai_app_state_t state)
{
    return state_to_string_internal(state);
}

const char *twai_app_mode_to_string(twai_app_mode_t mode)
{
    switch (mode)
    {
        case TWAI_APP_MODE_NORMAL:
            return "normal";

        case TWAI_APP_MODE_LISTEN_ONLY:
            return "listen-only";

        case TWAI_APP_MODE_NO_ACK:
            return "no-ack";

        default:
            return "normal";
    }
}

bool twai_app_parse_mode(const char *text, twai_app_mode_t *out_mode)
{
    if ((text == NULL) || (out_mode == NULL))
    {
        return false;
    }

    if (strcasecmp(text, "normal") == 0)
    {
        *out_mode = TWAI_APP_MODE_NORMAL;
        return true;
    }

    if ((strcasecmp(text, "listen-only") == 0) || (strcasecmp(text, "listen_only") == 0))
    {
        *out_mode = TWAI_APP_MODE_LISTEN_ONLY;
        return true;
    }

    if ((strcasecmp(text, "no-ack") == 0) || (strcasecmp(text, "no_ack") == 0))
    {
        *out_mode = TWAI_APP_MODE_NO_ACK;
        return true;
    }

    return false;
}

/**
 * @brief Converte o modo TWAI da aplicação para o modo do driver.
 *
 * @param mode Modo da aplicação.
 *
 * @return Modo equivalente do driver TWAI.
 */
static twai_mode_t mode_to_driver(twai_app_mode_t mode)
{
    switch (mode)
    {
        case TWAI_APP_MODE_LISTEN_ONLY:
            return TWAI_MODE_LISTEN_ONLY;

        case TWAI_APP_MODE_NO_ACK:
            return TWAI_MODE_NO_ACK;

        case TWAI_APP_MODE_NORMAL:
        default:
            return TWAI_MODE_NORMAL;
    }
}

/**
 * @brief Converte baudrate nominal para a configuração de timing do driver.
 *
 * @param baudrate Baudrate desejado em bits por segundo.
 * @param out_timing Ponteiro para a estrutura de timing de saída.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se o ponteiro for inválido.
 * @return ESP_ERR_NOT_SUPPORTED se o baudrate não for suportado.
 */
static esp_err_t baudrate_to_timing(uint32_t baudrate, twai_timing_config_t *out_timing)
{
    if (out_timing == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (baudrate)
    {
        case 25000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_25KBITS();
            break;

        case 50000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();
            break;

        case 100000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
            break;

        case 125000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
            break;

        case 250000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
            break;

        case 500000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
            break;

        case 800000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
            break;

        case 1000000:
            *out_timing = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
            break;

        default:
            return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

/**
 * @brief Insere um frame no buffer circular de recepção.
 *
 * @param frame Ponteiro para o frame a ser armazenado.
 */
static void rx_ring_push(const twai_app_rx_frame_t *frame)
{
    if (frame == NULL)
    {
        return;
    }

    twai_app_lock();

    s_rx_ring[s_rx_wr] = *frame;
    s_rx_wr = (s_rx_wr + 1U) % TWAI_APP_RX_RING_LEN;

    if (s_rx_count == TWAI_APP_RX_RING_LEN)
    {
        s_rx_rd = (s_rx_rd + 1U) % TWAI_APP_RX_RING_LEN;
    }
    else
    {
        s_rx_count++;
    }

    twai_app_unlock();
}

size_t twai_app_pop_rx_frames(twai_app_rx_frame_t *out_frames, size_t max_frames)
{
    size_t copied = 0;

    if ((out_frames == NULL) || (max_frames == 0))
    {
        return 0;
    }

    twai_app_lock();

    while ((copied < max_frames) && (s_rx_count > 0))
    {
        out_frames[copied] = s_rx_ring[s_rx_rd];
        s_rx_rd = (s_rx_rd + 1U) % TWAI_APP_RX_RING_LEN;
        s_rx_count--;
        copied++;
    }

    twai_app_unlock();

    return copied;
}

/**
 * @brief Verifica se um identificador passa no filtro em software.
 *
 * @param id Identificador CAN recebido.
 *
 * @return true se o frame for aceito.
 * @return false se o frame for rejeitado pelo filtro.
 */
static bool rx_matches_software_filter(uint32_t id)
{
    bool match = true;

    twai_app_lock();

    if (s_active_cfg.filter_enabled)
    {
        match = ((id & s_active_cfg.mask) == (s_active_cfg.filter & s_active_cfg.mask));
    }

    twai_app_unlock();

    return match;
}

/**
 * @brief Atualiza o snapshot de status a partir das informações atuais do driver.
 */
static void update_status_from_driver(void)
{
    twai_status_info_t info;

    if (!s_driver_installed)
    {
        return;
    }

    if (twai_get_status_info(&info) == ESP_OK)
    {
        twai_app_lock();
        s_status.tx_error_counter = info.tx_error_counter;
        s_status.rx_error_counter = info.rx_error_counter;
        s_status.rx_missed_count = info.rx_missed_count;
        s_status.rx_overrun_count = info.rx_overrun_count;
        s_status.bus_error_count = info.bus_error_count;
        twai_app_unlock();
    }
}

/**
 * @brief Para e desinstala o driver TWAI interno.
 *
 * @return ESP_OK em caso de sucesso.
 * @return Outro código de erro caso a desinstalação falhe.
 */
static esp_err_t twai_driver_shutdown_internal(void)
{
    esp_err_t ret;

    if (!s_driver_installed)
    {
        return ESP_OK;
    }

    ret = twai_stop();
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGW(TAG, "Falha ao parar driver TWAI: %s", esp_err_to_name(ret));
    }

    ret = twai_driver_uninstall();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Falha ao desinstalar driver TWAI: %s", esp_err_to_name(ret));
        return ret;
    }

    s_driver_installed = false;

    twai_app_lock();
    s_status.can_state = TWAI_APP_STATE_STOPPED;
    twai_app_unlock();

    return ESP_OK;
}

/**
 * @brief Instala e inicia o driver TWAI com a configuração informada.
 *
 * @param cfg Ponteiro para a configuração desejada.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se o ponteiro for inválido.
 * @return Outro código de erro em caso de falha na instalação ou partida.
 */
static esp_err_t twai_driver_start_internal(const twai_app_config_t *cfg)
{
    twai_general_config_t g_config;
    twai_timing_config_t t_config;
    twai_filter_config_t f_config;
    esp_err_t ret;

    if (cfg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = baudrate_to_timing(cfg->baudrate, &t_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    g_config = (twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT(
        TWAI_APP_TX_GPIO,
        TWAI_APP_RX_GPIO,
        mode_to_driver(cfg->mode)
    );

    f_config = (twai_filter_config_t)TWAI_FILTER_CONFIG_ACCEPT_ALL();

    g_config.tx_queue_len = 16;
    g_config.rx_queue_len = 32;
    g_config.alerts_enabled =
        TWAI_ALERT_TX_SUCCESS |
        TWAI_ALERT_TX_FAILED |
        TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_ERR_PASS |
        TWAI_ALERT_BUS_OFF |
        TWAI_ALERT_RECOVERY_IN_PROGRESS |
        TWAI_ALERT_BUS_RECOVERED |
        TWAI_ALERT_RX_QUEUE_FULL;

    ret = twai_driver_install(&g_config, &t_config, &f_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = twai_start();
    if (ret != ESP_OK)
    {
        twai_driver_uninstall();
        return ret;
    }

    s_driver_installed = true;

    twai_app_lock();
    s_active_cfg = *cfg;
    s_status.can_state = TWAI_APP_STATE_RUNNING;
    s_status.mode = cfg->mode;
    s_status.baudrate = cfg->baudrate;
    s_status.filter_enabled = cfg->filter_enabled;
    s_status.filter = cfg->filter;
    s_status.mask = cfg->mask;
    twai_app_unlock();

    update_status_from_driver();

    ESP_LOGI(TAG, "TWAI iniciado");

    return ESP_OK;
}

/**
 * @brief Reconfigura o driver TWAI aplicando uma nova configuração.
 *
 * @param cfg Ponteiro para a nova configuração.
 *
 * @return ESP_OK em caso de sucesso.
 * @return Outro código de erro em caso de falha.
 */
static esp_err_t twai_driver_reconfigure_internal(const twai_app_config_t *cfg)
{
    esp_err_t ret;

    ret = twai_driver_shutdown_internal();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = twai_driver_start_internal(cfg);
    return ret;
}

/**
 * @brief Processa todos os frames pendentes na fila RX do driver.
 */
static void process_rx_messages(void)
{
    while (s_driver_installed)
    {
        twai_message_t msg;
        esp_err_t ret = twai_receive(&msg, 0);

        if (ret == ESP_ERR_TIMEOUT)
        {
            break;
        }

        if (ret == ESP_ERR_INVALID_STATE)
        {
            break;
        }

        if (ret != ESP_OK)
        {
            twai_app_lock();
            s_status.err_count++;
            twai_app_unlock();
            break;
        }

        if (!rx_matches_software_filter(msg.identifier))
        {
            continue;
        }

        twai_app_rx_frame_t frame = {0};
        frame.timestamp_us = (uint64_t)esp_timer_get_time();
        frame.id = msg.identifier;
        frame.extended = msg.extd ? true : false;
        frame.remote = msg.rtr ? true : false;
        frame.dlc = msg.data_length_code;
        frame.err = false;
        memcpy(frame.data, msg.data, sizeof(frame.data));

        rx_ring_push(&frame);

        twai_app_lock();
        s_status.rx_count++;
        twai_app_unlock();
    }
}

/**
 * @brief Processa os alertas pendentes do driver TWAI.
 */
static void process_alerts(void)
{
    while (s_driver_installed)
    {
        uint32_t alerts = 0;
        esp_err_t ret = twai_read_alerts(&alerts, 0);

        if (ret == ESP_ERR_TIMEOUT)
        {
            break;
        }

        if (ret == ESP_ERR_INVALID_STATE)
        {
            break;
        }

        if (ret != ESP_OK)
        {
            break;
        }

        twai_app_lock();

        if (alerts & TWAI_ALERT_BUS_OFF)
        {
            s_status.can_state = TWAI_APP_STATE_BUS_OFF;
            s_status.err_count++;
            ESP_LOGW(TAG, "TWAI em BUS-OFF");
        }

        if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS)
        {
            s_status.can_state = TWAI_APP_STATE_RECOVERING;
        }

        if (alerts & TWAI_ALERT_BUS_RECOVERED)
        {
            s_status.can_state = TWAI_APP_STATE_STOPPED;
            ESP_LOGI(TAG, "Barramento TWAI recuperado");
        }

        if (alerts & TWAI_ALERT_ERR_PASS)
        {
            s_status.err_count++;
        }

        if (alerts & TWAI_ALERT_BUS_ERROR)
        {
            s_status.err_count++;
        }

        if (alerts & TWAI_ALERT_RX_QUEUE_FULL)
        {
            s_status.err_count++;
            ESP_LOGW(TAG, "Fila RX do TWAI cheia");
        }

        if (alerts & TWAI_ALERT_TX_FAILED)
        {
            s_status.err_count++;
            ESP_LOGW(TAG, "Falha na transmissão TWAI");
        }

        twai_app_unlock();
    }
}

/**
 * @brief Processa um comando recebido pela fila interna da aplicação.
 *
 * @param cmd Ponteiro para o comando a ser processado.
 */
static void process_command(const twai_app_cmd_t *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    switch (cmd->type)
    {
        case TWAI_APP_CMD_SEND:
        {
            twai_message_t msg = {0};
            esp_err_t ret;

            msg.identifier = cmd->data.tx.id;
            msg.extd = cmd->data.tx.extended ? 1U : 0U;
            msg.rtr = cmd->data.tx.remote ? 1U : 0U;
            msg.data_length_code = cmd->data.tx.dlc;
            memcpy(msg.data, cmd->data.tx.data, sizeof(msg.data));

            ret = twai_transmit(&msg, pdMS_TO_TICKS(100));

            twai_app_lock();
            if (ret == ESP_OK)
            {
                s_status.tx_count++;
            }
            else
            {
                s_status.err_count++;
            }
            twai_app_unlock();

            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Falha ao transmitir frame TWAI: %s", esp_err_to_name(ret));
            }

            break;
        }

        case TWAI_APP_CMD_APPLY_CONFIG:
        {
            esp_err_t ret = twai_driver_reconfigure_internal(&cmd->data.cfg);

            if (ret != ESP_OK)
            {
                twai_app_lock();
                s_status.err_count++;
                twai_app_unlock();

                ESP_LOGW(TAG, "Falha ao reconfigurar TWAI: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI(TAG, "Configuração TWAI aplicada");
            }

            break;
        }

        default:
            break;
    }
}

/**
 * @brief Task principal da aplicação TWAI.
 *
 * @param pvParameter Parâmetro opcional da task.
 */
static void twai_app_task(void *pvParameter)
{
    twai_app_cmd_t cmd;
    int64_t last_status_update_us = 0;

    (void)pvParameter;

    for (;;)
    {
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            process_command(&cmd);
        }

        process_rx_messages();
        process_alerts();

        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_status_update_us) >= 100000)
        {
            update_status_from_driver();
            last_status_update_us = now_us;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t twai_app_init(const twai_app_config_t *initial_cfg)
{
    esp_err_t ret;
    twai_app_config_t default_cfg =
    {
        .baudrate = 500000,
        .mode = TWAI_APP_MODE_NORMAL,
        .filter_enabled = false,
        .filter = 0,
        .mask = 0
    };

    const twai_app_config_t *cfg = initial_cfg ? initial_cfg : &default_cfg;

    if (s_cmd_queue != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    s_cmd_queue = xQueueCreate(TWAI_APP_CMD_QUEUE_LEN, sizeof(twai_app_cmd_t));
    if (s_cmd_queue == NULL)
    {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.can_state = TWAI_APP_STATE_STOPPED;
    s_status.mode = cfg->mode;
    s_status.baudrate = cfg->baudrate;
    s_status.filter_enabled = cfg->filter_enabled;
    s_status.filter = cfg->filter;
    s_status.mask = cfg->mask;

    ret = twai_driver_start_internal(cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Falha na inicialização do TWAI: %s", esp_err_to_name(ret));
    }

    if (xTaskCreatePinnedToCore(
            twai_app_task,
            "twai_app_task",
            TWAI_APP_TASK_STACK_SIZE,
            NULL,
            TWAI_APP_TASK_PRIORITY,
            &s_owner_task_handle,
            TWAI_APP_TASK_CORE_ID) != pdPASS)
    {
        twai_driver_shutdown_internal();
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t twai_app_send(const twai_app_tx_request_t *req)
{
    twai_app_cmd_t cmd = {0};

    if ((req == NULL) || (req->dlc > 8))
    {
        return ESP_ERR_INVALID_ARG;
    }

    cmd.type = TWAI_APP_CMD_SEND;
    cmd.data.tx = *req;

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t twai_app_apply_config(const twai_app_config_t *cfg)
{
    twai_app_cmd_t cmd = {0};

    if (cfg == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cmd.type = TWAI_APP_CMD_APPLY_CONFIG;
    cmd.data.cfg = *cfg;

    if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t twai_app_get_status(twai_app_status_t *out_status)
{
    if (out_status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    twai_app_lock();
    *out_status = s_status;
    twai_app_unlock();

    return ESP_OK;
}
