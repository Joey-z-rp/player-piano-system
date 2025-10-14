#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

// MIDI Parser Configuration
#define MIDI_MAX_EVENTS 1000

// MIDI Event Types (simplified for player piano)
typedef enum
{
  MIDI_EVENT_NOTE_OFF = 0x80,
  MIDI_EVENT_NOTE_ON = 0x90,
  MIDI_EVENT_CONTROL_CHANGE = 0xB0,
  MIDI_EVENT_META = 0xFF
} MidiEventType_t;

// MIDI Meta Event Types (simplified)
typedef enum
{
  MIDI_META_END_OF_TRACK = 0x2F,
  MIDI_META_SET_TEMPO = 0x51
} MidiMetaEventType_t;

// MIDI Control Change Numbers
#define MIDI_CC_SUSTAIN_PEDAL 64

// MIDI Event Structure (simplified for player piano)
typedef struct
{
  uint32_t delta_time;   // Time in ticks from previous event
  uint8_t event_type;    // MIDI event type
  uint8_t note_number;   // Piano key number (0-87 for 88-key piano)
  uint8_t velocity;      // Note velocity (0-127)
  bool is_note_on;       // True for note on, false for note off
  bool is_sustain_event; // True if this is a sustain pedal event
  bool is_sustain_on;    // True if sustain pedal is on, false if off (only valid if is_sustain_event is true)
} MidiEvent_t;

// MIDI Parser Module Structure (simplified)
typedef struct
{
  MidiEvent_t events[MIDI_MAX_EVENTS]; // Array of parsed events
  uint32_t event_count;                // Number of parsed events
  uint32_t current_event;              // Current event index for iteration
  uint16_t time_division;              // Ticks per quarter note
  uint32_t tempo;                      // Microseconds per quarter note
  bool is_loaded;                      // Whether data is successfully loaded
} MidiParser_t;

// Function prototypes
HAL_StatusTypeDef MidiParser_Init(MidiParser_t *parser);
HAL_StatusTypeDef MidiParser_LoadEmbeddedData(MidiParser_t *parser);
MidiEvent_t *MidiParser_GetEvent(MidiParser_t *parser, uint32_t index);
uint32_t MidiParser_GetEventCount(MidiParser_t *parser);
void MidiParser_Cleanup(MidiParser_t *parser);

// Event iteration methods for main.c
void MidiParser_ResetToBeginning(MidiParser_t *parser);
MidiEvent_t *MidiParser_GetNextEvent(MidiParser_t *parser);
bool MidiParser_HasMoreEvents(MidiParser_t *parser);
uint32_t MidiParser_GetCurrentEventIndex(MidiParser_t *parser);

// Timing methods for main.c
uint32_t MidiParser_GetTempo(MidiParser_t *parser);
uint16_t MidiParser_GetTimeDivision(MidiParser_t *parser);
uint32_t MidiParser_TicksToMilliseconds(MidiParser_t *parser, uint32_t ticks);

// Utility functions
uint32_t MidiParser_ReadVariableLength(uint8_t *data, uint32_t *offset);
uint32_t MidiParser_Read32Bit(uint8_t *data, uint32_t *offset);
uint16_t MidiParser_Read16Bit(uint8_t *data, uint32_t *offset);
uint8_t MidiParser_Read8Bit(uint8_t *data, uint32_t *offset);

// Global instance access
MidiParser_t *MidiParser_GetInstance(void);

#endif // MIDI_PARSER_H