#ifndef RS485_H
#define RS485_H

#include "stm32f1xx_hal.h"

// RS485 Configuration
#define RS485_UART_INSTANCE USART3
#define RS485_BAUDRATE 115200
#define RS485_TIMEOUT 1000
#define RS485_RX_BUFFER_SIZE 256 // longest line
// Bytes the receive interrupt can hold before the main loop reads them: a burst
// such as releasing all keys (~900 bytes) fits even if the loop falls behind.
#define RS485_RING_SIZE 1024

// Function prototypes
HAL_StatusTypeDef RS485_Init(void);
HAL_StatusTypeDef RS485_StartReceive(void);
void RS485_UART_Init(void);

// Callback function type for received messages
typedef void (*RS485_MessageCallback_t)(const char *message, uint16_t length);

// Set callback function for received messages
void RS485_SetMessageCallback(RS485_MessageCallback_t callback);
// Hands each complete line received to the callback. Call from the main loop:
// the receive interrupt only stores bytes, so a command arriving straight
// after another can't be lost while the first is being parsed.
void RS485_Poll(void);

#endif // RS485_H
