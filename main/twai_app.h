#ifndef MAIN_TWAI_APP_H_
#define MAIN_TWAI_APP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

/**
 * @brief GPIO utilizado para o sinal TX do controlador TWAI.
 */
#define TWAI_APP_TX_GPIO                 GPIO_NUM_21

/**
 * @brief GPIO utilizado para o sinal RX do controlador TWAI.
 */
#define TWAI_APP_RX_GPIO                 GPIO_NUM_22

/**
 * @brief Quantidade máxima de comandos pendentes na fila interna da aplicação TWAI.
 */
#define TWAI_APP_CMD_QUEUE_LEN           16

/**
 * @brief Quantidade máxima de frames armazenados no buffer circular de RX.
 */
#define TWAI_APP_RX_RING_LEN             128

/**
 * @brief Tamanho da stack, em bytes, da task interna da aplicação TWAI.
 */
#define TWAI_APP_TASK_STACK_SIZE         4096

/**
 * @brief Core onde a task da aplicação TWAI será fixada.
 */
#define TWAI_APP_TASK_CORE_ID            1

/**
 * @brief Prioridade da task única dona do driver TWAI.
 */
#define TWAI_APP_TASK_PRIORITY           7

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estados lógicos expostos pela aplicação TWAI para as camadas superiores.
 */
typedef enum
{
    TWAI_APP_STATE_STOPPED = 0,   /**< Driver TWAI parado ou ainda não inicializado. */
    TWAI_APP_STATE_RUNNING,       /**< Driver TWAI em execução normal. */
    TWAI_APP_STATE_BUS_OFF,       /**< Controlador entrou em estado Bus-Off. */
    TWAI_APP_STATE_RECOVERING     /**< Controlador está em processo de recuperação. */
} twai_app_state_t;

/**
 * @brief Modos de operação suportados pela aplicação TWAI.
 */
typedef enum
{
    TWAI_APP_MODE_NORMAL = 0,     /**< Modo normal de operação. */
    TWAI_APP_MODE_LISTEN_ONLY,    /**< Modo listen-only, apenas monitora o barramento. */
    TWAI_APP_MODE_NO_ACK          /**< Modo no-ack, transmite sem exigir ACK. */
} twai_app_mode_t;

/**
 * @brief Estrutura com os dados necessários para solicitar a transmissão de um frame CAN.
 */
typedef struct
{
    uint32_t id;          /**< Identificador CAN em valor numérico. */
    bool extended;        /**< Indica se o frame utiliza identificador estendido de 29 bits. */
    bool remote;          /**< Indica se o frame é do tipo RTR. */
    uint8_t dlc;          /**< DLC do frame, de 0 a 8. */
    uint8_t data[8];      /**< Bytes de dados do frame. */
} twai_app_tx_request_t;

/**
 * @brief Estrutura que representa a configuração operacional do controlador TWAI.
 */
typedef struct
{
    uint32_t baudrate;            /**< Baudrate nominal do barramento CAN, em bits por segundo. */
    twai_app_mode_t mode;         /**< Modo de operação do controlador. */
    bool filter_enabled;          /**< Indica se o filtro em software está habilitado. */
    uint32_t filter;              /**< Valor do filtro CAN usado na comparação em software. */
    uint32_t mask;                /**< Máscara usada na comparação do filtro em software. */
} twai_app_config_t;

/**
 * @brief Estrutura que representa um frame recebido do barramento CAN.
 */
typedef struct
{
    uint64_t timestamp_us; /**< Timestamp do recebimento em microssegundos. */
    uint32_t id;           /**< Identificador CAN em valor numérico. */
    bool extended;         /**< Indica se o frame utiliza identificador estendido de 29 bits. */
    bool remote;           /**< Indica se o frame recebido é RTR. */
    uint8_t dlc;           /**< DLC do frame recebido. */
    uint8_t data[8];       /**< Bytes de dados do frame recebido. */
    bool err;              /**< Indica se o frame está associado a condição de erro observada pela aplicação. */
} twai_app_rx_frame_t;

/**
 * @brief Snapshot do estado atual da aplicação TWAI.
 */
