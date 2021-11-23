/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

#define SPECIAL_CHECK_TIME 7500

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

void keyMode();
void LEDMode();

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Define the pins uses for acols, arows, and ledSelect
// For effiency, the program no longer uses these so it
// can do operations in bulk (e.g. 7 out of the 8 pins
// in acols are on GPIOB, so they can be set all at once)

// If the KIM-UNO board had been designed specifically for
// the blackpill, the pins would probably line up better and
// not have to span across multiple GPIO ports, but I am just
// sticking a blackpill onto the original Arduino Mini Pro
// socket.

GPIO_TypeDef *acols_bank[8] = {
		GPIOA, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB
};

uint16_t acols_pin[8] = {
		GPIO_PIN_15, GPIO_PIN_3, GPIO_PIN_4, GPIO_PIN_5,
		GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_11
};

GPIO_TypeDef *arows_bank[3] = {
		GPIOB, GPIOC, GPIOA
};

uint16_t arows_pin[3] = {
		GPIO_PIN_9, GPIO_PIN_15, GPIO_PIN_0
};

GPIO_TypeDef *ledSelect_bank[7] = {
		GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOA, GPIOC
};

uint16_t ledSelect_pin[7] = {
		GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3, GPIO_PIN_4,
		GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_14
};

uint8_t RAM[0x8000];
uint8_t RIOT002_RAM[64];
uint8_t RIOT003_RAM[64];

typedef struct TIMER {
    uint32_t mult;
    uint32_t tick_accum;
    uint32_t start_value;
    int32_t count;
    uint32_t timeout;
} TIMER;

typedef struct RIOT {
    uint8_t padd, sad;
    uint8_t pbdd, sbd;
    TIMER timer;
} RIOT;

struct RIOT riot003;
struct RIOT riot002;

void riot002write(uint16_t, uint8_t);
void riot003write(uint16_t, uint8_t);

void reset_timer(TIMER *, int, uint8_t);
void update_timer(TIMER *, uint32_t);

int check_special();

uint8_t sst_mode;
uint8_t key_mode;

extern const uint8_t RIOT002_ROM[1024];
extern const uint8_t RIOT003_ROM[1024];

#ifdef GLOBAL_REGISTERS
register uint16_t pc asm ("r6");
register uint8_t a asm ("r7");
register uint8_t x asm ("r8");
register uint8_t y asm ("r9");
register uint8_t status asm ("r10");
extern uint8_t sp;

#else
extern uint16_t pc;
extern uint8_t sp, a, x, y, status;
#endif

extern void reset6502();
extern void exec6502(uint32_t);
extern void step6502();
extern void nmi6502();

uint8_t riot002read(uint16_t);
uint8_t riot003read(uint16_t);

uint8_t receive_char;
uint8_t transmit_char;
uint8_t serial_mode;
int paper_tape_line_len;
uint8_t paper_tape_line[1024];

uint8_t read6502(uint16_t addr) {
  if ((addr >= 0x1c00) && (addr < 0x2000)) {
    return RIOT002_ROM[addr-0x1c00];
  } else if (addr < 0x1000) {
    return RAM[addr];
  } else if ((addr >= 0x1800) && (addr < 0x1c00)) {
    return RIOT003_ROM[addr-0x1800];
  } else if ((addr >= 0x1780) && (addr < 0x17c0)) {
    return RIOT003_RAM[addr-0x1780];
  } else if ((addr >= 0x17c0) && (addr < 0x1800)) {
    return RIOT002_RAM[addr-0x17c0];
  } else if ((addr >= 0x1700) && (addr < 0x1740)) {
     return riot003read(addr);
  } else if ((addr >= 0x1740) && (addr < 0x1780)) {
      return riot002read(addr);
  } else if ((addr >= 0x9c00) && (addr < 0xa000)) {
      return RIOT002_RAM[addr - 0x9c00];
  } else if (addr >= 0xff00) {
      return RIOT002_ROM[addr - 0xfc00];
  } else if ((addr >= 0x2000) && (addr <= 9000)) {
      return RAM[addr-0x1000];
  } else {
      return 0;
  }
}

void write6502(uint16_t addr, uint8_t val) {
  if (addr < 0x1000) {
    RAM[addr] = val;
  } else if ((addr >= 0x1780) && (addr < 0x17c0)) {
    RIOT003_RAM[addr-0x1780] = val;
  } else if ((addr >= 0x17c0) && (addr < 0x1800)) {
    RIOT002_RAM[addr-0x17c0] = val;
  } else if ((addr >= 0x1700) && (addr < 0x1740)) {
      riot003write(addr, val);
  } else if ((addr >= 0x1740) && (addr < 0x1780)) {
      riot002write(addr, val);
  } else if ((addr >= 0x2000) && (addr <= 9000)) {
      RAM[addr - 0x1000] = val;
  }
}

