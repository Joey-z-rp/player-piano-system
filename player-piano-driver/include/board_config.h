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

#if (BOARD_NUMBER < 0) || (BOARD_NUMBER > 5)
#error BOARD_NUMBER must be 0 through 5
#elif BOARD_NUMBER == 0
#define KEY_BASE 0
#define LOCAL_CHANNELS 13
#else
#define KEY_BASE (13 + ((BOARD_NUMBER - 1) * 15))
#define LOCAL_CHANNELS 15
#endif

#endif // BOARD_CONFIG_H