typedef struct
{
    twai_app_state_t can_state;   /**< Estado atual da aplicação. */
    twai_app_mode_t mode;         /**< Modo de operação atual do controlador. */
    uint32_t baudrate;            /**< Baudrate atualmente configurado. */
    bool filter_enabled;          /**< Indica se o filtro em software está habilitado. */
    uint32_t filter;              /**< Valor atual do filtro em software. */
    uint32_t mask;                /**< Valor atual da máscara em software. */

    uint32_t tx_count;            /**< Quantidade acumulada de frames transmitidos. */
    uint32_t rx_count;            /**< Quantidade acumulada de frames recebidos e aceitos. */
    uint32_t err_count;           /**< Quantidade acumulada de ocorrências de erro observadas pela aplicação. */

    uint32_t tx_error_counter;    /**< Valor atual do contador de erro de transmissão do controlador. */
    uint32_t rx_error_counter;    /**< Valor atual do contador de erro de recepção do controlador. */
    uint32_t rx_missed_count;     /**< Quantidade de frames perdidos na fila RX interna do driver. */
    uint32_t rx_overrun_count;    /**< Quantidade de overrun de RX reportados pelo driver. */
    uint32_t bus_error_count;     /**< Quantidade de erros de barramento reportados pelo driver. */
} twai_app_status_t;

/**
 * @brief Inicializa a aplicação TWAI, cria recursos internos e aplica a configuração inicial.
 *
 * @param initial_cfg Ponteiro para a configuração inicial desejada.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_STATE se a aplicação já estiver inicializada.
 * @return ESP_ERR_NO_MEM se houver falha de alocação de recursos.
 * @return Outro código de erro em caso de falha na inicialização do driver.
 */
esp_err_t twai_app_init(const twai_app_config_t *initial_cfg);

/**
 * @brief Enfileira uma solicitação de transmissão de frame CAN.
 *
 * @param req Ponteiro para a estrutura com os dados do frame a ser transmitido.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se os parâmetros forem inválidos.
 * @return ESP_ERR_TIMEOUT se a fila de comandos estiver cheia.
 */
esp_err_t twai_app_send(const twai_app_tx_request_t *req);

/**
 * @brief Solicita a aplicação de uma nova configuração TWAI.
 *
 * @param cfg Ponteiro para a nova configuração a ser aplicada.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se os parâmetros forem inválidos.
 * @return ESP_ERR_TIMEOUT se a fila de comandos estiver cheia.
 */
esp_err_t twai_app_apply_config(const twai_app_config_t *cfg);

/**
 * @brief Obtém um snapshot do estado atual da aplicação TWAI.
 *
 * @param out_status Ponteiro para a estrutura que receberá o status atual.
 *
 * @return ESP_OK em caso de sucesso.
 * @return ESP_ERR_INVALID_ARG se o ponteiro for inválido.
 */
esp_err_t twai_app_get_status(twai_app_status_t *out_status);

/**
 * @brief Remove do buffer circular interno até max_frames frames recebidos.
 *
 * @param out_frames Ponteiro para o array de saída que receberá os frames.
 * @param max_frames Quantidade máxima de frames a copiar.
 *
 * @return Quantidade de frames efetivamente copiados.
 */
size_t twai_app_pop_rx_frames(twai_app_rx_frame_t *out_frames, size_t max_frames);

/**
 * @brief Converte um estado TWAI para sua representação textual.
 *
 * @param state Estado TWAI a ser convertido.
 *
 * @return Ponteiro para string constante correspondente ao estado informado.
 */
const char *twai_app_state_to_string(twai_app_state_t state);

/**
 * @brief Converte um modo de operação TWAI para sua representação textual.
 *
 * @param mode Modo TWAI a ser convertido.
 *
 * @return Ponteiro para string constante correspondente ao modo informado.
 */
const char *twai_app_mode_to_string(twai_app_mode_t mode);

/**
 * @brief Converte uma string para o enum correspondente de modo TWAI.
 *
 * @param text Ponteiro para a string de entrada.
 * @param out_mode Ponteiro para a variável que receberá o modo convertido.
 *
 * @return true se a conversão foi bem-sucedida.
 * @return false se a string for inválida ou se os parâmetros forem inválidos.
 */
bool twai_app_parse_mode(const char *text, twai_app_mode_t *out_mode);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_TWAI_APP_H_ */