uint8_t riot003read(uint16_t address) {
    if (address == 0x1700) {
        return riot003.sad;
    } else if (address == 0x1701) {
        return riot003.padd;
    } else if (address == 0x1702) {
        return riot003.sad;
    } else if (address == 0x1703) {
        return riot003.pbdd;
    } else if ((address == 0x1706) || (address == 0x170e)) {
        if (riot003.timer.timeout) {
            reset_timer(&riot003.timer, riot003.timer.mult, riot003.timer.start_value);
            riot003.timer.timeout = 0;
            riot003.timer.count = 255;
            return 0;
        } else {
            return riot003.timer.count;
        }
    } else if (address == 0x1707) {
        if (riot003.timer.timeout) {
            return 0x80;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
}

uint8_t riot002read(uint16_t address) {
    uint8_t sv;
    int bits;
    if (address == 0x1740) {
        sv = (riot002.sbd >> 1) & 0xf;
        // Return the correct key_bits if the current key depressed
        // belongs to the right scan row, otherwise 0xff, meaning
        // nothing on that row is depressed.
        // arows B9, C15, A0
        if (sv < 3) {
            // Key scan row 0, set B9 low and scan for input on acols pins
            if (sv == 0) {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 0);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
                bits = ((GPIOB->IDR >> 2) & 0x7e) | HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) |
                        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) << 7);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
            } else if (sv == 1) {
                // Key scan row 1, set C15 low and scan for input on acols pins

                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 0);
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
                bits = ((GPIOB->IDR >> 2) & 0x7e) | HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) |
                        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) << 7);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
            } else if (sv == 2) {
                // Key scan row 2, set A0 low and scan for input on acols pins

                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 0);
                bits = ((GPIOB->IDR >> 2) & 0x7e) | HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) |
                        (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) << 7);
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
            }
   			return bits;
        } else if (sv == 3) {
        	if (serial_mode || __HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
                // If there is data on the serial port, go into serial mode. When there
                // is no cable connected to tx/rx, noise on the serial line can cause
                // this to happen and you see the display go blank. A jumper across the rx/tx
                // pins on the expansion port seems to prevent this.
        		serial_mode = 1;
        		return 0;
        	} else {
        		return 0xff;
        	}
        } else {
        	return 0x80;
        }
    } else if (address == 0x1741) {
        return riot002.padd;
    } else if (address == 0x1742) {
        return riot002.sbd;
    } else if (address == 0x1743) {
        return riot002.pbdd;
    } else if ((address == 0x1746) || (address == 0x174e)) {
        if (riot002.timer.timeout) {
            reset_timer(&riot002.timer, riot002.timer.mult, riot002.timer.start_value);
            return 0;
        } else {
            return riot002.timer.count;
        }
    } else if (address == 0x1747) {
        if (riot002.timer.timeout) {
            return 0x80;
        } else {
            return 0;
        }
    }
    return 0;
}

void riot002write(uint16_t address, uint8_t value) {
    uint8_t digit;
    switch (address) {
        case 0x1740:
            riot002.sad = value;
            // Write the LED segments
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, !(value & 1));
            int bits = ((~(value & 0x7e)) << 2) | (((~value) & 0x80) << 4);
            GPIOB->ODR = (GPIOB->ODR & 0xfffff607) | bits;
            break;

        case 0x1741:
            riot002.padd = value;
            if (value == 0) {
                // If padd is in input mode, set up for reading the keyboard
                keyMode();
            } else {
                // If padd is in output mode, set up for writing LED segments
                LEDMode();
            }
            break;

        case 0x1742:
            riot002.sbd = value;
            digit = (value >> 1);
            if ((digit >= 4) && (digit <= 9)) {
                // Turn on the requested LED segment
                GPIOA->ODR = (GPIOA->ODR & 0xffffff81) | (1 << (digit-3));
            }
            break;

        case 0x1743:
            riot002.pbdd = value;
            break;

        case 0x1744:
            reset_timer(&riot002.timer, 1, value);
            break;

        case 0x1745:
            reset_timer(&riot002.timer, 8, value);
            break;

        case 0x1746:
            reset_timer(&riot002.timer, 64, value);
            break;

        case 0x1747:
            reset_timer(&riot002.timer, 1024, value);
            break;
    }
}

