#ifndef EVAL_H
#define EVAL_H

#include <sys/types.h>

#include "cli.h"
#include "db.h"

#define MAX_STACK 256

typedef struct
{
  Cell* items[MAX_STACK];
  int top;
  int bang;
} Stack;

void
forth_eval(char* input, Database* db, Stack* stack, Cli* cli);

#endif
