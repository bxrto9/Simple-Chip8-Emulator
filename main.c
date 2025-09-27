/* main.c
 * CHIP-8 Emulator con menu grafico SDL2 + testo SDL_ttf
 * Compila: gcc main.c chip8.c -o chip8 -lSDL2 -lSDL2_ttf -std=c99
 */

#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "chip.h"

#define PIXEL_SCALE 10
#define WINDOW_W (CHIP8_SCREEN_W * PIXEL_SCALE)
#define WINDOW_H (CHIP8_SCREEN_H * PIXEL_SCALE)

// Palette pixel OFF / ON
SDL_Color palette[2] = {
    {0, 0, 0, 255},          // OFF
    {255, 165, 0, 255}       // ON (arancione)
};

// Mapping tasti CHIP-8
static int map_sdlkey_to_chip8(SDL_Keysym ks) {
    switch (ks.sym) {
        case SDLK_1: return 0x1; case SDLK_2: return 0x2; case SDLK_3: return 0x3; case SDLK_4: return 0xC;
        case SDLK_q: return 0x4; case SDLK_w: return 0x5; case SDLK_e: return 0x6; case SDLK_r: return 0xD;
        case SDLK_a: return 0x7; case SDLK_s: return 0x8; case SDLK_d: return 0x9; case SDLK_f: return 0xE;
        case SDLK_z: return 0xA; case SDLK_x: return 0x0; case SDLK_c: return 0xB; case SDLK_v: return 0xF;
        default: return -1;
    }
}

// Audio square wave
void audio_callback(void *userdata, Uint8 *stream, int len) {
    static int phase = 0;
    Sint16 *buf = (Sint16*)stream;
    int samples = len / 2;
    for (int i = 0; i < samples; ++i) {
        buf[i] = (phase & 1) ? 2000 : -2000;
        phase++;
        if (phase > 480) phase = 0;
    }
}

// Debug overlay su console
void print_debug(Chip8 *chip) {
    printf("\rPC:%03X I:%03X DT:%d ST:%d ", chip->pc, chip->I, chip->delay_timer, chip->sound_timer);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }

    if(TTF_Init()!=0) {
        fprintf(stderr,"TTF_Init: %s\n", TTF_GetError()); return 1;
    }

    SDL_Window *win = SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, 0);
    SDL_Renderer *rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 2048;
    want.callback = audio_callback;
    if (SDL_OpenAudio(&want,&have)>=0) SDL_PauseAudio(0);

    // FONT
    TTF_Font *font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24);
    if(!font){ fprintf(stderr,"TTF_OpenFont: %s\n",TTF_GetError()); return 1; }

    // MENU GRAFICO SDL
    const char *roms[] = {"TETRIS.ch8","PONG.ch8","OCTOJAM2TITLE.ch8"};
    int num_roms = 3;
    int selection = 0;
    int launcher = 1;

    while(launcher) {
        // SFONDO ROSA
        SDL_SetRenderDrawColor(rend,255,192,203,255);
        SDL_RenderClear(rend);

        for(int i=0;i<num_roms;i++) {
            // rettangolo selezione
            if(i==selection) SDL_SetRenderDrawColor(rend,255,165,0,255); // arancione
            else SDL_SetRenderDrawColor(rend,255,255,255,255); // bianco
            SDL_Rect r = {WINDOW_W/4, 100 + i*60, WINDOW_W/2, 50};
            SDL_RenderFillRect(rend,&r);

            // testo
            SDL_Color textColor = {0,0,0,255};
            SDL_Surface *surf = TTF_RenderText_Solid(font, roms[i], textColor);
            SDL_Texture *tex = SDL_CreateTextureFromSurface(rend,surf);
            SDL_Rect textRect = {r.x+10,r.y+10,surf->w,surf->h};
            SDL_RenderCopy(rend,tex,NULL,&textRect);
            SDL_FreeSurface(surf);
            SDL_DestroyTexture(tex);
        }

        SDL_RenderPresent(rend);

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT) exit(0);
            else if(e.type==SDL_KEYDOWN) {
                if(e.key.keysym.sym==SDLK_UP) selection=(selection+num_roms-1)%num_roms;
                if(e.key.keysym.sym==SDLK_DOWN) selection=(selection+1)%num_roms;
                if(e.key.keysym.sym==SDLK_RETURN) launcher=0;
                if(e.key.keysym.sym==SDLK_ESCAPE) exit(0);
            }
        }
        SDL_Delay(50);
    }

    printf("Avvio %s...\n", roms[selection]);

    Chip8 chip;
    chip8_init(&chip);
    if(!chip8_load_rom(&chip, roms[selection])) { fprintf(stderr,"Failed to load ROM\n"); return 2; }

    uint32_t last_timer = SDL_GetTicks();
    const int cycles_per_frame = 8;
    int running = 1;

    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT) running=0;
            else if(e.type==SDL_KEYDOWN) {
                if(e.key.keysym.sym==SDLK_ESCAPE) running=0;
                else if(e.key.keysym.sym==SDLK_s) chip8_save_state(&chip,"save.ch8");
                else if(e.key.keysym.sym==SDLK_l) chip8_load_state(&chip,"save.ch8");
                int k = map_sdlkey_to_chip8(e.key.keysym);
                if(k>=0) chip8_set_key(&chip,k,1);
            } else if(e.type==SDL_KEYUP) {
                int k = map_sdlkey_to_chip8(e.key.keysym);
                if(k>=0) chip8_set_key(&chip,k,0);
            }
        }

        for(int i=0;i<cycles_per_frame;i++) chip8_emulate_cycle(&chip);

        uint32_t now = SDL_GetTicks();
        if(now-last_timer>=1000/60) {
            if(chip.delay_timer>0) chip.delay_timer--;
            if(chip.sound_timer>0) chip.sound_timer--;
            if(chip.sound_timer>0) SDL_PauseAudio(0);
            else SDL_PauseAudio(1);
            last_timer=now;
        }

        if(chip.draw_flag) {
            SDL_SetRenderDrawColor(rend,0,0,0,255);
            SDL_RenderClear(rend);

            SDL_Rect r; r.w=PIXEL_SCALE; r.h=PIXEL_SCALE;
            for(int y=0;y<CHIP8_SCREEN_H;y++) {
                for(int x=0;x<CHIP8_SCREEN_W;x++) {
                    int idx = x + y*CHIP8_SCREEN_W;
                    if(chip.gfx[idx]) {
                        int flick = rand()%50;
                        SDL_SetRenderDrawColor(rend,
                            palette[1].r - flick,
                            palette[1].g - flick,
                            palette[1].b - flick, 255);
                        r.x = x*PIXEL_SCALE; r.y = y*PIXEL_SCALE;
                        SDL_RenderFillRect(rend,&r);
                    }
                }
            }

            print_debug(&chip);
            SDL_RenderPresent(rend);
            chip.draw_flag=0;
        }

        SDL_Delay(1);
    }

    TTF_CloseFont(font);
    TTF_Quit();
    SDL_CloseAudio();
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
