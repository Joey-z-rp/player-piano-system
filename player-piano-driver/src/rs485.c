#include "rs485.h"
#include "stm32f1xx_hal.h"
#include <string.h>

// Global UART handle
UART_HandleTypeDef huart3;

// The receive interrupt stores each byte in this ring (it only moves head);
// RS485_Poll, in the main loop, reads it (it only moves tail) and assembles
// lines. LINE_LOST in the ring means bytes were lost: the line it's in is
// thrown away rather than read short (a cut-off "P:89:100" would still parse).
#define LINE_LOST 0x00
static volatile uint8_t ring[RS485_RING_SIZE];
static volatile uint16_t ring_head = 0;
static volatile uint16_t ring_tail = 0;
static volatile uint8_t ring_overflowed = 0;
static uint8_t rx_byte;

static char line[RS485_RX_BUFFER_SIZE];
static uint16_t line_length = 0;
static uint8_t line_lost = 0; // discard up to the next end of line
static RS485_MessageCallback_t message_callback = NULL;

// Interrupt side: store one byte, or remember that one was lost.
static void RingPut(uint8_t byte)
{
  uint16_t next = (uint16_t)((ring_head + 1) % RS485_RING_SIZE);
  if (next == ring_tail)
  {
    ring_overflowed = 1;
    return;
  }
  ring[ring_head] = byte;
  __DMB(); // the byte is written before the main loop can see it
  ring_head = next;
}

/**
 * @brief Initialize RS485 module
 * @return HAL status
 */
HAL_StatusTypeDef RS485_Init(void)
{
  // Initialize UART
  RS485_UART_Init();

  // Start receiving
  RS485_StartReceive();

  return HAL_OK;
}

/**
 * @brief Start receiving data (non-blocking)
 * @return HAL status
 */
HAL_StatusTypeDef RS485_StartReceive(void)
{
  return HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
}

/**
 * @brief Initialize UART for RS485 communication
 */
void RS485_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // Enable UART and GPIO clocks
  __HAL_RCC_USART3_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Configure UART
  huart3.Instance = RS485_UART_INSTANCE;
  huart3.Init.BaudRate = RS485_BAUDRATE;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;

  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    // Error handling - could be improved with proper error reporting
  }

  // Enable UART interrupt in NVIC
  HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/**
 * @brief Set callback function for received messages
 * @param callback: Function to call when a message is received
 */
void RS485_SetMessageCallback(RS485_MessageCallback_t callback)
{
  message_callback = callback;
}

/**
 * @brief UART3 interrupt handler
 */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}

/**
 * @brief UART error callback. An overrun (a byte arriving before the last
 * was read) makes the HAL stop receiving: restart it, and mark the line lost.
 * Errors that leave reception running (noise, framing) need nothing: the
 * garbled line fails to parse.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == RS485_UART_INSTANCE && huart->RxState == HAL_UART_STATE_READY)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    RingPut(LINE_LOST);
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
  }
}

/**
 * @brief UART receive complete callback: store the byte, receive the next.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == RS485_UART_INSTANCE)
  {
    RingPut(rx_byte);
    HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
  }
}

void RS485_Poll(void)
{
  while (ring_tail != ring_head)
  {
    __DMB(); // read the byte only after seeing it was stored
    uint8_t byte = ring[ring_tail];
    ring_tail = (uint16_t)((ring_tail + 1) % RS485_RING_SIZE);

    if (byte == LINE_LOST)
    {
      line_lost = 1;
    }
    else if (byte == '\n' || byte == '\r')
    {
      if (!line_lost && line_length > 0 && message_callback != NULL)
      {
        line[line_length] = '\0';
        message_callback(line, line_length);
      }
      line_length = 0;
      line_lost = 0;
    }
    else if (line_length < RS485_RX_BUFFER_SIZE - 1)
    {
      line[line_length++] = (char)byte;
    }
    else
    {
      line_lost = 1; // too long to be a command
    }
  }
  // Bytes lost to a full ring came after everything read above: the line
  // being assembled is missing some (and so may the next, if a line end was
  // among them), so drop it.
  if (ring_overflowed)
  {
    ring_overflowed = 0;
    line_lost = 1;
  }
}
