# fliptable

An image-based programming system, with a Forth-like stack REPL.

![images](./docs/fliptable.png)
![audio](./docs/audio.png)
![geometry](./docs/geometry.png)

State is stored as a single PNG image, making snapshots first-class.
Text is rendered with a 3x6 bitmap font directly into RGB pixels.
This facilitate reconstruction, but should not be considered essential.

## Data model

All variables are a (k,v) pair.
Keys are strings. Values are strings, integers, or images.

Keys use dot-separated namespaces. The part after the last dot
is the local key; everything before it is the namespace.
Keys without a dot go into the `home` namespace.

    30 age SET                -> home.age = 30
    90 metrics.cpu SET        -> metrics.cpu = 90

Each namespace is a visual row in the image.

### Basics

     3 4 + .                  -> 7
    10 3 - .                  -> 7
     6 7 * .                  -> 42
    10 3 / .                  -> 3
    10 3 % .                  -> 1
    
    val key SET               set key to value
    key GET                   push value of key
    @key                      shorthand: fetch and push
    key DEL                   delete key or namespace
    
    DUP                       duplicate top
    DROP                      discard top
    SWAP                      swap top two
    
    a b CAT                   concatenate two values

    EXEC                      pop text, execute as Forth
    .                         pop and print top of stack
    quit / exit               close the program

### Procedures

    "2 *" double SET          store a procedure
    5 double GET EXEC .       -> 10
    5 @double .               -> 10 (shorthand)

### Editing

    val EDIT                  push text/number back to CLI for editing
    img EDIT                  open image in gthumb for editing

For text and numbers, EDIT places the value (quoted) in the input line so
you can modify it before submitting. For images, gthumb opens non-blocking
and the modified image is reloaded onto the stack when closed.

### Media

    path READ                 load image, text file, or audio
    SCREENSHOT                interactive screenshot (via scrot -s)
    img size RESIZE           resize image to size x size pixels
    TIME                      push time (YYYY-MM-DD HH:MM:SS)
    LOCATION                  push city via IP geolocation

Image cells are three rows: key, file path, and pixel data. 

The file path enables re-linking to the source file for image operations like
RESIZE. If an image cell's source file is missing from disk, it is re-created
from the pixels stored in the database image.

### Audio

    "song.mp3" READ           decode audio to PCM-as-pixels
    img PLAY                  play audio image via ffplay
    secs RECORD               record from mic for N seconds

Audio files (.mp3, .wav, .ogg, .flac, .aac, .opus) are decoded via ffmpeg
to 8kHz mono 16-bit PCM. Each sample is stored as one pixel: R = high byte,
G = low byte, B = 0. A 10-second clip produces a 256x313 image.

Since PNG is lossless, audio survives save/load round-trips perfectly.
PLAY unpacks the pixels back to raw PCM and plays via ffplay.
RECORD captures from the default PulseAudio input.

### 3D Geometry

    "model.obj" READ              load OBJ as connectivity matrix image
    tex model RENDER              reconstruct OBJ, apply texture, open in f3d

OBJ files are parsed and encoded into a single image. Vertices with
different UV mappings are duplicated so each vertex has a unique UV.
For N (deduplicated) vertices, the image is N rows by (N+4) columns:

- Columns 0-1: vertex position (X, Y, Z as 16-bit per axis across 2 pixels)
- Columns 2-3: UV texture coordinates (U, V as 16-bit across 2 pixels)
- Columns 4+: NxN adjacency matrix (face connectivity)

Adjacency encodes face triangles: for edge (i,j) where i < j, the pixel
stores R=k1, G=k2 (the third vertex of each triangle sharing that edge),
B=1. White pixels (255,255,255) mean no edge. Coordinates are normalized
to 0-1 range permanently.

RENDER pops a model image and a texture image from the stack. It writes
a temp OBJ with a MTL file referencing the texture, then opens f3d.
Editing the matrix pixels (e.g., with gthumb via EDIT) modifies the geometry.

    "texture.png" READ "cube.obj" READ RENDER

### Utility

    ns pos PIN                move namespace to position (0 = first)

### Persistence

    name SAVE                 save db to images/name
    name LOAD                 load db from file
    
Save writes the database image to `images/`. On load, the cell structure
(rows, keys, values, images) is reconstructed by scanning the pixel data:

- Namespace labels are read from the left column
- Cell keys are decoded from the bitmap font
- Values are detected as text (gray pixels) or image data
- Image cells include a file path header above the pixel data


## Keyboard shortcuts

    Ctrl +/-                  scale images up/down (text stays fixed)
    Arrow keys / scroll       scroll the view vertically
    Ctrl g                    display the bit-map font grid

## Files

    src/main.c         REPL + SDL2 window + keyboard input
    src/db.h/c         Database, cells, rows, PNG I/O, reconstruction
    src/eval.h/c       Forth-style stack evaluator
    src/audio.h/c      Audio PCM-as-pixels encoding/decoding
    src/geometry.h/c   OBJ parsing and connectivity matrix encoding
    src/glyph.h/c      3x6 bitmap font (read and write)
    src/cli.h/c        User input
    deps/              stb_image headers
    install.sh         dependency installer
