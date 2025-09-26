#include "rs485.h"
#include "stm32f1xx_hal.h"

// Global UART handle
UART_HandleTypeDef huart3;

/**
 * @brief Initialize RS485 module
 * @return HAL status
 */
HAL_StatusTypeDef RS485_Init(void)
{
  // Initialize UART
  RS485_UART_Init();

  return HAL_OK;
}

/**
 * @brief Send string via RS485
 * @param str: Null-terminated string to send
 * @return HAL status
 */
HAL_StatusTypeDef RS485_SendString(const char *str)
{
  HAL_StatusTypeDef status;
  uint16_t length = 0;

  // Calculate string length
  while (str[length] != '\0' && length < 255)
  {
    length++;
  }

  // Send string data
  status = HAL_UART_Transmit(&huart3, (uint8_t *)str, length, RS485_TIMEOUT);

  // Wait for transmission to complete
  while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) == RESET)
  {
    // Wait for transmission complete flag
  }

  return status;
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

  // Configure UART pins (PB10 = TX, PB11 = RX)
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
}
