# e4rat-preload-lite

More efficient e4rat file preloader.

## Instructions

1. Change `LIST` and `INIT` defines to your file list and init paths
   respectively.
* Compile file with `clang -O3 e4rat-preload-lite.c -o e4rat-preload-lite`
   or `gcc -O3 -std=c99 e4rat-preload-lite.c -o e4rat-preload-lite`.
* Strip binary: `strip -s ./e4rat-preload-lite`.
* Copy init file to /sbin/e4rat-preload-lite:
  `cp ./e4rat-preload-lite /sbin/`.
* Add `init=/sbin/e4rat-preload-lite` to kernel line in your bootloader
  configuration.

## Changelog

### 0.2

* Load 33% of list but not more than 1000 files before running system process.
* Load only 100 files per iteration.

This version saves 2 more seconds until fully functional gnome-shell comparing
to 0.1.

## Original

Original version of this utility was written by John Lindgren. You can find it
[here](http://e4rat-l.bananarocker.org/).
