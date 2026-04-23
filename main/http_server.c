#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "sys/param.h"

#include "cJSON.h"

#include "twai_app.h"
#include "http_server.h"
#include "wifi_app.h"

/** @brief Tag utilizada nas mensagens de log do módulo HTTP. */
static const char TAG[] = "http_server";

/** @brief Handle da instância principal do servidor HTTP. */
static httpd_handle_t http_server_handle = NULL;

/** @brief Handle da task de monitoramento do servidor HTTP. */
static TaskHandle_t task_http_server_monitor = NULL;

/** @brief Fila de mensagens utilizada pelo monitor do servidor HTTP. */
static QueueHandle_t http_server_monitor_queue_handle;

/** @brief Início do arquivo index.html embarcado na aplicação. */
extern const uint8_t index_html_start[]				asm("_binary_index_html_start");
/** @brief Fim do arquivo index.html embarcado na aplicação. */
extern const uint8_t index_html_end[]				asm("_binary_index_html_end");
/** @brief Início do arquivo styles.css embarcado na aplicação. */
extern const uint8_t styles_css_start[]				asm("_binary_styles_css_start");
/** @brief Fim do arquivo styles.css embarcado na aplicação. */
extern const uint8_t styles_css_end[]				asm("_binary_styles_css_end");
/** @brief Início do arquivo script.js embarcado na aplicação. */
extern const uint8_t script_js_start[]				asm("_binary_script_js_start");
/** @brief Fim do arquivo script.js embarcado na aplicação. */
extern const uint8_t script_js_end[]				asm("_binary_script_js_end");
/** @brief Início do arquivo favicon.ico embarcado na aplicação. */
extern const uint8_t favicon_ico_start[]			asm("_binary_favicon_ico_start");
/** @brief Fim do arquivo favicon.ico embarcado na aplicação. */
extern const uint8_t favicon_ico_end[]			asm("_binary_favicon_ico_end");

/**
 * @brief Task de monitoramento do servidor HTTP.
 *
 * @param parameter Parâmetro opcional da task, não utilizado.
 */
static void http_server_monitor(void *parameter)
{
	http_server_queue_message_t msg;

	(void)parameter;

	for (;;)
	{
		if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY))
		{
			switch (msg.msgID)
			{
				case HTTP_MSG_WIFI_CONNECT_INIT:
					ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");

					break;

				case HTTP_MSG_WIFI_CONNECT_SUCCESS:
					ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");

					break;

				case HTTP_MSG_WIFI_CONNECT_FAIL:
					ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");

					break;

				default:
					break;
			}
		}
	}
}

/**
 * @brief Envia a página principal index.html ao cliente HTTP.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "index.html requested");

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);

	return ESP_OK;
}

/**
 * @brief Envia o arquivo styles.css ao cliente HTTP.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_styles_css_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "styles.css requested");

	httpd_resp_set_type(req, "text/css");
	httpd_resp_send(req, (const char *)styles_css_start, styles_css_end - styles_css_start);

	return ESP_OK;
}

/**
 * @brief Envia o arquivo script.js ao cliente HTTP.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_script_js_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "script.js requested");

	httpd_resp_set_type(req, "application/javascript");
	httpd_resp_send(req, (const char *)script_js_start, script_js_end - script_js_start);

	return ESP_OK;
}

/**
 * @brief Envia o arquivo favicon.ico ao cliente HTTP.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "favicon.ico requested");

	httpd_resp_set_type(req, "image/x-icon");
	httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);

	return ESP_OK;
}

/**
 * @brief Recebe um JSON via HTTP, interpreta os campos e solicita o envio de um frame TWAI.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de tratamento concluído.
 */
