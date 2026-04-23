#include "eval.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void
push(Stack* s, Cell* c)
{
  if (s->top < MAX_STACK) {
    s->items[s->top++] = c;
  }
}

static Cell*
pop(Stack* s)
{
  if (s->top > 0) {
    return s->items[--s->top];
  }
  return cell_make_nil();
}

static char*
next_token(char** cursor, char* buf, int bufsize)
{
  char* c = *cursor;
  while (*c && isspace(*c)) {
    c++;
  }
  if (!*c) {
    return NULL;
  }

  int i = 0;

  if (*c == '"') {
    c++;
    while (*c && *c != '"') {
      if (i < bufsize - 1) {
        buf[i++] = *c;
      }
      c++;
    }
    if (*c == '"') {
      c++;
    }
  } else {
    while (*c && !isspace(*c)) {
      if (i < bufsize - 1) {
        buf[i++] = *c;
      }
      c++;
    }
  }

  buf[i] = '\0';
  *cursor = c;
  return buf;
}

static int
is_number(char* s, long* out)
{
  char* end;
  *out = strtol(s, &end, 10);
  return *end == '\0' && end != s;
}

void
forth_eval(char* input, Database* db, Stack* stack)
{
  char* cursor = input;
  char tok[MAX_KEY];

  while (next_token(&cursor, tok, sizeof(tok))) {
    long num;

    if (is_number(tok, &num)) {
      push(stack, cell_make_num((int)num));
      continue;
    }

    if (tok[0] == '@' && tok[1]) {
      Cell* found = db_get_cell(db, tok + 1);
      push(stack, found ? cell_copy(found) : cell_make_nil());
      continue;
    }

    if (strcasecmp(tok, "SET") == 0) {
      Cell* key = pop(stack);
      Cell* val = pop(stack);
      db_set_cell(db, key->value, val);
      cell_free_temp(key);
      cell_free_temp(val);

    } else if (strcasecmp(tok, "GET") == 0) {
      Cell* key = pop(stack);
      Cell* found = db_get_cell(db, key->value);
      cell_free_temp(key);
      push(stack, found ? cell_copy(found) : cell_make_nil());

    } else if (strcasecmp(tok, "DEL") == 0) {
      Cell* key = pop(stack);
      db_del(db, key->value);
      cell_free_temp(key);

    } else if (strcasecmp(tok, "SAVE") == 0) {
      Cell* name = pop(stack);
      db_save(db, name->value[0] ? name->value : NULL);
      cell_free_temp(name);

    } else if (strcasecmp(tok, "LOAD") == 0) {
      Cell* name = pop(stack);
      db_load(db, name->value);
      cell_free_temp(name);

    } else if (strcasecmp(tok, "READ") == 0) {
      Cell* path = pop(stack);
      Cell* img = cell_read_image(path->value);
      cell_free_temp(path);
      push(stack, img ? img : cell_make_nil());

    } else if (strcasecmp(tok, "SCREENSHOT") == 0) {
      char path[256];
      snprintf(path, sizeof(path),
               "images/scrot_%ld.png", (long)time(NULL));
      char cmd[512];
      snprintf(cmd, sizeof(cmd), "scrot -s %s", path);
      if (system(cmd) != 0) {
        fprintf(stderr, "scrot failed\n");
      }
      Cell* img = cell_read_image(path);
      push(stack, img ? img : cell_make_nil());

    } else if (strcasecmp(tok, "RESIZE") == 0) {
      Cell* size = pop(stack);
      Cell* img = pop(stack);
      int sz = cell_to_num(size);
      if (img->type == VAL_IMAGE && img->value[0] && sz > 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "convert %s -resize %dx%d %s",
                 img->value, sz, sz, img->value);
        if (system(cmd) != 0) {
          fprintf(stderr, "convert failed\n");
        }
        Cell* reloaded = cell_read_image(img->value);
        cell_free_temp(img);
        push(stack, reloaded ? reloaded : cell_make_nil());
      } else {
        push(stack, img);
      }
      cell_free_temp(size);

    } else if (strcmp(tok, "+") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      int r = cell_to_num(a) + cell_to_num(b);
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_num(r));

    } else if (strcmp(tok, "-") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      int r = cell_to_num(a) - cell_to_num(b);
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_num(r));

    } else if (strcmp(tok, "*") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      int r = cell_to_num(a) * cell_to_num(b);
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_num(r));

    } else if (strcmp(tok, "/") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      int d = cell_to_num(b);
      int r = d == 0 ? 0 : cell_to_num(a) / d;
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_num(r));

    } else if (strcmp(tok, "%") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      int d = cell_to_num(b);
      int r = d == 0 ? 0 : cell_to_num(a) % d;
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_num(r));

    } else if (strcasecmp(tok, "CAT") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      char buf[MAX_KEY] = { 0 };
      int len = snprintf(buf, MAX_KEY, "%s", a->value);
      if (len < MAX_KEY) {
        snprintf(buf + len, MAX_KEY - len, "%s", b->value);
      }
      cell_free_temp(a);
      cell_free_temp(b);
      push(stack, cell_make_text(buf));

    } else if (strcasecmp(tok, "DUP") == 0) {
      Cell* a = pop(stack);
      push(stack, a);
      push(stack, cell_copy(a));

    } else if (strcasecmp(tok, "DROP") == 0) {
      cell_free_temp(pop(stack));

    } else if (strcasecmp(tok, "SWAP") == 0) {
      Cell* b = pop(stack);
      Cell* a = pop(stack);
      push(stack, b);
      push(stack, a);

    } else if (strcasecmp(tok, "EXEC") == 0) {
      Cell* code = pop(stack);
      char buf[MAX_KEY];
      snprintf(buf, MAX_KEY, "%s", code->value);
      cell_free_temp(code);
      forth_eval(buf, db, stack);

    } else if (strcmp(tok, ".") == 0) {
      Cell* a = pop(stack);
      if (a->type != VAL_NIL && a->value[0]) {
        printf("%s\n", a->value);
      }
      cell_free_temp(a);

    } else {
      push(stack, cell_make_text(tok));
    }
  }
}
