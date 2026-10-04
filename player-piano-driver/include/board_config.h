#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Change this to 0-5 before flashing each board.
//   0: keys  0-12 (13 channels, lowest notes)
//   1: keys 13-27
//   2: keys 28-42
//   3: keys 43-57
//   4: keys 58-72
//   5: keys 73-87
#define BOARD_NUMBER 0

#define PIANO_NUM_KEYS 88

// Pedal solenoids use board 0's PWM 13 and 14: PA9, PA8.
// Addressed as keys 88 and 89.
#define PEDAL_KEY_A 88
#define PEDAL_KEY_B 89

#if (BOARD_NUMBER < 0) || (BOARD_NUMBER > 5)
#error BOARD_NUMBER must be 0 through 5
#elif BOARD_NUMBER == 0
#define KEY_BASE 0
#define LOCAL_CHANNELS 13
#define PEDAL_LOCAL_CHANNELS 2
#else
#define KEY_BASE (13 + ((BOARD_NUMBER - 1) * 15))
#define LOCAL_CHANNELS 15
#define PEDAL_LOCAL_CHANNELS 0
#endif

#endif // BOARD_CONFIG_H
