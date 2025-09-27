/* Chip8
Core (CPU/memory) of CHIP-8 - Interfaccia pulita per essere usata da main.c
Riferimenti: Multigesture tutorial, Cowgod's Chip-8 Technical Reference.

Usa solo tipi C standard (stdint.h) per chiarezza.
*/

#ifndef CHIP_H
#define CHIP_H

#include <stdint.h>

#define CHIP8_MEM_SIZE 4096 // bytes
#define CHIP8_START_ADDR 0x200 // it starts on register 512 (hex)
#define CHIP8_FONT_ADDR 0x50
#define CHIP8_SCREEN_W 64 // width Pixels
#define CHIP8_SCREEN_H 32 // height Pixels

typedef struct {
  uint16_t opcode; // per debug
  uint8_t memory[CHIP8_MEM_SIZE];
  uint8_t V[16]; // registers V0..VF
  uint16_t I;    // index register
  uint16_t pc;   // program counter
  uint8_t gfx[CHIP8_SCREEN_W * CHIP8_SCREEN_H]; // framebuffer (0/1)
  uint8_t delay_timer;
  uint8_t sound_timer;
  uint16_t stack[16];
  uint8_t sp; // stack pointer
  uint8_t key[16]; // keypad state (0/1)
  uint8_t draw_flag; // set = we have to re-draw
} Chip8;

/* Functions */
void chip8_init(Chip8 *c); // reset state and reload the fontset
int chip8_load_rom(Chip8 *c, const char *path); // load ROM in memory at 0x200 (512)
void chip8_emulate_cycle(Chip8 *c); // execute 1 opcode (fetch/decode/execute
void chip8_set_key(Chip8 *c, int key, int pressed);  // updates key state
void chip8_save_state(Chip8 *c, const char *filename);
void chip8_load_state(Chip8 *c, const char *filename);


#endif /* CHIP_H */