static esp_err_t http_server_can_send_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "/api/can/send requested");

    char buf[256] = {0};
    int ret = 0;

    cJSON *root = NULL;
    cJSON *id = NULL;
    cJSON *format = NULL;
    cJSON *type = NULL;
    cJSON *dlc = NULL;
    cJSON *data = NULL;

    twai_app_tx_request_t tx_req = {0};

    if (req->content_len <= 0 || req->content_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ESP_OK;
    }

    ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_OK;
    }

    buf[req->content_len] = '\0';

    root = cJSON_Parse(buf);
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_OK;
    }

    id = cJSON_GetObjectItem(root, "id");
    format = cJSON_GetObjectItem(root, "format");
    type = cJSON_GetObjectItem(root, "type");
    dlc = cJSON_GetObjectItem(root, "dlc");
    data = cJSON_GetObjectItem(root, "data");

    if (!cJSON_IsNumber(id) ||
        !cJSON_IsString(format) ||
        !cJSON_IsString(type) ||
        !cJSON_IsNumber(dlc))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_OK;
    }

    tx_req.id = (uint32_t)id->valuedouble;
    tx_req.extended = (strcmp(format->valuestring, "extended") == 0);
    tx_req.remote = (strcmp(type->valuestring, "remote") == 0);
    tx_req.dlc = (uint8_t)dlc->valueint;

    if (!tx_req.remote && cJSON_IsArray(data))
    {
        int count = cJSON_GetArraySize(data);

        for (int i = 0; i < count && i < 8; i++)
        {
            cJSON *item = cJSON_GetArrayItem(data, i);

            if (cJSON_IsNumber(item))
            {
                tx_req.data[i] = (uint8_t)item->valueint;
            }
        }
    }

    cJSON_Delete(root);

     if (twai_app_send(&tx_req) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "send failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    return ESP_OK;
}

/**
 * @brief Recebe um JSON via HTTP e solicita a aplicação de uma nova configuração TWAI.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de tratamento concluído.
 */
static esp_err_t http_server_can_config_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "/api/can/config requested");

    char buf[256] = {0};
    int ret = 0;

    cJSON *root = NULL;
    cJSON *baudrate = NULL;
    cJSON *mode = NULL;
    cJSON *filter = NULL;
    cJSON *mask = NULL;

    twai_app_config_t cfg = {0};

    if (req->content_len <= 0 || req->content_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ESP_OK;
    }

    ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_OK;
    }

    buf[req->content_len] = '\0';

    root = cJSON_Parse(buf);
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_OK;
    }

    baudrate = cJSON_GetObjectItem(root, "baudrate");
    mode = cJSON_GetObjectItem(root, "mode");
    filter = cJSON_GetObjectItem(root, "filter");
    mask = cJSON_GetObjectItem(root, "mask");

    if (!cJSON_IsNumber(baudrate) || !cJSON_IsString(mode))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_OK;
    }

    cfg.baudrate = (uint32_t)baudrate->valuedouble;

    if (!twai_app_parse_mode(mode->valuestring, &cfg.mode))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid mode");
        return ESP_OK;
    }

    cfg.filter_enabled = false;
    cfg.filter = 0;
    cfg.mask = 0;

    if (!cJSON_IsNull(filter) && !cJSON_IsNull(mask))
    {
        if (!cJSON_IsString(filter) || !cJSON_IsString(mask))
        {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid filter or mask");
            return ESP_OK;
        }

        cfg.filter = (uint32_t)strtoul(filter->valuestring, NULL, 16);
        cfg.mask = (uint32_t)strtoul(mask->valuestring, NULL, 16);
        cfg.filter_enabled = true;
    }

    cJSON_Delete(root);

    if (twai_app_apply_config(&cfg) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    return ESP_OK;
}

