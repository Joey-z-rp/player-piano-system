#ifndef RS485_H
#define RS485_H

#include "stm32f1xx_hal.h"

// RS485 Configuration
#define RS485_UART_INSTANCE USART3
#define RS485_BAUDRATE 9600
#define RS485_TIMEOUT 1000

// Function prototypes
HAL_StatusTypeDef RS485_Init(void);
HAL_StatusTypeDef RS485_SendString(const char *str);
void RS485_UART_Init(void);

#endif // RS485_H
