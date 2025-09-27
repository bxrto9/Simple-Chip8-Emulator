/* chip8.c
 * Implementazione core CHIP-8 (fetch/decode/execute).
 * Non contiene I/O; main.c si occupa di SDL, input e audio.
 *
 * Abilitare/disabilitare comportamenti "quirky" via macro:
 *  - QUIRK_LOADSTORE_I_INC : se FX55/Fx65 incrementano I (storico) o no.
 *  - QUIRK_SHIFT_VY        : se gli shift prendono Vy invece di Vx (alcune implementazioni).
 *  - QUIRK_SPRITE_WRAP     : se i sprite possono wrap-around sullo schermo.
 */

#include "chip.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* comportamenti "quirky" */
#define QUIRK_SHIFT_VY        0
#define QUIRK_LOADSTORE_I_INC 1
#define QUIRK_SPRITE_WRAP     1

/* fontset standard CHIP-8, 5 byte per carattere */
static const uint8_t chip8_fontset[80] = {
    0xF0,0x90,0x90,0x90,0xF0, // 0
    0x20,0x60,0x20,0x20,0x70, // 1
    0xF0,0x10,0xF0,0x80,0xF0, // 2
    0xF0,0x10,0xF0,0x10,0xF0, // 3
    0x90,0x90,0xF0,0x10,0x10, // 4
    0xF0,0x80,0xF0,0x10,0xF0, // 5
    0xF0,0x80,0xF0,0x90,0xF0, // 6
    0xF0,0x10,0x20,0x40,0x40, // 7
    0xF0,0x90,0xF0,0x90,0xF0, // 8
    0xF0,0x90,0xF0,0x10,0xF0, // 9
    0xF0,0x90,0xF0,0x90,0x90, // A
    0xE0,0x90,0xE0,0x90,0xE0, // B
    0xF0,0x80,0x80,0x80,0xF0, // C
    0xE0,0x90,0x90,0x90,0xE0, // D
    0xF0,0x80,0xF0,0x80,0xF0, // E
    0xF0,0x80,0xF0,0x80,0x80  // F
};

/* inizializza CHIP-8: reset memoria, registri, PC, fontset */
void chip8_init(Chip8 *c) {
    memset(c, 0, sizeof(Chip8));              // reset totale
    c->pc = CHIP8_START_ADDR;                 // PC parte da 0x200
    c->I = 0;
    c->sp = 0;
    c->delay_timer = 0;
    c->sound_timer = 0;
    c->draw_flag = 1;
    memset(c->V, 0, sizeof(c->V));
    memset(c->stack, 0, sizeof(c->stack));
    memset(c->gfx, 0, sizeof(c->gfx));
    memcpy(&c->memory[CHIP8_FONT_ADDR], chip8_fontset, sizeof(chip8_fontset));
    srand((unsigned)time(NULL));
}

/* carica ROM in memoria a partire da 0x200 */
int chip8_load_rom(Chip8 *c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (sz + CHIP8_START_ADDR) > CHIP8_MEM_SIZE) { fclose(f); return 0; }
    fread(&c->memory[CHIP8_START_ADDR], 1, sz, f);
    fclose(f);
    return 1;
}

/* aggiorna stato tasto (0-15) */
void chip8_set_key(Chip8 *c, int key, int pressed) {
    if (key >= 0 && key < 16) c->key[key] = pressed ? 1 : 0;
}

// salva lo stato della macchina in un file
void chip8_save_state(Chip8 *c, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    fwrite(c, sizeof(Chip8), 1, f);
    fclose(f);
}

// carica lo stato della macchina da un file
void chip8_load_state(Chip8 *c, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return;
    fread(c, sizeof(Chip8), 1, f);
    fclose(f);
}


