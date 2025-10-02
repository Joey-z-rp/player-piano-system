#include "midi_parser.h"
#include <string.h>
#include <stdlib.h>

// Global MIDI parser instance
static MidiParser_t g_midi_parser;

// Embedded MIDI file data for twinkle.mid
// This is a simple "Twinkle Twinkle Little Star" MIDI file
static const uint8_t twinkle_midi_data[] = {
    0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x02, 0x01, 0xE0, 0x4D, 0x54,
    0x72, 0x6B, 0x00, 0x00, 0x00, 0x17, 0x00, 0xFF, 0x03, 0x00, 0x00, 0xFF, 0x58, 0x04, 0x04, 0x02,
    0x18, 0x08, 0x00, 0xFF, 0x51, 0x03, 0x05, 0x1E, 0x29, 0x00, 0xFF, 0x2F, 0x00, 0x4D, 0x54, 0x72,
    0x6B, 0x00, 0x00, 0x00, 0xCA, 0x00, 0xB0, 0x79, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x00, 0xB0, 0x0A,
    0x40, 0x00, 0xB0, 0x07, 0x64, 0x00, 0xB0, 0x0B, 0x7F, 0x00, 0xB0, 0x65, 0x00, 0x00, 0xB0, 0x64,
    0x02, 0x00, 0xB0, 0x06, 0x40, 0x00, 0xB0, 0x65, 0x00, 0x00, 0xB0, 0x64, 0x01, 0x00, 0xB0, 0x06,
    0x40, 0x00, 0xB0, 0x26, 0x00, 0x00, 0xB0, 0x65, 0x00, 0x00, 0xB0, 0x64, 0x00, 0x00, 0xB0, 0x06,
    0x0C, 0x00, 0xE0, 0x00, 0x40, 0x00, 0xB0, 0x01, 0x00, 0x00, 0xC0, 0x00, 0x8F, 0x00, 0x90, 0x39,
    0x64, 0x83, 0x10, 0x80, 0x39, 0x00, 0x50, 0x90, 0x39, 0x64, 0x83, 0x1A, 0x80, 0x39, 0x00, 0x46,
    0x90, 0x40, 0x64, 0x83, 0x1A, 0x80, 0x40, 0x00, 0x46, 0x90, 0x40, 0x64, 0x83, 0x1A, 0x80, 0x40,
    0x00, 0x46, 0x90, 0x42, 0x64, 0x83, 0x10, 0x80, 0x42, 0x00, 0x50, 0x90, 0x42, 0x64, 0x83, 0x1A,
    0x80, 0x42, 0x00, 0x46, 0x90, 0x40, 0x64, 0x86, 0x7A, 0x80, 0x40, 0x00, 0x46, 0x90, 0x3E, 0x64,
    0x83, 0x1A, 0x80, 0x3E, 0x00, 0x46, 0x90, 0x3E, 0x64, 0x83, 0x1A, 0x80, 0x3E, 0x00, 0x46, 0x90,
    0x3D, 0x64, 0x83, 0x1A, 0x80, 0x3D, 0x00, 0x46, 0x90, 0x3D, 0x64, 0x83, 0x1A, 0x80, 0x3D, 0x00,
    0x46, 0x90, 0x3B, 0x64, 0x83, 0x24, 0x80, 0x3B, 0x00, 0x3C, 0x90, 0x3B, 0x64, 0x83, 0x1A, 0x80,
    0x3B, 0x00, 0x46, 0x90, 0x39, 0x64, 0x87, 0x04, 0x80, 0x39, 0x00, 0x00, 0xFF, 0x2F, 0x00};

// Private function prototypes
static HAL_StatusTypeDef MidiParser_ParseTrack(MidiParser_t *parser, uint8_t *track_data, uint32_t track_length);
static HAL_StatusTypeDef MidiParser_ParseEvent(MidiParser_t *parser, uint8_t *data, uint32_t *offset, uint32_t *event_index, uint8_t *running_status);

/**
 * @brief Initialize the MIDI parser module
 * @param parser Pointer to MIDI parser structure
 * @return HAL status
 */
