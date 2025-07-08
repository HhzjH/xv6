#include "fs.h"
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  int pinned;  // is this buffer pinned (for mmap)
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

