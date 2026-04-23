Follow the style guide at: `./Style.md`

The idea is an image database.
Concretely, the database is an image, meaning a PNG!
It stores and retrieves data.

We think of this as key/value pairs.
k-v pairs are stored visually in an image.
We use a terse bitmap font (5x5) and store the (k,v) as a 1x2 column.
key is always a string
value is either: string, byte data, or an image.

Each subsequent item stored gets added to the image.
We need the 1x2 pieces to have consistent height and width for easy retrieval.
This is easy for height: 2 x (bit-font height + padding)

Beyond the core data, there is also a database header which contains:
The creation timestamp of the database (as bit-map font)
Bloom-filters for fast O(1) existence lookup
Error-correction codes

Beyond the k,v pair, we store additional data using steganography.
This is done in the key field.
Since the bit-font is binary, only the most significant bit matters. 
All further bits can be modified to encode data.
We encode metadata in binary, and hide it inside of the image.
E.g. created at, modified at (offset by the db's create time)
Opting for RGB pixels, we can triple the metadata size.

The database is compressed only via PNG-style compresion.

I want to write this in C, probably a very minimal C99 variant.
I want to mainly interact with this through a REPL in the CLI, but that also
shows the current image in a window. Probably done trivially with libsdl2.

REPL commands are:
> SET key value
> GET key
> DEL key

Beyond that, we expose a small stack based language with integer, string, and
image operations:

Integer: ADD, SUB, MUL, DIV, MOD
String:  CAT, SUB, 
Image:   CRP, RSZ, CAT

A string cell can be interpreted as a command using EXEC, e.g.
> 10 age SET
> "1 ADD" prog1 SET
> age GET prog1 EXEC output SET
> output GET -> 11


Namespaces generate new rows, both logical and spatial groupings.
Namespaces are just prefixes on keys using "." as delimiter.

90 metrics.cpu SET
10 scores.math SET
metrics. DUP LEN SUM DIV

[header:  bloom filter, index, timestamp ]
[home:    age  | name | city             ]
[metrics: cpu  | mem  | disk  | net      ]
[scores:  math | eng  | hist             ]