HAL_StatusTypeDef MidiParser_Init(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return HAL_ERROR;
  }

  // Initialize parser structure
  memset(parser, 0, sizeof(MidiParser_t));
  parser->is_loaded = false;
  parser->current_event = 0;
  parser->tempo = 500000;      // Default tempo (120 BPM)
  parser->time_division = 480; // Default time division

  return HAL_OK;
}

/**
 * @brief Load embedded MIDI data
 * @param parser Pointer to MIDI parser structure
 * @return HAL status
 */
HAL_StatusTypeDef MidiParser_LoadEmbeddedData(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return HAL_ERROR;
  }

  // Clean up previous data if loaded
  MidiParser_Cleanup(parser);

  // Use embedded MIDI data directly
  const uint32_t file_size = sizeof(twinkle_midi_data);
  uint8_t *file_data = malloc(file_size);
  if (file_data == NULL)
  {
    return HAL_ERROR;
  }

  memcpy(file_data, twinkle_midi_data, file_size);

  // Parse MIDI header
  uint32_t offset = 0;

  // Check MIDI header signature
  if (file_size < 14 ||
      file_data[0] != 'M' || file_data[1] != 'T' ||
      file_data[2] != 'h' || file_data[3] != 'd')
  {
    free(file_data);
    return HAL_ERROR;
  }

  // Skip the "MThd" header (4 bytes)
  offset = 4;

  // Read header length (should be 6)
  uint32_t header_length = MidiParser_Read32Bit(file_data, &offset);
  if (header_length != 6)
  {
    free(file_data);
    return HAL_ERROR;
  }

  // Read format, number of tracks, and time division
  uint16_t format = MidiParser_Read16Bit(file_data, &offset);
  uint16_t num_tracks = MidiParser_Read16Bit(file_data, &offset);
  parser->time_division = MidiParser_Read16Bit(file_data, &offset);

  // Validate format and track count
  if (format > 2 || num_tracks == 0)
  {
    free(file_data);
    return HAL_ERROR;
  }

  // Parse tracks (find the track with note events)
  for (uint16_t i = 0; i < num_tracks; i++) // Parse all tracks to find note events
  {
    if (offset >= file_size)
    {
      break;
    }

    // Check track header
    if (file_data[offset] != 'M' || file_data[offset + 1] != 'T' ||
        file_data[offset + 2] != 'r' || file_data[offset + 3] != 'k')
    {
      break;
    }
    offset += 4;

    // Read track length
    uint32_t track_length = MidiParser_Read32Bit(file_data, &offset);

    // Parse track data
    HAL_StatusTypeDef status = MidiParser_ParseTrack(parser, &file_data[offset], track_length);
    if (status != HAL_OK)
    {
      free(file_data);
      return status;
    }

    offset += track_length;
  }

  free(file_data);
  parser->is_loaded = true;

  return HAL_OK;
}

/**
 * @brief Parse a single MIDI track
 * @param parser Pointer to MIDI parser structure
 * @param track_data Pointer to track data
 * @param track_length Length of track data
 * @return HAL status
 */
static HAL_StatusTypeDef MidiParser_ParseTrack(MidiParser_t *parser, uint8_t *track_data, uint32_t track_length)
{
  if (parser == NULL || track_data == NULL)
  {
    return HAL_ERROR;
  }

  uint32_t offset = 0;
  uint32_t event_index = 0;
  uint8_t running_status = 0;

  // Parse events
  while (offset < track_length && event_index < MIDI_MAX_EVENTS)
  {
    HAL_StatusTypeDef status = MidiParser_ParseEvent(parser, track_data, &offset, &event_index, &running_status);
    if (status != HAL_OK)
    {
      break;
    }
  }

  parser->event_count = event_index;
  return HAL_OK;
}

/**
 * @brief Parse a single MIDI event
 * @param parser Pointer to MIDI parser structure
 * @param data Pointer to track data
 * @param offset Pointer to current offset in data
 * @param event_index Pointer to current event index
 * @param running_status Pointer to running status byte
 * @return HAL status
 */
