#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "freertos/FreeRTOS.h"

/** @brief Tamanho da stack da task principal do servidor HTTP. */
#define HTTP_SERVER_TASK_STACK_SIZE     8192
/** @brief Prioridade da task principal do servidor HTTP. */
#define HTTP_SERVER_TASK_PRIORITY       4
/** @brief Core onde a task principal do servidor HTTP será executada. */
#define HTTP_SERVER_TASK_CORE_ID        0

/** @brief Tamanho da stack da task de monitoramento do servidor HTTP. */
#define HTTP_SERVER_MONITOR_STACK_SIZE  4096
/** @brief Prioridade da task de monitoramento do servidor HTTP. */
#define HTTP_SERVER_MONITOR_PRIORITY    3
/** @brief Core onde a task de monitoramento do servidor HTTP será executada. */
#define HTTP_SERVER_MONITOR_CORE_ID     0

/**
 * @brief Mensagens tratadas pelo monitor do servidor HTTP.
 */
typedef enum http_server_message
{
    HTTP_MSG_WIFI_CONNECT_INIT = 0,   /**< Início do processo de conexão Wi-Fi. */
    HTTP_MSG_WIFI_CONNECT_SUCCESS,    /**< Conexão Wi-Fi concluída com sucesso. */
    HTTP_MSG_WIFI_CONNECT_FAIL,       /**< Falha no processo de conexão Wi-Fi. */
} http_server_message_e;

/**
 * @brief Estrutura da mensagem trafegada na fila do monitor HTTP.
 */
typedef struct http_server_queue_message
{
    http_server_message_e msgID;      /**< Identificador da mensagem enviada ao monitor. */
} http_server_queue_message_t;

/**
 * @brief Envia uma mensagem para a fila de monitoramento do servidor HTTP.
 *
 * @param msgID Identificador da mensagem do tipo http_server_message_e.
 *
 * @return pdTRUE se o item foi enviado com sucesso.
 * @return pdFALSE em caso de falha no envio.
 *
 * @note Expanda a lista de parâmetros conforme a necessidade da aplicação.
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID);

/**
 * @brief Inicializa o servidor HTTP.
 */
void http_server_start(void);

/**
 * @brief Interrompe o servidor HTTP.
 */
void http_server_stop(void);

#endif /* MAIN_HTTP_SERVER_H_*/
