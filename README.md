# fliptable

A key-value database stored as a single PNG image.

Keys are strings. Values are strings, integers, or images.
Text is rendered with a 3x6 bitmap font directly into RGB pixels.
The database file *is* the image; there is no separate format.

An SDL2 window displays the live database while a stdin REPL
accepts commands. Resizing the window reflows the layout.

## Build

    make
    make run        # wraps with rlwrap for history

Requires SDL2 and a C99 compiler.

## Data model

Keys use dot-separated namespaces. The part after the last dot
is the local key; everything before it is the namespace.
Keys without a dot go into the `home` namespace.

    set age 30            -> home.age = 30
    set metrics.cpu 90    -> metrics.cpu = 90
    set docs.math.+ help  -> docs.math.+ = help

Each namespace is a visual row in the image. Rows wrap when
they exceed the window width.

Deleting a namespace deletes all sub-namespaces too:
`del docs` removes `docs`, `docs.math`, `docs.string`, etc.

## Commands

Parentheses are optional for simple commands.
`set a 1` and `(set a 1)` are equivalent.

### Database

    save [name]     Save database to images/name (default: db.png)
    load name       Load database from file

### Cells

    set key value   Set a key to a text or numeric value
    get key         Get the value of a key
    del key         Delete a key, or a whole namespace

### Images

    read path       Load an image file as a cell value
    set k (read f)  Store an image under a key

### Math

    (+ a b ...)     Add
    (- a b ...)     Subtract
    (* a b ...)     Multiply
    (/ a b)         Divide
    (% a b)         Modulo

### Strings

    (cat a b ...)   Concatenate values

### REPL

    quit / exit     Close the program

## Files

    src/main.c      REPL + SDL2 window
    src/db.h/c      Database, cells, rows, PNG I/O
    src/eval.h/c    S-expression parser and evaluator
    src/glyph.h/c   3x6 bitmap font
