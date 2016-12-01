#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size
#define NDIRECT 12

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
};

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

int main(int argc, char *argv[]) {
  // TODO Check argc, argv. Use argv[1]

  int fd = open("fs.img", O_RDONLY);
  assert(fd > -1);

  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  // use mmap()
  void *img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_ptr != MAP_FAILED);

  struct superblock *sb = (struct superblock *) (img_ptr + BSIZE);
  printf("%d %d %d\n", sb->size, sb->nblocks, sb->ninodes);

  int i;
  struct dinode *dip = (struct dinode *) (img_ptr + (2*BSIZE));
  for (i = 0; i < sb->ninodes; i++) {
    printf("%d: type %d\n", i, dip->type);
    dip++;
  }

  // figure out where the bitmap is

  // do other stuff (rest of p5)

  return 0;
}
