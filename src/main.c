#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <SDL2/SDL.h>

#include "cli.h"
#include "db.h"
#include "eval.h"
#include "glyph.h"

#define DEFAULT_H 64
#define DEFAULT_W 128

#define SCALE 4
#define SCROLL_STEP 8
#define INPUT_ROW_H (FONT_HEIGHT + 2 + ROW_GAP)

static int scroll_y = 0;
static int content_w = 0;
static int content_h = 0;
static int win_vw = 0;
static int win_vh = 0;
static bool show_grid = false;

static void
content_bounds(Database* db, int* w, int* h)
{
  int x0, y0, x1, y1;
  image_bounds(&db->img, &x0, &y0, &x1, &y1);
  *w = x1 + 2;
  *h = y1 + 2;
  if (*w < DEFAULT_W) {
    *w = DEFAULT_W;
  }
  if (*h < DEFAULT_H) {
    *h = DEFAULT_H;
  }
}

static void
update_content(Database* db)
{
  content_bounds(db, &content_w, &content_h);
  content_h += INPUT_ROW_H;
}

static void
update_win_size(SDL_Window* window)
{
  int pw, ph;
  SDL_GetWindowSize(window, &pw, &ph);
  win_vw = pw / SCALE;
  win_vh = ph / SCALE;
}

static void
clamp_scroll(void)
{
  int visible = win_vh - INPUT_ROW_H;
  int max = content_h - INPUT_ROW_H - visible;
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
  update_content(db);
  update_win_size(window);
  return SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                           SDL_TEXTUREACCESS_STREAMING,
                           win_vw, win_vh);
}

static void
render(SDL_Renderer* renderer, SDL_Texture* texture, Database* db, Cli* cli)
{
  Image* img = &db->img;
  uint8_t* packed = malloc(win_vw * win_vh * 3);
  memset(packed, 255, win_vw * win_vh * 3);

  write_text(packed, win_vw, ">", 1, 1, 120, 120, 120);
  int text_x = 1 + FONT_WIDTH + 1 + 1;
  write_text(packed, win_vw, cli->buf, text_x, 1, 0, 0, 0);

  int cursor_x = text_x + cli->cursor * (FONT_WIDTH + 1);
  if (cursor_x < win_vw) {
    for (int cy = 0; cy < FONT_HEIGHT; cy++) {
      int idx = ((1 + cy) * win_vw + cursor_x) * 3;
      packed[idx] = 0;
      packed[idx + 1] = 56;
      packed[idx + 2] = 255;
    }
  }

  if (scroll_y > 0) {
    int dot_y = INPUT_ROW_H - 1;
    for (int x = 0; x < win_vw; x += 2) {
      int idx = (dot_y * win_vw + x) * 3;
      packed[idx] = 180;
      packed[idx + 1] = 180;
      packed[idx + 2] = 180;
    }
  }

  for (int y = INPUT_ROW_H; y < win_vh; y++) {
    int sy = y - INPUT_ROW_H + scroll_y;
    if (sy < 0 || sy >= img->height) {
      continue;
    }
    for (int x = 0; x < win_vw && x < img->width; x++) {
      int src = (sy * img->alloc_width + x) * 3;
      int dst = (y * win_vw + x) * 3;
      packed[dst] = img->data[src];
      packed[dst + 1] = img->data[src + 1];
      packed[dst + 2] = img->data[src + 2];
    }
  }

  if (show_grid) {
    int gx = FONT_WIDTH + 1;
    int gy = FONT_HEIGHT + 2;
    int max_x = win_vw < img->width ? win_vw : img->width;
    for (int y = INPUT_ROW_H; y < win_vh; y++) {
      int ay = y - INPUT_ROW_H + scroll_y;
      if (ay < 0 || ay >= img->height) {
        continue;
      }
      for (int x = 0; x < max_x; x++) {
        if (x % gx == 0 || ay % gy == 0) {
          int idx = (y * win_vw + x) * 3;
          packed[idx] = (packed[idx] + 200) / 2;
          packed[idx + 1] = (packed[idx + 1] + 220) / 2;
          packed[idx + 2] = (packed[idx + 2] + 255) / 2;
        }
      }
    }
  }

  SDL_UpdateTexture(texture, NULL, packed, win_vw * 3);
  free(packed);

  SDL_RenderClear(renderer);
  SDL_Rect dst_rect = { 0, 0, win_vw * SCALE, win_vh * SCALE };
  SDL_RenderCopy(renderer, texture, NULL, &dst_rect);
  SDL_RenderPresent(renderer);
}

