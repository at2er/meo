# Meo (Meow editor)
A suck less (not `suckless`) text editor.
Trying to keep simple with functional and convenient as much as possible.

Human-readable and human-modifiable are two important design of meo, like every
[suckless](https://suckless.org) software.

## Building
### Requirements
- [libgrapheme](https://libs.suckless.org/libgrapheme)

## Bugs
### Multi-window
It supports a little bit of multi-window (only on design).
But multi-edition isn't expected function.
> Maybe implement it in the future.

### libgrapheme.so
Try `export LD_LIBRARY_PATH=/usr/local/lib` if your
libgrapheme was installed in `/usr/local`.

### wcwidth()
You need the termux's
[wcwidth](https://github.com/termux/wcwidth)
to implement the f**king `wcwidth()`

# Others
> I am trying to use meo to develop meo now.
> So it's usable! *but still in experimental stage*
