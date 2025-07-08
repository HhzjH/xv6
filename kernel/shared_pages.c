#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 共享页表项结构
struct shared_page_entry {
  uint dev;           
  uint inum;        
  uint offset;   
  char *pa;
  int refcnt;         // 引用计数
  int initialized;    // 是否已初始化
  struct spinlock lock; 
};

// 全局共享页表
struct {
  struct spinlock lock;
  struct shared_page_entry entries[64]; // 最多64个共享页
} shared_pages;

void
shared_pages_init(void)
{
  initlock(&shared_pages.lock, "shared_pages");
  for(int i = 0; i < 64; i++) {
    initlock(&shared_pages.entries[i].lock, "shared_page_entry");
    shared_pages.entries[i].pa = 0;
    shared_pages.entries[i].refcnt = 0;
    shared_pages.entries[i].initialized = 0;
  }
}

// 查找共享页表项
static struct shared_page_entry*
find_shared_page(uint dev, uint inum, uint offset)
{
  for(int i = 0; i < 64; i++) {
    struct shared_page_entry *entry = &shared_pages.entries[i];
    if(entry->pa != 0 && entry->dev == dev && entry->inum == inum && entry->offset == offset) {
      return entry;
    }
  }
  return 0;
}

// 分配新的共享页表项
static struct shared_page_entry*
alloc_shared_page_entry(void)
{
  for(int i = 0; i < 64; i++) {
    struct shared_page_entry *entry = &shared_pages.entries[i];
    if(entry->pa == 0) {
      return entry;
    }
  }
  return 0;
}

// 获取共享页，如果不存在则创建
char*
get_shared_page(uint dev, uint inum, uint offset)
{
  acquire(&shared_pages.lock);
  
  // 查找是否已存在
  struct shared_page_entry *entry = find_shared_page(dev, inum, offset);
  if(entry) {
    // 已存在，增加引用计数
    acquire(&entry->lock);
    entry->refcnt++;
    char *pa = entry->pa;
    release(&entry->lock);
    release(&shared_pages.lock);
    return pa;
  }
  
  // 不存在，分配新的共享页
  entry = alloc_shared_page_entry();
  if(!entry) {
    release(&shared_pages.lock);
    return 0;
  }
  
  // 分配物理页
  char *pa = kalloc();
  if(!pa) {
    release(&shared_pages.lock);
    return 0;
  }
  
  // 初始化共享页表项
  acquire(&entry->lock);
  entry->dev = dev;
  entry->inum = inum;
  entry->offset = offset;
  entry->pa = pa;
  entry->refcnt = 1;
  entry->initialized = 0;  // 新分配的页需要初始化
  release(&entry->lock);
  
  release(&shared_pages.lock);
  return pa;
}

// 标记共享页已初始化
void
mark_shared_page_initialized(char* pa)
{
  acquire(&shared_pages.lock);
  
  // 查找对应的共享页表项
  struct shared_page_entry *entry = 0;
  for(int i = 0; i < 64; i++) {
    if(shared_pages.entries[i].pa == pa) {
      entry = &shared_pages.entries[i];
      break;
    }
  }
  
  if(entry) {
    acquire(&entry->lock);
    entry->initialized = 1;
    release(&entry->lock);
  }
  
  release(&shared_pages.lock);
}

// 检查共享页是否已初始化
int
is_shared_page_initialized(char* pa)
{
  acquire(&shared_pages.lock);
  
  // 查找对应的共享页表项
  struct shared_page_entry *entry = 0;
  for(int i = 0; i < 64; i++) {
    if(shared_pages.entries[i].pa == pa) {
      entry = &shared_pages.entries[i];
      break;
    }
  }
  
  int initialized = 0;
  if(entry) {
    acquire(&entry->lock);
    initialized = entry->initialized;
    release(&entry->lock);
  }
  
  release(&shared_pages.lock);
  return initialized;
}

// 释放共享页
void
release_shared_page(char* pa)
{
  acquire(&shared_pages.lock);
  
  // 查找对应的共享页表项
  struct shared_page_entry *entry = 0;
  for(int i = 0; i < 64; i++) {
    if(shared_pages.entries[i].pa == pa) {
      entry = &shared_pages.entries[i];
      break;
    }
  }
  
  if(!entry) {
    release(&shared_pages.lock);
    return;
  }
  
  // 减少引用计数
  acquire(&entry->lock);
  entry->refcnt--;
  
  if(entry->refcnt == 0) {
    // 引用计数为0，释放物理页和表项
    kfree(entry->pa);
    entry->pa = 0;
    entry->dev = 0;
    entry->inum = 0;
    entry->offset = 0;
    entry->initialized = 0;
  }
  
  release(&entry->lock);
  release(&shared_pages.lock);
} 