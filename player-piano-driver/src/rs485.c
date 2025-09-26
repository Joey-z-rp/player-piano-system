#include "rs485.h"
#include "stm32f1xx_hal.h"
#include <string.h>

// Global UART handle
UART_HandleTypeDef huart3;

// Receive buffer and state
static char rx_buffer[RS485_RX_BUFFER_SIZE];
static uint16_t rx_index = 0;
static RS485_MessageCallback_t message_callback = NULL;

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
 * @brief Receive string via RS485 (blocking)
 * @param buffer: Buffer to store received string
 * @param buffer_size: Size of the buffer
 * @param timeout: Timeout in milliseconds
 * @return HAL status
 */
HAL_StatusTypeDef RS485_ReceiveString(char *buffer, uint16_t buffer_size, uint32_t timeout)
{
  uint32_t start_time = HAL_GetTick();

  // Clear buffer
  memset(buffer, 0, buffer_size);

  // Wait for data with timeout
  while (HAL_GetTick() - start_time < timeout)
  {
    if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE))
    {
      uint8_t byte;
      if (HAL_UART_Receive(&huart3, &byte, 1, 10) == HAL_OK)
      {
        if (rx_index < buffer_size - 1)
        {
          buffer[rx_index] = byte;
          rx_index++;

          // Check for end of message (newline or carriage return)
          if (byte == '\n' || byte == '\r')
          {
            buffer[rx_index] = '\0';
            rx_index = 0;
            return HAL_OK;
          }
        }
      }
    }
  }

  return HAL_TIMEOUT;
}

/**
 * @brief Start receiving data (non-blocking)
 * @return HAL status
 */
HAL_StatusTypeDef RS485_StartReceive(void)
{
  return HAL_UART_Receive_IT(&huart3, (uint8_t *)rx_buffer, 1);
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

  // Configure UART pin (PB11 = RX)

  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // Configure UART
  huart3.Instance = RS485_UART_INSTANCE;
  huart3.Init.BaudRate = RS485_BAUDRATE;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
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
 * @brief UART receive complete callback
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == RS485_UART_INSTANCE)
  {
    // Toggle LED to indicate UART interrupt received
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

    // Check for end of message
    if (rx_buffer[rx_index] == '\n' || rx_buffer[rx_index] == '\r')
    {
      rx_buffer[rx_index] = '\0';

      // Call callback if set
      if (message_callback != NULL)
      {
        message_callback(rx_buffer, rx_index);
      }

      // Reset for next message
      rx_index = 0;
    }
    else
    {
      rx_index++;
      if (rx_index >= RS485_RX_BUFFER_SIZE - 1)
      {
        // Buffer overflow, reset
        rx_index = 0;
      }
    }

    // Restart reception
    HAL_UART_Receive_IT(&huart3, (uint8_t *)&rx_buffer[rx_index], 1);
  }
}
