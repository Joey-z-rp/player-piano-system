#include "stm32f1xx_hal.h"
#include "rs485.h"
#include <stdio.h>

// Twinkle Twinkle Little Star melody
// Format: {note, octave, duration_ms}
typedef struct
{
  char note[4]; // Note name (e.g., "C4", "C#4", "D4")
  uint16_t duration_ms;
} MelodyNote_t;

int8_t speed = 1;
// Twinkle Twinkle Little Star melody
static const MelodyNote_t twinkle_melody[] = {
    {"C4", 500}, {"C4", 500}, {"G4", 500}, {"G4", 500}, {"A4", 500}, {"A4", 500}, {"G4", 1000}, {"F4", 500}, {"F4", 500}, {"E4", 500}, {"E4", 500}, {"D4", 500}, {"D4", 500}, {"C4", 1000}, {"G4", 500}, {"G4", 500}, {"F4", 500}, {"F4", 500}, {"E4", 500}, {"E4", 500}, {"D4", 1000}, {"G4", 500}, {"G4", 500}, {"F4", 500}, {"F4", 500}, {"E4", 500}, {"E4", 500}, {"D4", 1000}, {"C4", 500}, {"C4", 500}, {"G4", 500}, {"G4", 500}, {"A4", 500}, {"A4", 500}, {"G4", 1000}, {"F4", 500}, {"F4", 500}, {"E4", 500}, {"E4", 500}, {"D4", 500}, {"D4", 500}, {"C4", 1000}};

#define MELODY_LENGTH (sizeof(twinkle_melody) / sizeof(twinkle_melody[0]))

// Function to play a single note
void play_note(const char *note, uint8_t velocity, uint16_t duration_ms)
{
  // Send press command
  char press_cmd[16];
  snprintf(press_cmd, sizeof(press_cmd), "P:%s:%d\n", note, velocity);
  RS485_SendString(press_cmd);

  // Wait for note duration
  HAL_Delay(duration_ms);

  // Send release command
  char release_cmd[16];
  snprintf(release_cmd, sizeof(release_cmd), "R:%s:0\n", note);
  RS485_SendString(release_cmd);
}

// Function to play the entire melody
void play_twinkle_twinkle(void)
{
  for (int i = 0; i < MELODY_LENGTH; i++)
  {
    play_note(twinkle_melody[i].note, 100, twinkle_melody[i].duration_ms * speed);
    // Small pause between notes
    HAL_Delay(50);
  }
}

int main(void)
{
  HAL_Init();

  // Initialize RS485 module
  if (RS485_Init() != HAL_OK)
  {
    // Error handling
    while (1)
      ;
  }

  while (1)
  {
    int16_t interval = 100;
    RS485_SendString("P:C4:127\n");
    HAL_Delay(interval);
    RS485_SendString("R:C4:0\n");
    RS485_SendString("P:C#4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:C#4:0\n");
    RS485_SendString("P:D4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:D4:0\n");
    RS485_SendString("P:D#4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:D#4:0\n");
    RS485_SendString("P:E4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:E4:0\n");
    RS485_SendString("P:F4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:F4:0\n");
    RS485_SendString("P:F#4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:F#4:0\n");
    RS485_SendString("P:G4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:G4:0\n");
    RS485_SendString("P:G#4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:G#4:0\n");
    RS485_SendString("P:A4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:A4:0\n");
    RS485_SendString("P:A#4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:A#4:0\n");
    RS485_SendString("P:B4:110\n");
    HAL_Delay(interval);
    RS485_SendString("R:B4:0\n");
    // HAL_Delay(interval);

    // play_twinkle_twinkle();

    // Wait 2 seconds before playing again
    HAL_Delay(interval);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
