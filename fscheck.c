#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size
#define NDIRECT 12
// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

// Inode types
#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

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

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

int main(int argc, char *argv[]) {
  // TODO Check argc, argv. Use argv[1]
  if (argc != 2) {

  }

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "image not found.\n");
    exit(1);    
  }

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
    // printf("%d: type %d\n", i, dip->type);
    // Each inode is either unallocated or one of the valid types
    if (dip->type < 0 || dip->type > 4) {
      fprintf(stderr, "bad inode.\n");
      exit(1);
    }
    dip++;
  }

  // figure out where the bitmap is

  // do other stuff (rest of p)

  return 0;
}
