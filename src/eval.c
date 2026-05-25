#define _POSIX_C_SOURCE 200809L
#include "eval.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "cli.h"
#include "geometry.h"
#include "stb_image_write.h"


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
forth_eval(char* input, Database* db, Stack* stack, Cli* cli)
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

    int bang = 0;
    int toklen = strlen(tok);
    if (toklen > 1 && tok[toklen - 1] == '!') {
      tok[toklen - 1] = '\0';
      bang = 1;
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
      if (strcmp(key->value, "stack") == 0) {
        for (int si = 0; si < stack->top; si++) {
          cell_free_temp(stack->items[si]);
        }
        stack->top = 0;
      }
      db_del(db, key->value);
      cell_free_temp(key);

    } else if (strcasecmp(tok, "PIN") == 0) {
      Cell* pos = pop(stack);
      Cell* ns = pop(stack);
      db_move_row(db, ns->value, cell_to_num(pos));
      cell_free_temp(ns);
      cell_free_temp(pos);

    } else if (strcasecmp(tok, "SAVE") == 0) {
      Cell* name = pop(stack);
      db_save(db, name->value[0] ? name->value : NULL);
      cell_free_temp(name);

    } else if (strcasecmp(tok, "LOAD") == 0) {
      Cell* name = pop(stack);
      db_load(db, name->value);
      cell_free_temp(name);
      db_load_commands(db);
      for (int si = 0; si < stack->top; si++) {
        cell_free_temp(stack->items[si]);
      }
      stack->top = db_load_stack(db, stack->items, MAX_STACK);

    } else if (strcasecmp(tok, "READ") == 0) {
      Cell* path = pop(stack);
      Cell* img = cell_read_image(path->value);
      cell_free_temp(path);
      push(stack, img ? img : cell_make_nil());

    } else if (strcasecmp(tok, "SPAWN") == 0) {
      Cell* cmd = pop(stack);
      if (cmd->type != VAL_COMMAND) {
        push(stack, cmd);
        continue;
      }
      int arity = cmd->args;
      char* argv_strs[16];
      memset(argv_strs, 0, sizeof(argv_strs));
      for (int ai = arity - 1; ai >= 0 && ai < 16; ai--) {
        Cell* arg = pop(stack);
        if (arg->type == VAL_IMAGE && arg->img_data) {
          char tmp_png[256], tmp_raw[256];
          snprintf(tmp_png, sizeof(tmp_png),
                   "/tmp/aguafuerte_arg%d.png", ai);
          snprintf(tmp_raw, sizeof(tmp_raw),
                   "/tmp/aguafuerte_arg%d.raw", ai);
          stbi_write_png(tmp_png, arg->img_width, arg->img_height,
                         3, arg->img_data, arg->img_width * 3);
          cell_write_pcm(arg, tmp_raw);
          int vlen = strlen(arg->value);
          if (vlen > 8 && strcmp(arg->value + vlen - 8, ".obj.png") == 0) {
            char tmp_obj[256];
            snprintf(tmp_obj, sizeof(tmp_obj),
                     "/tmp/aguafuerte_arg%d.obj", ai);
            cell_write_obj(arg, tmp_obj, NULL);
            argv_strs[ai] = strdup(tmp_obj);
          } else {
            argv_strs[ai] = strdup(tmp_png);
          }
        } else {
          argv_strs[ai] = strdup(arg->value);
        }
        cell_free_temp(arg);
      }

      char script_path[256];
      snprintf(script_path, sizeof(script_path), "%s", cmd->value);

      int pipefd[2];
      if (pipe(pipefd) < 0) {
        cell_free_temp(cmd);
        continue;
      }

      pid_t pid = fork();
      if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char* exec_argv[20];
        int ei = 0;
        exec_argv[ei++] = "bash";
        char resolved_script[512];
        if (resolve_path(script_path, resolved_script,
                         sizeof(resolved_script))) {
          exec_argv[ei++] = resolved_script;
        } else {
          exec_argv[ei++] = script_path;
        }
        for (int ai = 0; ai < arity && ei < 18; ai++) {
          exec_argv[ei++] = argv_strs[ai];
        }
        exec_argv[ei] = NULL;
        execvp("bash", exec_argv);
        _exit(1);
      } else if (pid > 0) {
        close(pipefd[1]);
        char* cmd_name = cmd->key[0] ? cmd->key : "cmd";
        Cell* proc = cell_make_process(cmd_name, pid);
        proc->num = pipefd[0];
        push(stack, proc);
      }
      for (int ai = 0; ai < arity; ai++) {
        free(argv_strs[ai]);
      }
      cell_free_temp(cmd);

    } else if (strcasecmp(tok, "WAIT") == 0) {
      Cell* proc = pop(stack);
      if (proc->type != VAL_PROCESS) {
        push(stack, proc);
        continue;
      }

      if (proc->num > 0) {
        char output[256] = { 0 };
        int n = read(proc->num, output, sizeof(output) - 1);
        if (n > 0) {
          output[n] = '\0';
          while (n > 0 && (output[n-1] == '\n' || output[n-1] == '\r')) {
            output[--n] = '\0';
          }
        }
        close(proc->num);
        proc->num = 0;
        int status;
        waitpid((pid_t)proc->args, &status, 0);
        char* tab = strchr(proc->value, '\t');
        char cmd_name[MAX_KEY] = "";
        if (tab) {
          strncpy(cmd_name, tab + 1, MAX_KEY - 1);
        }
        if (output[0]) {
          snprintf(proc->value, MAX_KEY, "done:%s\t%s", output, cmd_name);
        } else {
          snprintf(proc->value, MAX_KEY, "done\t%s", cmd_name);
        }
      }

      char* out = NULL;
      if (strncmp(proc->value, "done:", 5) == 0) {
        out = proc->value + 5;
        char* tab = strchr(out, '\t');
        if (tab) *tab = '\0';
      }
      if (out && out[0]) {
        Cell* result = cell_read_image(out);
        if (result) {
          push(stack, result);
        }
      }
      cell_free_temp(proc);

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
      forth_eval(buf, db, stack, cli);

    } else if (strcasecmp(tok, "LOCATION") == 0) {
      char buf[128] = { 0 };
      FILE* fp = popen("curl -s ipinfo.io/city 2>/dev/null", "r");
      if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
          int len = strlen(buf);
          if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
          }
        }
        pclose(fp);
      }
      push(stack, buf[0] ? cell_make_text(buf) : cell_make_nil());

    } else if (strcasecmp(tok, "TIME") == 0) {
      time_t now = time(NULL);
      struct tm* t = localtime(&now);
      char buf[20];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
      push(stack, cell_make_text(buf));

    } else if (strcasecmp(tok, "EDIT") == 0) {
      Cell* a = pop(stack);
      if ((a->type == VAL_TEXT || a->type == VAL_NUM) && cli) {
        char quoted[MAX_INPUT];
        snprintf(quoted, sizeof(quoted), "\"%s\"", a->value);
        cli_set(cli, quoted);
      } else {
        push(stack, a);
        a = NULL;
      }
      if (a) {
        cell_free_temp(a);
      }

    } else if (strcmp(tok, ".") == 0) {
      Cell* a = pop(stack);
      if (a->type != VAL_NIL && a->value[0]) {
        printf("%s\n", a->value);
      }
      cell_free_temp(a);

    } else {
      char lower[MAX_KEY];
      int li = 0;
      for (int ci = 0; tok[ci] && li < MAX_KEY - 1; ci++) {
        lower[li++] = tolower((unsigned char)tok[ci]);
      }
      lower[li] = '\0';
      char sys_key[MAX_KEY + 8];
      snprintf(sys_key, sizeof(sys_key), "sys.%s", lower);
      Cell* sys = db_get_cell(db, sys_key);
      if (sys && sys->type == VAL_COMMAND) {
        push(stack, cell_copy(sys));
        if (bang) {
          forth_eval("spawn", db, stack, cli);
          stack->bang = 1;
        }
      } else {
        if (bang) {
          tok[strlen(tok)] = '!';
        }
        push(stack, cell_make_text(tok));
      }
    }
  }
}
