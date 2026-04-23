#ifndef EVAL_H
#define EVAL_H

#include "db.h"

#define MAX_STACK 256

typedef struct
{
  Cell* items[MAX_STACK];
  int top;
} Stack;

void
forth_eval(char* input, Database* db, Stack* stack);

#endif