void riot003write(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x1700:
            riot003.sad = value;
            break;

        case 0x1701:
            riot003.padd = value;
            break;

        case 0x1702:
            riot003.sbd = value;
            break;

        case 0x1703:
            riot003.pbdd = value;
            break;

        case 0x1704:
            reset_timer(&riot003.timer, 1, value);
            break;

        case 0x1705:
            reset_timer(&riot003.timer, 8, value);
            break;

        case 0x1706:
            reset_timer(&riot003.timer, 64, value);
            break;

        case 0x1707:
            reset_timer(&riot003.timer, 1024, value);
            break;
    }
}

void init_timer(TIMER *timer) {
	timer->mult = 0;
	timer->timeout = 0;
}

void reset_timer(TIMER *timer, int scale, uint8_t start_value) {
    timer->mult = scale;
    timer->tick_accum = 0;
    timer->timeout = 0;
    timer->start_value = start_value;
    timer->count = start_value;
}

void update_timer(TIMER *timer, uint32_t ticks) {
    if (!timer->mult || timer->timeout) {
        return;
    }
    if (ticks >= timer->count) {
        timer->tick_accum++;
        if (timer->tick_accum >= timer->mult) {
            timer->timeout = 1;
            timer->tick_accum = 0;
            timer->count = timer->start_value - (ticks - timer->count);
        } else {
            timer->count = timer->start_value - (ticks - timer->count);
        }
    } else {
        timer->count -= ticks;
    }
}

// Reads a paper tape input line, filtering out anything that isn't part of the paper tape
// protocol, and expecting it to start with a semicolon
int read_paper_tape_line() {
    paper_tape_line_len = 0;
    for (;;) {
        while ((HAL_UART_Receive(&huart1, &receive_char, 1, 1000)) != HAL_OK) {
            if (check_special()) {
                return 1;
            }
        }

        if (receive_char == '\n') {
            return 0;
        } else if (receive_char == '\r') {
            continue;
        } else if ((receive_char != ';') &&
            !(((receive_char >= '0') && (receive_char <= '9')) ||
                    (( receive_char >= 'a') && (receive_char <= 'f')) ||
                    ((receive_char >= 'A') && (receive_char <= 'F')))) {
            continue;
        }
        paper_tape_line[paper_tape_line_len++] = receive_char;
        if (paper_tape_line_len == sizeof(paper_tape_line)) {
            return 0;
        }
    }
}

int paper_tape_read_byte(int *paper_tape_pos, uint8_t *b) {
    uint8_t ch;
    if (*paper_tape_pos >= paper_tape_line_len) return 1;
    ch = paper_tape_line[*paper_tape_pos];
    *paper_tape_pos += 1;
    if ((ch >= '0') && (ch <= '9')) {
        *b = (uint8_t) ((ch - '0') << 4);
    } else if ((ch >= 'A') && (ch <= 'F')) {
        *b = (uint8_t) ((ch - 'A' + 10) << 4);
    } else if ((ch >= 'a') && (ch <= 'f')) {
        *b = (uint8_t) ((ch - 'a' + 10) << 4);
    }
    if (*paper_tape_pos >= paper_tape_line_len) return 1;

    ch = paper_tape_line[*paper_tape_pos];
    *paper_tape_pos += 1;
    if ((ch >= '0') && (ch <= '9')) {
        *b = *b + (uint8_t) (ch - '0');
    } else if ((ch >= 'A') && (ch <= 'F')) {
        *b = *b + (uint8_t) (ch - 'A' + 10);
    } else if ((ch >= 'a') && (ch <= 'f')) {
        *b = *b + (uint8_t) (ch - 'a' + 10);
    }
    return 0;
}

int paper_tape_read_word(int *paper_tape_pos, uint16_t *w) {
    uint8_t b1, b2;
    if (paper_tape_read_byte(paper_tape_pos, &b1)) return 1;
    if (paper_tape_read_byte(paper_tape_pos, &b2)) return 1;
    *w = (b1 << 8) + b2;
    return 0;
}