/* esegue un ciclo fetch-decode-execute */
void chip8_emulate_cycle(Chip8 *c) {
    /* fetch opcode (2 byte) */
    uint16_t opcode = (c->memory[c->pc] << 8) | c->memory[c->pc + 1];
    c->pc += 2;

    /* decodifica campi principali dell'opcode */
    uint16_t nnn = opcode & 0x0FFF;          // indirizzo 12-bit
    uint8_t  nn  = opcode & 0x00FF;          // byte immediato
    uint8_t  n   = opcode & 0x000F;          // nibble più basso
    uint8_t  x   = (opcode & 0x0F00) >> 8;   // registro X
    uint8_t  y   = (opcode & 0x00F0) >> 4;   // registro Y

    switch (opcode & 0xF000) {
        case 0x0000:
            if (nn == 0xE0) {
                /* 00E0: cancella schermo */
                memset(c->gfx, 0, sizeof(c->gfx));
                c->draw_flag = 1;
            } else if (nn == 0xEE) {
                /* 00EE: ritorna da subroutine */
                c->sp--;
                c->pc = c->stack[c->sp];
            }
            break;
        case 0x1000: c->pc = nnn; break;            // 1NNN JP
        case 0x2000: c->stack[c->sp++] = c->pc; c->pc = nnn; break; // 2NNN CALL
        case 0x3000: if (c->V[x] == nn) c->pc += 2; break; // 3XNN
        case 0x4000: if (c->V[x] != nn) c->pc += 2; break; // 4XNN
        case 0x5000: if ((opcode & 0x000F) == 0 && c->V[x] == c->V[y]) c->pc += 2; break; // 5XY0
        case 0x6000: c->V[x] = nn; break;           // 6XNN
        case 0x7000: c->V[x] += nn; break;          // 7XNN
        case 0x8000:                                 // operazioni tra registri
            switch (n) {
                case 0x0: c->V[x] = c->V[y]; break;
                case 0x1: c->V[x] |= c->V[y]; break;
                case 0x2: c->V[x] &= c->V[y]; break;
                case 0x3: c->V[x] ^= c->V[y]; break;
                case 0x4: { uint16_t sum = c->V[x]+c->V[y]; c->V[0xF]=sum>0xFF; c->V[x]=sum&0xFF; } break;
                case 0x5: c->V[0xF] = c->V[x] > c->V[y]; c->V[x]-=c->V[y]; break;
                case 0x6:
#if QUIRK_SHIFT_VY
                    c->V[0xF] = c->V[y] & 1; c->V[x] = c->V[y] >> 1;
#else
                    c->V[0xF] = c->V[x] & 1; c->V[x] >>= 1;
#endif
                    break;
                case 0x7: c->V[0xF] = c->V[y] > c->V[x]; c->V[x]=c->V[y]-c->V[x]; break;
                case 0xE:
#if QUIRK_SHIFT_VY
                    c->V[0xF]=(c->V[y]&0x80)>>7; c->V[x]=c->V[y]<<1;
#else
                    c->V[0xF]=(c->V[x]&0x80)>>7; c->V[x]<<=1;
#endif
                    break;
            }
            break;
        case 0x9000: if ((opcode&0x000F)==0 && c->V[x]!=c->V[y]) c->pc+=2; break;
        case 0xA000: c->I = nnn; break;            // ANNN
        case 0xB000: c->pc = nnn + c->V[0]; break; // BNNN
        case 0xC000: c->V[x] = (rand() & 0xFF) & nn; break; // CXNN
        case 0xD000: {                              // DXYN: draw sprite
            uint8_t vx=c->V[x]%CHIP8_SCREEN_W;
            uint8_t vy=c->V[y]%CHIP8_SCREEN_H;
            uint8_t height=n;
            c->V[0xF]=0;
            for(int row=0; row<height; ++row){
                uint8_t sprite=c->memory[c->I+row];
                for(int col=0; col<8; ++col){
                    if(sprite & (0x80>>col)){
                        int px=vx+col;
                        int py=vy+row;
#if QUIRK_SPRITE_WRAP
                        px%=CHIP8_SCREEN_W; py%=CHIP8_SCREEN_H;
#else
                        if(px>=CHIP8_SCREEN_W || py>=CHIP8_SCREEN_H) continue;
#endif
                        int idx=px + py*CHIP8_SCREEN_W;
                        if(c->gfx[idx]) c->V[0xF]=1;
                        c->gfx[idx]^=1;
                    }
                }
            }
            c->draw_flag=1; // segnala a main.c di ridisegnare lo schermo
        } break;
        case 0xE000: // opcodes input
            if(nn==0x9E && c->key[c->V[x]]) c->pc+=2;
            else if(nn==0xA1 && !c->key[c->V[x]]) c->pc+=2;
            break;
        case 0xF000: // timers, memory, key
            switch(nn){
                case 0x07: c->V[x]=c->delay_timer; break;
                case 0x0A:{ int pressed=0; for(int k=0;k<16;++k) if(c->key[k]){c->V[x]=k;pressed=1;break;} if(!pressed)c->pc-=2;} break;
                case 0x15: c->delay_timer=c->V[x]; break;
                case 0x18: c->sound_timer=c->V[x]; break;
                case 0x1E: c->I+=c->V[x]; break;
                case 0x29: c->I=CHIP8_FONT_ADDR+(c->V[x]*5); break; // carattere font
                case 0x33:{ // BCD
                    uint8_t val=c->V[x];
                    c->memory[c->I+0]=val/100;
                    c->memory[c->I+1]=(val/10)%10;
                    c->memory[c->I+2]=val%10;
                } break;
            case 0x55: { // store V0..Vx
              for (int i = 0; i <= x; ++i) c->memory[c->I + i] = c->V[i];
#if QUIRK_LOADSTORE_I_INC
              c->I += x + 1;  // necessario per Pong
#endif
            } break;

            case 0x65: { // read V0..Vx
              for (int i = 0; i <= x; ++i) c->V[i] = c->memory[c->I + i];
#if QUIRK_LOADSTORE_I_INC
              c->I += x + 1;  // necessario per Pong
#endif
            } break;
            } break;
    default:
      printf("Opcode ///: %04X\n", opcode);
      break;
    }

    c->opcode = opcode;
}
