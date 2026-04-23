#include <ctype.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "db.h"
#include "eval.h"
#include "glyph.h"

#define SCALE 4
#define MAX_INPUT 512
#define MAX_VIEW_H 256
#define SCROLL_STEP 8

static int scroll_y = 0;
static int view_w = 0;
static int view_h = 0;

static void
content_bounds(Database* db, int* w, int* h)
{
  int x0, y0, x1, y1;
  image_bounds(&db->img, &x0, &y0, &x1, &y1);
  *w = x1 + 2;
  *h = y1 + 2;
  if (*w < 64) {
    *w = 64;
  }
  if (*h < 64) {
    *h = 64;
  }
}

static void
update_view(Database* db)
{
  content_bounds(db, &view_w, &view_h);
  if (view_h > MAX_VIEW_H) {
    view_h = MAX_VIEW_H;
  }
}

static void
clamp_scroll(Database* db)
{
  int max = db->img.height - view_h;
  if (max < 0) {
    max = 0;
  }
  if (scroll_y > max) {
    scroll_y = max;
  }
  if (scroll_y < 0) {
    scroll_y = 0;
  }
}

static SDL_Texture*
remake_texture(SDL_Renderer* renderer, SDL_Texture* old,
               SDL_Window* window, Database* db)
{
  if (old) {
    SDL_DestroyTexture(old);
  }
  update_view(db);
  SDL_SetWindowSize(window, view_w * SCALE, view_h * SCALE);
  return SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                           SDL_TEXTUREACCESS_STREAMING,
                           view_w, view_h);
}

static void
render(SDL_Renderer* renderer, SDL_Texture* texture, Database* db)
{
  Image* img = &db->img;
  uint8_t* packed = malloc(view_w * view_h * 3);
  memset(packed, 255, view_w * view_h * 3);

  for (int y = 0; y < view_h; y++) {
    int sy = y + scroll_y;
    if (sy < 0 || sy >= img->height) {
      continue;
    }
    for (int x = 0; x < view_w && x < img->width; x++) {
      int src = (sy * img->alloc_width + x) * 3;
      int dst = (y * view_w + x) * 3;
      packed[dst] = img->data[src];
      packed[dst + 1] = img->data[src + 1];
      packed[dst + 2] = img->data[src + 2];
    }
  }

  SDL_UpdateTexture(texture, NULL, packed, view_w * 3);
  free(packed);

  SDL_RenderClear(renderer);
  SDL_Rect dst_rect = { 0, 0, view_w * SCALE, view_h * SCALE };
  SDL_RenderCopy(renderer, texture, NULL, &dst_rect);
  SDL_RenderPresent(renderer);
}

static Stack stack = { 0 };

static void
handle_input(char* line, Database* db, bool* running)
{
  while (*line && isspace(*line)) {
    line++;
  }
  if (!*line) {
    return;
  }

  if (strcasecmp(line, "quit") == 0 || strcasecmp(line, "exit") == 0) {
    *running = false;
    return;
  }

  forth_eval(line, db, &stack);
}

int
main(int argc, char* argv[])
{
  Database db;
  db_init(&db);

  if (argc > 1) {
    if (db_load(&db, argv[1]) == 0) {
      printf("Loaded %s\n", argv[1]);
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }

  update_view(&db);

  SDL_Window* window =
    SDL_CreateWindow("fliptable",
                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                     view_w * SCALE, view_h * SCALE,
                     SDL_WINDOW_SHOWN);
  SDL_Renderer* renderer =
    SDL_CreateRenderer(window, -1,
                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* texture =
    remake_texture(renderer, NULL, window, &db);

  printf("> ");
  fflush(stdout);

  struct pollfd fds[1];
  fds[0].fd = 0;
  fds[0].events = POLLIN;

  bool running = true;
  char input[MAX_INPUT];

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_MOUSEWHEEL) {
        scroll_y -= event.wheel.y * SCROLL_STEP;
        clamp_scroll(&db);
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_UP) {
          scroll_y -= SCROLL_STEP;
          clamp_scroll(&db);
        } else if (event.key.keysym.sym == SDLK_DOWN) {
          scroll_y += SCROLL_STEP;
          clamp_scroll(&db);
        }
      }
    }

    if (poll(fds, 1, 0) > 0 && (fds[0].revents & POLLIN)) {
      if (fgets(input, MAX_INPUT, stdin)) {
        input[strcspn(input, "\n")] = '\0';
        handle_input(input, &db, &running);
        texture = remake_texture(renderer, texture, window, &db);
        clamp_scroll(&db);

        printf("> ");
        fflush(stdout);
      } else {
        running = false;
      }
    }

    render(renderer, texture, &db);
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  db_free(&db);
  return 0;
}