static HAL_StatusTypeDef MidiParser_ParseEvent(MidiParser_t *parser, uint8_t *data, uint32_t *offset, uint32_t *event_index, uint8_t *running_status)
{
  if (parser == NULL || data == NULL || offset == NULL || event_index == NULL || running_status == NULL)
  {
    return HAL_ERROR;
  }

  MidiEvent_t *event = &parser->events[*event_index];
  memset(event, 0, sizeof(MidiEvent_t));

  // Read delta time
  event->delta_time = MidiParser_ReadVariableLength(data, offset);

  // Read status byte
  uint8_t status_byte = data[(*offset)++];

  if (status_byte & 0x80)
  {
    // Status byte - new event
    *running_status = status_byte;
  }
  else
  {
    // Data byte - use running status
    (*offset)--; // Back up to re-read this byte as data
  }

  // Only process note on/off events for player piano
  if ((*running_status & 0xF0) == MIDI_EVENT_NOTE_ON || (*running_status & 0xF0) == MIDI_EVENT_NOTE_OFF)
  {
    event->event_type = *running_status & 0xF0;
    event->note_number = data[(*offset)++];
    event->velocity = data[(*offset)++];

    // Convert note on with velocity 0 to note off
    if (event->event_type == MIDI_EVENT_NOTE_ON && event->velocity == 0)
    {
      event->event_type = MIDI_EVENT_NOTE_OFF;
    }

    event->is_note_on = (event->event_type == MIDI_EVENT_NOTE_ON);

    // Only add note events (ignore other events)
    (*event_index)++;
  }
  else if (*running_status == MIDI_EVENT_META)
  {
    // Handle meta events
    uint8_t meta_type = data[(*offset)++];
    uint8_t meta_length = MidiParser_ReadVariableLength(data, offset);

    // Process tempo changes
    if (meta_type == MIDI_META_SET_TEMPO && meta_length >= 3)
    {
      parser->tempo = (data[*offset] << 16) | (data[*offset + 1] << 8) | data[*offset + 2];
    }

    // Skip meta event data
    *offset += meta_length;
  }
  else
  {
    // Skip other events
    if ((*running_status & 0xF0) == 0xB0 || // Control Change
        (*running_status & 0xF0) == 0xC0 || // Program Change
        (*running_status & 0xF0) == 0xE0)   // Pitch Bend
    {
      // Skip 1-2 data bytes depending on event type
      if ((*running_status & 0xF0) == 0xC0) // Program Change
      {
        (*offset)++; // Skip 1 data byte
      }
      else
      {
        (*offset) += 2; // Skip 2 data bytes
      }
    }
  }

  return HAL_OK;
}

/**
 * @brief Get a specific event by index
 * @param parser Pointer to MIDI parser structure
 * @param index Index of the event to retrieve
 * @return Pointer to event, or NULL if index is invalid
 */
MidiEvent_t *MidiParser_GetEvent(MidiParser_t *parser, uint32_t index)
{
  if (parser == NULL || !parser->is_loaded || index >= parser->event_count)
  {
    return NULL;
  }

  return &parser->events[index];
}

/**
 * @brief Get the total number of events
 * @param parser Pointer to MIDI parser structure
 * @return Number of events
 */
uint32_t MidiParser_GetEventCount(MidiParser_t *parser)
{
  if (parser == NULL || !parser->is_loaded)
  {
    return 0;
  }

  return parser->event_count;
}

/**
 * @brief Clean up MIDI parser resources
 * @param parser Pointer to MIDI parser structure
 */
void MidiParser_Cleanup(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return;
  }

  // Reset parser structure
  memset(parser, 0, sizeof(MidiParser_t));
  parser->tempo = 500000;      // Reset to default tempo
  parser->time_division = 480; // Reset to default time division
}

/**
 * @brief Read a variable length value from MIDI data
 * @param data Pointer to data
 * @param offset Pointer to current offset
 * @return Variable length value
 */
uint32_t MidiParser_ReadVariableLength(uint8_t *data, uint32_t *offset)
{
  uint32_t value = 0;
  uint8_t byte;

  do
  {
    byte = data[(*offset)++];
    value = (value << 7) | (byte & 0x7F);
  } while (byte & 0x80);

  return value;
}