// THis is one of the few places I override the action of the KIM-1 ROM. I couldn't
// get the paper tape working well without hardware flow control, but by letting
// the ARM chip do the work, it can keep up with a faster baud rate.
// It returns to either LOADER, the spot in the KIM-1 ROM that displays an error
// when loading a paper tape, or LOAD7 where it goes when it succeeds.
void paper_tape_receive() {
    uint8_t count, b1;
    int checksum;
    int paper_tape_pos;
    uint16_t target_checksum;
    uint16_t addr;

    for (;;) {
        if (check_special()) return;

        if (read_paper_tape_line()) return;

        HAL_UART_Transmit(&huart1, paper_tape_line, paper_tape_line_len, 2000);
        HAL_UART_Transmit(&huart1, "\r\n", 2, 2000);

        if (paper_tape_line_len < 9) {
            pc = 0x1d3e; // LOADER
            return;
        }

        if (paper_tape_line[0] != ';') {
            pc = 0x1d3e; // LOADER
            return;
        }

        paper_tape_pos = 1;

        if (paper_tape_read_byte(&paper_tape_pos, &count)) {
            pc = 0x1d3e; // LOADER
            return;
        }

        if (paper_tape_read_word(&paper_tape_pos, &addr)) {
            pc = 0x1d3e; // LOADER
            return;
        }

        checksum = (addr >> 8) + (addr & 0xff) + count;

        for (int i = 0; i < count; i++) {
            if (paper_tape_read_byte(&paper_tape_pos, &b1)) {
                pc = 0x1d3e; // LOADER
                return;
            }
            checksum = (checksum + b1) & 0xffff;
            write6502(addr, b1);
            addr++;
        }
        if (paper_tape_read_word(&paper_tape_pos, &target_checksum)) {
            pc = 0x1d3e; // LOADER
            return;
        }

        if (target_checksum != checksum) {
            pc = 0x1d3e; // LOADER
            return;
        }

        if (count == 0) {
            pc = 0x1d2e; // LOAD7
            return;
        }
    }
}

void check_pc() {
    // Serial IO in the KIM-1 is hard to emulate because it is based on CPU timing.
    // As long as you use the ROM routines to read/write, the emulator will work
    // because it notices when you are trying to read/write a serial char and takes over

	if (pc == 0x1e5a) {  // GETCH - read serial char
        // Look at the return addr to see if this might have been called from the
        // paper tape read
		int return_addr = (RAM[0x102+sp] << 8) + RAM[0x101+sp];
        if (return_addr == 0x1ce9) {
            // If a paper tape read, clear the return value from the stack since
            // we will return straight to whatever called LOAD, and use the ARM
            // paper tape reader
            sp += 2; // Remove the call to GETCH from the stack
			paper_tape_receive();
			return;
		}

        // Read a serial char
		while (HAL_UART_Receive(&huart1, &receive_char, 1, 1000) != HAL_OK);
		a = receive_char;

        // Allow control-D to go back to the built-in keyboard/display
		if (receive_char == 4) {
			serial_mode = 0;
			RAM[0x102+sp] = 0x1c;
			RAM[0x101+sp] = 0x4e;  // to escape serial mode, jump back to START symbol
			                       // change the return address on the stack from 1c6c to 1c4e
		}
		y = 0xff;
		pc = 0x1e85;
	} else if (pc == 0x1ea0) {
        // OUTCH - print a serial char
		transmit_char = a;
		HAL_UART_Transmit(&huart1, &transmit_char, 1, 100);
		pc = 0x1ed3;
	}
}