/**
 * @brief Lê frames recebidos e o estado atual do barramento para envio à interface web.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_can_rx_handler(httpd_req_t *req)
{
    twai_app_status_t status = {0};
    twai_app_rx_frame_t frames[16] = {0};
    size_t frame_count = 0;

    cJSON *root = NULL;
    cJSON *json_frames = NULL;
    char *json_str = NULL;

    if (twai_app_get_status(&status) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status failed");
        return ESP_OK;
    }

    frame_count = twai_app_pop_rx_frames(frames, 16);

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_OK;
    }

    cJSON_AddStringToObject(root, "can_state", twai_app_state_to_string(status.can_state));

    json_frames = cJSON_AddArrayToObject(root, "frames");
    if (json_frames == NULL)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_OK;
    }

    for (size_t i = 0; i < frame_count; i++)
    {
        cJSON *json_frame = cJSON_CreateObject();
        cJSON *json_data = NULL;
        char timestamp_str[24] = {0};

        if (json_frame == NULL)
        {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
            return ESP_OK;
        }

        /* Timestamp simples em milissegundos desde boot.*/
		snprintf(
			timestamp_str,
			sizeof(timestamp_str),
			"%" PRIu64,
			(uint64_t)(frames[i].timestamp_us / 1000ULL)
		);

        cJSON_AddStringToObject(json_frame, "timestamp", timestamp_str);
        cJSON_AddNumberToObject(json_frame, "id", (double)frames[i].id);
        cJSON_AddStringToObject(json_frame, "format", frames[i].extended ? "extended" : "standard");
        cJSON_AddStringToObject(json_frame, "type", frames[i].remote ? "remote" : "data");
        cJSON_AddNumberToObject(json_frame, "dlc", frames[i].dlc);
        cJSON_AddBoolToObject(json_frame, "err", frames[i].err);

        json_data = cJSON_AddArrayToObject(json_frame, "data");
        if (json_data == NULL)
        {
            cJSON_Delete(json_frame);
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
            return ESP_OK;
        }

        for (uint8_t j = 0; j < frames[i].dlc && j < 8; j++)
        {
            cJSON_AddItemToArray(json_data, cJSON_CreateNumber(frames[i].data[j]));
        }

        cJSON_AddItemToArray(json_frames, json_frame);
    }

    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);

    return ESP_OK;
}

/**
 * @brief Lê o status e a configuração atuais do módulo TWAI para envio à interface web.
 *
 * @param req Ponteiro para a requisição HTTP recebida.
 *
 * @return ESP_OK em caso de sucesso.
 */
static esp_err_t http_server_can_status_handler(httpd_req_t *req)
{
    twai_app_status_t status = {0};
    cJSON *root = NULL;
    char *json_str = NULL;

    if (twai_app_get_status(&status) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status failed");
        return ESP_OK;
    }

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_OK;
    }

    cJSON_AddStringToObject(root, "can_state", twai_app_state_to_string(status.can_state));
    cJSON_AddStringToObject(root, "mode", twai_app_mode_to_string(status.mode));
    cJSON_AddNumberToObject(root, "baudrate", (double)status.baudrate);

    if (status.filter_enabled)
    {
        char filter_str[9] = {0};
        char mask_str[9] = {0};

		snprintf(filter_str, sizeof(filter_str), "%" PRIX32, status.filter);
		snprintf(mask_str, sizeof(mask_str), "%" PRIX32, status.mask);

        cJSON_AddStringToObject(root, "filter", filter_str);
        cJSON_AddStringToObject(root, "mask", mask_str);
    }
    else
    {
        cJSON_AddNullToObject(root, "filter");
        cJSON_AddNullToObject(root, "mask");
    }

    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);

    return ESP_OK;
}

/**
 * @brief Configura a instância do servidor HTTP, cria recursos auxiliares e registra as rotas.
 *
 * @return Handle do servidor HTTP em caso de sucesso, ou NULL em caso de falha.
 */