/**
 * @brief Read a 32-bit value from MIDI data
 * @param data Pointer to data
 * @param offset Pointer to current offset
 * @return 32-bit value
 */
uint32_t MidiParser_Read32Bit(uint8_t *data, uint32_t *offset)
{
  uint32_t value = (data[*offset] << 24) | (data[*offset + 1] << 16) |
                   (data[*offset + 2] << 8) | data[*offset + 3];
  *offset += 4;
  return value;
}

/**
 * @brief Read a 16-bit value from MIDI data
 * @param data Pointer to data
 * @param offset Pointer to current offset
 * @return 16-bit value
 */
uint16_t MidiParser_Read16Bit(uint8_t *data, uint32_t *offset)
{
  uint16_t value = (data[*offset] << 8) | data[*offset + 1];
  *offset += 2;
  return value;
}

/**
 * @brief Read an 8-bit value from MIDI data
 * @param data Pointer to data
 * @param offset Pointer to current offset
 * @return 8-bit value
 */
uint8_t MidiParser_Read8Bit(uint8_t *data, uint32_t *offset)
{
  return data[(*offset)++];
}

/**
 * @brief Reset parser to beginning of events
 * @param parser Pointer to MIDI parser structure
 */
void MidiParser_ResetToBeginning(MidiParser_t *parser)
{
  if (parser != NULL)
  {
    parser->current_event = 0;
  }
}

/**
 * @brief Get the next event in sequence
 * @param parser Pointer to MIDI parser structure
 * @return Pointer to next event, or NULL if no more events
 */
MidiEvent_t *MidiParser_GetNextEvent(MidiParser_t *parser)
{
  if (parser == NULL || !parser->is_loaded || parser->current_event >= parser->event_count)
  {
    return NULL;
  }

  MidiEvent_t *event = &parser->events[parser->current_event];
  parser->current_event++;
  return event;
}

/**
 * @brief Check if there are more events to process
 * @param parser Pointer to MIDI parser structure
 * @return true if there are more events, false otherwise
 */
bool MidiParser_HasMoreEvents(MidiParser_t *parser)
{
  if (parser == NULL || !parser->is_loaded)
  {
    return false;
  }

  return parser->current_event < parser->event_count;
}

/**
 * @brief Get current event index
 * @param parser Pointer to MIDI parser structure
 * @return Current event index
 */
uint32_t MidiParser_GetCurrentEventIndex(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return 0;
  }

  return parser->current_event;
}

/**
 * @brief Get tempo from MIDI parser
 * @param parser Pointer to MIDI parser structure
 * @return Tempo in microseconds per quarter note
 */
uint32_t MidiParser_GetTempo(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return 500000; // Default tempo (120 BPM)
  }
  return parser->tempo;
}

/**
 * @brief Get time division from MIDI parser
 * @param parser Pointer to MIDI parser structure
 * @return Time division (ticks per quarter note)
 */
uint16_t MidiParser_GetTimeDivision(MidiParser_t *parser)
{
  if (parser == NULL)
  {
    return 480; // Default time division
  }
  return parser->time_division;
}

/**
 * @brief Convert MIDI ticks to milliseconds
 * @param parser Pointer to MIDI parser structure
 * @param ticks Number of MIDI ticks
 * @return Milliseconds
 */
uint32_t MidiParser_TicksToMilliseconds(MidiParser_t *parser, uint32_t ticks)
{
  if (parser == NULL || parser->time_division == 0)
  {
    return 0;
  }

  // Formula: milliseconds = (ticks * tempo) / (time_division * 1000)
  // tempo is in microseconds per quarter note
  uint64_t result = ((uint64_t)ticks * parser->tempo) / ((uint64_t)parser->time_division * 1000);
  return (uint32_t)result;
}

/**
 * @brief Get global MIDI parser instance
 * @return Pointer to global MIDI parser instance
 */
MidiParser_t *MidiParser_GetInstance(void)
{
  return &g_midi_parser;
}