// On the original KIM-1, the ST, RS, and SST buttons/switch went straight
// to pins on the 6502, so they could occur at any time, and not just when
// the KIM-1 was scanning input. Since the Blackpill lets the KIM-1 do the
// keyboard scanning, we have to do a periodic check for these special keys
// to allow you to hit them when the KIM-1 isn't checking the keyboard.
int check_special() {
	if (key_mode) {  // Only do this when acols pins are set to input
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 0);

        // Check scan line 0
		int bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
		if (bit == 0) {  // Was a key hit?
			HAL_Delay(50);  // Delay to debounce
			bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
    		if (bit == 0) { // If the key is still down, do an NMI
    			while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == 0);
                // KIM-1 has hardware circuitry to prevent NMI when
                // address lines 10,11,12 are all high
                if (!(pc & 0x1c00)) {
                    nmi6502();
                }
    			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
                serial_mode = 0;
    			return 1;
    		}
    		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
    		return 0;
		}
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);

        // Check scan line 1
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 0);
		bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
		if (bit == 0) { // key pressed?
			HAL_Delay(50);  // debounce delay
			bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
    		if (bit == 0) {
    			while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == 0);
    			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
    			reset6502();
                serial_mode = 0;
    			return 1;
    		}
		}
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);

        // Check scan line 2
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 0);
		bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
		if (bit == 0) {  // key hit
			if (sst_mode) {  // If we are already in SST mode, do a 50ms debounce delay
				HAL_Delay(50);
				bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
	    		if (bit == 0) {
	    			sst_mode = 0;
	    			while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == 0);
	    		}
			} else {
				int flag = 1;
				for (int j=0; j < 10; j++) { // Require the user to hold SST for 1 second
					HAL_Delay(100);
					bit = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11);
					if (bit == 1) { // If at any time the key is released, quit
						flag = 0;
						break;
					}
				}
				if (flag) { // If the key was held for 1 second, wait for it to be released
	    			while (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_11) == 0);
				}
				sst_mode = flag;
			}
            serial_mode = 0;
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
			return 1;
		}
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, 1);
		return 0;
	}

}
/* USER CODE END 0 */

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, 0);

  for (int i=0; i < 8; i++) {
	  HAL_GPIO_WritePin(acols_bank[i], acols_pin[i], 1);
  }

  for (int i=0; i < 3; i++) {
	  HAL_GPIO_WritePin(arows_bank[i], arows_pin[i], 0);
  }

  for (int i=0; i < 7; i++) {
	  HAL_GPIO_WritePin(ledSelect_bank[i], ledSelect_pin[i], 0);
  }

  memset(&riot002, 0, sizeof(RIOT));
  memset(&riot003, 0, sizeof(RIOT));

  init_timer(&riot002.timer);
  init_timer(&riot003.timer);

  // Set the vectors that the KIM-1 ROM uses
  write6502(0x17fa, 0);
  write6502(0x17fb, 0x1c);
  write6502(0x17fe, 0);
  write6502(0x17ff, 0x1c);

  // Turn single step off
  sst_mode = 0;

  serial_mode = 0;

  // Reset the CPU
  reset6502();

  // Set the countdown for checking the special keys
  int check_special_ticks = SPECIAL_CHECK_TIME;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
      // In the Kim-1 schematic it looks like there is a circuit that prevents the NMI when the
      // address line is 0x1cxx (bits 10, 11, and 12 of the address)
	  uint8_t enable_SST_NMI = sst_mode && !(pc & 0x1c00);

      step6502();

      // If we are in SST mode and can send an NMI, send it
      if (sst_mode && enable_SST_NMI) {
  		  nmi6502();
      }

      // Update the timer ticks. Since the blackpill can't run the CPU at
      // quite full speed, we update the ticks more frequently. At some point
      // maybe I will convert this to using ARM timers to get more accurate timing.
      update_timer(&riot003.timer, 16);
      update_timer(&riot002.timer, 16);

      // See if the CPU is at any interesting location
      check_pc();

      // If it is time to check for special keys, do it
      if (--check_special_ticks <= 0) {
    	  check_special_ticks = SPECIAL_CHECK_TIME;
    	  check_special();
      }
  }
  /* USER CODE END 3 */
}
#pragma clang diagnostic pop

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  key_mode = 0;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_8
                          |GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA2 PA3
                           PA4 PA5 PA6 PA8
                           PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_8
                          |GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB11 PB3 PB4 PB5
                           PB6 PB7 PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

void keyMode() {
    key_mode = 1;
	  GPIO_InitTypeDef GPIO_InitStruct = {0};

	// Set ACOLS and LED Select to input

	/*Configure GPIO pins : PC13 PC14 PC15 */
	  GPIO_InitStruct.Pin = GPIO_PIN_14;
	  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	  GPIO_InitStruct.Pull = GPIO_PULLUP;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	  /*Configure GPIO pins : PA0 PA1 PA2 PA3
	                           PA4 PA5 PA6 PA8
	                           PA15 */
	  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
	                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6
	                          |GPIO_PIN_15;
	  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	  GPIO_InitStruct.Pull = GPIO_PULLUP;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	  /*Configure GPIO pins : PB11 PB3 PB4 PB5
	                           PB6 PB7 PB8 PB9 */
	  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
	                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8;
	  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	  GPIO_InitStruct.Pull = GPIO_PULLUP;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void LEDMode() {
    key_mode = 0;
	  GPIO_InitTypeDef GPIO_InitStruct = {0};


	  /*Configure GPIO pins : PC13 PC14 PC15 */
	  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	  /*Configure GPIO pins : PA0 PA1 PA2 PA3
	                           PA4 PA5 PA6 PA8
	                           PA15 */
	  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
	                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_8
	                          |GPIO_PIN_15;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	  /*Configure GPIO pins : PB11 PB3 PB4 PB5
	                           PB6 PB7 PB8 PB9 */
	  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
	                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
	  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	  GPIO_InitStruct.Pull = GPIO_NOPULL;
	  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);


}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
