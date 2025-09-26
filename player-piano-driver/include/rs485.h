#ifndef RS485_H
#define RS485_H

#include "stm32f1xx_hal.h"

// RS485 Configuration
#define RS485_UART_INSTANCE USART3
#define RS485_BAUDRATE 9600
#define RS485_TIMEOUT 1000
#define RS485_RX_BUFFER_SIZE 256

// Function prototypes
HAL_StatusTypeDef RS485_Init(void);
HAL_StatusTypeDef RS485_ReceiveString(char *buffer, uint16_t buffer_size, uint32_t timeout);
HAL_StatusTypeDef RS485_StartReceive(void);
void RS485_UART_Init(void);

// Callback function type for received messages
typedef void (*RS485_MessageCallback_t)(const char *message, uint16_t length);

// Set callback function for received messages
void RS485_SetMessageCallback(RS485_MessageCallback_t callback);

#endif // RS485_H
