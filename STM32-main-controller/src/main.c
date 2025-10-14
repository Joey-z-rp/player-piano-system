#include "stm32f1xx_hal.h"
#include "rs485.h"
#include "button_module.h"
#include "midi_parser.h"
#include <stdio.h>

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

  // Initialize button module
  ButtonModule_Init(ButtonModule_GetInstance());

  // Initialize MIDI parser module
  if (MidiParser_Init(MidiParser_GetInstance()) != HAL_OK)
  {
    // Error handling
    while (1)
      ;
  }

  // Load embedded MIDI data
  if (MidiParser_LoadEmbeddedData(MidiParser_GetInstance()) == HAL_OK)
  {
    // MIDI data loaded successfully
    // Reset to beginning for event processing
    MidiParser_ResetToBeginning(MidiParser_GetInstance());
  }

  while (1)
  {
    // Update button module to check for button presses
    ButtonModule_Update(ButtonModule_GetInstance());

    // Process MIDI events with proper timing
    static MidiParser_t *parser = NULL;
    static uint32_t last_event_time = 0;
    static bool playback_started = false;

    // Initialize parser pointer on first run
    if (parser == NULL)
    {
      parser = MidiParser_GetInstance();
    }

    if (parser->is_loaded && MidiParser_HasMoreEvents(parser))
    {
      // Get the next event without advancing the parser
      uint32_t current_index = MidiParser_GetCurrentEventIndex(parser);
      MidiEvent_t *event = MidiParser_GetEvent(parser, current_index);

      if (event != NULL)
      {
        // Convert delta time to milliseconds
        uint32_t event_delay_ms = MidiParser_TicksToMilliseconds(parser, event->delta_time);

        // Check if it's time to play this event
        uint32_t current_time = HAL_GetTick();

        if (!playback_started)
        {
          // Start playback
          last_event_time = current_time;
          playback_started = true;
        }

        // Check if enough time has passed since the last event
        if (current_time - last_event_time >= event_delay_ms)
        {
          // Process the event for player piano control
          if (event->is_sustain_event)
          {
            // Handle sustain pedal event
            if (event->is_sustain_on)
            {
              // Send sustain on command: "P:P"
              RS485_SendString("P:P\n");
            }
            else
            {
              // Send sustain off command: "R:P"
              RS485_SendString("R:P\n");
            }
          }
          else if (event->is_note_on)
          {
            // Convert MIDI note number to channel (0-11 for A to G#)
            uint8_t channel = (event->note_number - 21) % 12;
            if (channel > 11)
              channel = 0; // Safety check

            // Convert velocity (0-127) to duty cycle (65-80)
            uint8_t duty_cycle = 65 + ((event->velocity * 15) / 127);

            // Send note on command: "P:channel:duty_cycle\n"
            char command[16];
            sprintf(command, "P:%d:%d\n", channel, duty_cycle);
            RS485_SendString(command);
          }
          else
          {
            // Convert MIDI note number to channel (0-11 for A to G#)
            uint8_t channel = (event->note_number - 21) % 12;
            if (channel > 11)
              channel = 0; // Safety check

            // Send note off command: "R:channel:0\n"
            char command[16];
            sprintf(command, "R:%d:0\n", channel);
            RS485_SendString(command);
          }

          // Advance to next event and update timing
          MidiParser_GetNextEvent(parser);
          last_event_time = current_time;
        }
      }
    }

    // Small delay to prevent excessive CPU usage
    HAL_Delay(1);
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