static httpd_handle_t http_server_configure(void)
{
	/* Gera a configuração padrão do servidor HTTP. */
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	/* Cria a task de monitoramento do servidor HTTP. */
	xTaskCreatePinnedToCore(&http_server_monitor,
								"http_server_monitor",
								HTTP_SERVER_MONITOR_STACK_SIZE,
								NULL,
								HTTP_SERVER_MONITOR_PRIORITY,
								&task_http_server_monitor,
								HTTP_SERVER_MONITOR_CORE_ID);

	/* Cria a fila de mensagens do monitor. */
	http_server_monitor_queue_handle = xQueueCreate(3, sizeof(http_server_queue_message_t));

	/* Core onde a task do servidor HTTP será executada. */
	config.core_id = HTTP_SERVER_TASK_CORE_ID;

	/* Ajusta a prioridade da task do servidor HTTP. */
	config.task_priority = HTTP_SERVER_TASK_PRIORITY;

	/* Ajusta o tamanho da stack da task do servidor HTTP. */
	config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;

	/* Aumenta a quantidade máxima de URI handlers. */
	config.max_uri_handlers = 20;

	/* Ajusta os timeouts de recepção e envio. */
	config.recv_wait_timeout = 10;
	config.send_wait_timeout = 10;

	ESP_LOGI(TAG,
			"http_server_configure: Starting server on port: '%d' with task priority: '%d'",
			config.server_port,
			config.task_priority);

	/* Inicializa o servidor HTTP. */
	if (httpd_start(&http_server_handle, &config) == ESP_OK)
	{
		ESP_LOGI(TAG, "http_server_configure: Registering URI handlers");

		/* Registra o handler de index.html. */
		httpd_uri_t index_html = {
				.uri = "/",
				.method = HTTP_GET,
				.handler = http_server_index_html_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &index_html);

		/* Registra o handler de styles.css. */
		httpd_uri_t styles_css = {
				.uri = "/styles.css",
				.method = HTTP_GET,
				.handler = http_server_styles_css_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &styles_css);

		/* Registra o handler de script.js. */
		httpd_uri_t script_js = {
				.uri = "/script.js",
				.method = HTTP_GET,
				.handler = http_server_script_js_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &script_js);

		/* Registra o handler de favicon.ico. */
		httpd_uri_t favicon_ico = {
				.uri = "/favicon.ico",
				.method = HTTP_GET,
				.handler = http_server_favicon_ico_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &favicon_ico);

		/* Registra o handler de envio de frames CAN. */
		httpd_uri_t api_can_send = {
			.uri = "/api/can/send",
			.method = HTTP_POST,
			.handler = http_server_can_send_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &api_can_send);

		/* Registra o handler de configuração do módulo CAN. */
		httpd_uri_t api_can_config = {
			.uri = "/api/can/config",
			.method = HTTP_POST,
			.handler = http_server_can_config_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &api_can_config);

		/* Registra o handler de leitura de frames recebidos. */
		httpd_uri_t api_can_rx = {
			.uri = "/api/can/rx",
			.method = HTTP_GET,
			.handler = http_server_can_rx_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &api_can_rx);

		/* Registra o handler de leitura de status do módulo CAN. */
		httpd_uri_t api_can_status = {
			.uri = "/api/can/status",
			.method = HTTP_GET,
			.handler = http_server_can_status_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &api_can_status);

		return http_server_handle;
	}

	return NULL;
}

/**
 * @brief Inicializa o servidor HTTP caso ele ainda não esteja em execução.
 */
void http_server_start(void)
{
	if (http_server_handle == NULL)
	{
		http_server_handle = http_server_configure();
	}
}

/**
 * @brief Interrompe o servidor HTTP e encerra a task de monitoramento.
 */
void http_server_stop(void)
{
	if (http_server_handle)
	{
		httpd_stop(http_server_handle);
		ESP_LOGI(TAG, "http_server_stop: stopping HTTP server");
		http_server_handle = NULL;
	}
	if (task_http_server_monitor)
	{
		vTaskDelete(task_http_server_monitor);
		ESP_LOGI(TAG, "http_server_stop: stopping HTTP server monitor");
		task_http_server_monitor = NULL;
	}
}

/**
 * @brief Envia uma mensagem para a fila de monitoramento do servidor HTTP.
 *
 * @param msgID Identificador da mensagem a ser enviada.
 *
 * @return pdTRUE se a mensagem foi enviada com sucesso.
 * @return pdFALSE em caso de falha no envio.
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
	http_server_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}