static Stack stack = { 0 };

static void
handle_submit(char* line, Database* db, Cli* cli, bool* running)
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

  forth_eval(line, db, &stack, cli);
  db_sync_stack(db, stack.items, stack.top);
}

int
main(int argc, char* argv[])
{
  Database db;
  db_init(&db);

  if (argc > 1) {
    if (db_load(&db, argv[1]) == 0) {
      printf("Loaded %s\n", argv[1]);
      stack.top = db_load_stack(&db, stack.items, MAX_STACK);
      db_sync_stack(&db, stack.items, stack.top);
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }

  update_content(&db);

  SDL_Window* window =
    SDL_CreateWindow("aguafuerte",
                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                     content_w * SCALE, content_h * SCALE,
                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer =
    SDL_CreateRenderer(window, -1,
                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* texture =
    remake_texture(renderer, NULL, window, &db);

  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_StartTextInput();

  Cli cli;
  cli_init(&cli);

  bool running = true;
  char submitted[MAX_INPUT];

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;

      } else if (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        texture = remake_texture(renderer, texture, window, &db);
        clamp_scroll();

      } else if (event.type == SDL_TEXTINPUT) {
        if (!(SDL_GetModState() & KMOD_CTRL)) {
          for (int i = 0; event.text.text[i]; i++) {
            cli_insert(&cli, event.text.text[i]);
          }
        }

      } else if (event.type == SDL_MOUSEWHEEL) {
        scroll_y -= event.wheel.y * SCROLL_STEP;
        clamp_scroll();

      } else if (event.type == SDL_KEYDOWN) {
        SDL_Keymod mod = SDL_GetModState();
        SDL_Keycode key = event.key.keysym.sym;

        if (key == SDLK_RETURN) {
          if (cli_submit(&cli, submitted, sizeof(submitted))) {
            handle_submit(submitted, &db, &cli, &running);
            texture = remake_texture(renderer, texture, window, &db);
            clamp_scroll();
          }

        } else if (key == SDLK_BACKSPACE) {
          cli_backspace(&cli);
        } else if (key == SDLK_DELETE) {
          cli_delete(&cli);
        } else if (key == SDLK_LEFT) {
          cli_left(&cli);
        } else if (key == SDLK_RIGHT) {
          cli_right(&cli);
        } else if (key == SDLK_HOME ||
                   ((mod & KMOD_CTRL) && key == SDLK_a)) {
          cli_home(&cli);
        } else if (key == SDLK_END ||
                   ((mod & KMOD_CTRL) && key == SDLK_e)) {
          cli_end(&cli);
        } else if ((mod & KMOD_CTRL) && key == SDLK_u) {
          cli_clear(&cli);

        } else if (key == SDLK_UP) {
          if (mod & KMOD_CTRL) {
            scroll_y -= SCROLL_STEP;
            clamp_scroll();
          } else {
            cli_history_up(&cli);
          }
        } else if (key == SDLK_DOWN) {
          if (mod & KMOD_CTRL) {
            scroll_y += SCROLL_STEP;
            clamp_scroll();
          } else {
            cli_history_down(&cli);
          }

        } else if ((mod & KMOD_CTRL) && key == SDLK_MINUS) {
          if (db.img_scale > 0.1f) {
            db.img_scale *= 0.5f;
            db_render(&db);
            texture = remake_texture(renderer, texture, window, &db);
            clamp_scroll();
          }
        } else if ((mod & KMOD_CTRL) &&
                   (key == SDLK_EQUALS || key == SDLK_PLUS)) {
          db.img_scale *= 2.0f;
          if (db.img_scale > 4.0f) {
            db.img_scale = 4.0f;
          }
          db_render(&db);
          texture = remake_texture(renderer, texture, window, &db);
          clamp_scroll();
        } else if ((mod & KMOD_CTRL) && key == SDLK_g) {
          show_grid = !show_grid;
        }
      }
    }

    if (stack.edit_pid > 0) {
      int status;
      if (waitpid(stack.edit_pid, &status, WNOHANG) > 0) {
        Cell* img = cell_read_image(stack.edit_path);
        if (img) {
          stack.items[stack.top++] = img;
        }
        stack.edit_pid = 0;
        stack.edit_path[0] = '\0';
        db_sync_stack(&db, stack.items, stack.top);
        texture = remake_texture(renderer, texture, window, &db);
        clamp_scroll();
      }
    }

    render(renderer, texture, &db, &cli);
  }

  SDL_StopTextInput();
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  db_free(&db);
  return 0;
}
