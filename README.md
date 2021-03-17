
# libpcrt

libpcrt is a lightweight library that implements POSIX threads' interface with pure user-level multithreading supported by [the `setcontext` family](https://en.wikipedia.org/wiki/Setcontext). By preloading this library, applications could benefit from user-level thread switching without any modification.

## Building

A single `make` is adequate. Build options are defined in `common.h`:

- `DEBUG`: undefine it to turn off logging.

## Usage

```bash
LD_PRELOAD=/path/to/libpcrt.so [original command]
```