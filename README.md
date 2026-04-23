# fliptable

A key-value database stored as a single PNG image.

Keys are strings. Values are strings, integers, or images.
Text is rendered with a 3x6 bitmap font directly into RGB pixels.
The database file *is* the image; there is no separate format.

An SDL2 window displays the live database while a stdin REPL
accepts commands. The evaluator is a Forth-style stack machine.

## Build

    make
    make run        # wraps with rlwrap for history

Requires SDL2 and a C99 compiler.

## Data model

Keys use dot-separated namespaces. The part after the last dot
is the local key; everything before it is the namespace.
Keys without a dot go into the `home` namespace.

    30 age SET                -> home.age = 30
    90 metrics.cpu SET        -> metrics.cpu = 90

Each namespace is a visual row in the image. Rows wrap when
they exceed the window width.

Deleting a namespace deletes all sub-namespaces too:
`docs DEL` removes `docs`, `docs.math`, `docs.string`, etc.

## Language

Forth-style postfix. Words are whitespace-separated.
Values are pushed onto a stack; commands pop their arguments.

    42                        push number
    "hello world"             push quoted string
    age                       push literal "age"

Use `.` to print the top of the stack.

### Database

    name SAVE                 save db to images/name (default: db.png)
    name LOAD                 load db from file

### Cells

    val key SET               set key to value
    key GET                   push value of key
    key DEL                   delete key or namespace

### Images

    path READ                 load image or text file (.md, .txt)
    path READ key SET         store image under key
    SCREENSHOT key SET        interactive screenshot, store it

### Arithmetic

    3 4 + .                   -> 7
    10 3 - .                  -> 7
    6 7 * .                   -> 42
    10 3 / .                  -> 3
    10 3 % .                  -> 1

### Strings

    a b CAT                   concatenate two values

### Stack

    DUP                       duplicate top
    DROP                      discard top
    SWAP                      swap top two

### Procedures

    "2 *" double SET          store a procedure
    5 double GET EXEC .       -> 10

### REPL

    quit / exit               close the program

The stack persists across lines, so `5` on one line
then `age SET` on the next works.

## Files

    src/main.c      REPL + SDL2 window
    src/db.h/c      Database, cells, rows, PNG I/O
    src/eval.h/c    Forth-style stack evaluator
    src/glyph.h/c   3x6 bitmap font
