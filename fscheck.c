#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

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

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

int main(int argc, char *argv[]) {
  // TODO Check argc, argv. Use argv[1]
  if (argc != 2) {
    fprintf(stderr,"Usage: fscheck file_system_image\n");
    exit(1);
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

  // Structure: unused block | superblock | inode table | bitmap | data blocks
  void *img_indirect_block = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_indirect_block != MAP_FAILED);

  // Superblock
  struct superblock *sb = (struct superblock *) (img_indirect_block + BSIZE);
  printf("%d %d %d\n", sb->size, sb->nblocks, sb->ninodes);

  // Bitmap
  int bitmap_block = sb->ninodes / IPB + 3;
  char *bitmap_buf = (char *)(img_indirect_block + (bitmap_block * BSIZE));

  // Data
  int num_bit_blocks = sb->size / (BSIZE * 8) + 1;  // 8 bits a byte; 1 based indexing
	int data_block = bitmap_block + num_bit_blocks;
    // Set up bitmap of data
  int data_bitmap[sb->size];  // Made large for better indexing later
  int i;
  for (i = data_block; i < sb->size; i++){
    if ((bitmap_buf[i / 8] & (0x1 << (i % 8))) > 0) {
      data_bitmap[i] = 1;
    } else {
      data_bitmap[i] = 0;
    }
  }
  /****************************** INODE CHECKER *******************************/
  int j, k, block_use, bitmap_value, num_dir_entries, curr_entry_inum;
  int num_indirect_blocks, found_root = 0;
  int dir_inodes[sb->ninodes];  // Keep track of inodes that are directories
  int ref_to_parent[sb->ninodes];  // Reference from entry to parent directory
  int ref_back[sb->ninodes];  // Reference back to entry
  int num_refs[sb->ninodes];  // Number of references to each of the inodes

  struct dinode *dip = (struct dinode *) (img_indirect_block + (2*BSIZE));  // First one
  struct dirent *dir_entry;
  for (i = 0; i < sb->ninodes; i++) {
    // Each inode is either unallocated or one of the valid types
    if (dip->type < 0 || dip->type > 3) {
      fprintf(stderr, "bad inode.\n");
      exit(1);
    }

    // For in-use inodes only
    if (dip->type != 0) {
      /**************************** DIRECT BLOCKS *****************************/
      for (j = 0; j <= NDIRECT; j++) {

        /************************ Address use checks **************************/
        // Check if each address that is used by inode is valid
        if (dip->addrs[j] != 0 &&   // || instead of &&?
            (dip->addrs[j] < data_block || dip->addrs[j] > 1023)) {
          fprintf(stderr,"bad address in inode.\n");
          exit(1);
        }

        block_use = dip->addrs[j];
        bitmap_value = data_bitmap[block_use];

        if (block_use > 0) {
          // Increase use count for all checked bits to compare later(mark bitmap)
          data_bitmap[block_use]++;

          // Check that each address in use is also marked in use in the bitmap
          if (bitmap_value == 0) {  // if(dip->addrs[j] > 0 && !bitmap_val)
            fprintf(stderr,"address used by inode but marked free in bitmap.\n");
            exit(1);
          }

          // Check that any address in use is only used once.
          if (data_bitmap[block_use] > 2) {  // 2 since just incremented 1 extra
            fprintf(stderr,"address used more than once.\n");
            exit(1);
          }
        }

        /********************* Directory specific checks **********************/
        if (dip->type == 1) {
          num_dir_entries = BSIZE / sizeof(struct dirent);
          dir_entry = (struct dirent*)(img_indirect_block + (block_use * BSIZE));  //First
          k = 0;

          // Check that each directory contains . and .. as first two entries
          if (j == 0) {
            if (strcmp(dir_entry->name,".") ||
                  strcmp((dir_entry + 1)->name, "..")) {
              fprintf(stderr,"directory not properly formatted.\n");
              exit(1);
            }

            // This inode is a directory
            dir_inodes[dir_entry->inum] = 1;

            // If . and .. found, then root directory present
            if(dir_entry->inum == 1 && (dir_entry + 1)->inum == 1 &&
                 found_root == 0) {
              found_root = 1;
            }
            curr_entry_inum = dir_entry->inum;
            ref_to_parent[curr_entry_inum] = (dir_entry + 1)->inum;

            // Skip over . and .. and continue processing
            dir_entry += 2;
            k += 2;
          }
          // Process indirect blocks later
          if (j < NDIRECT) {
            for ( ; k < num_dir_entries; k++) {
              if (dir_entry->inum != 0){
                ref_back[dir_entry->inum] = curr_entry_inum;
                num_refs[dir_entry->inum]++;
              }
              dir_entry++;
            }
          }
        }
      }

      /*************************** INDIRECT BLOCKS ****************************/
      num_indirect_blocks = BSIZE / sizeof(uint);
      block_use = dip->addrs[NDIRECT];
      uint *indirect_block = (uint*)(img_indirect_block + (block_use * BSIZE));
      for (j = 0; j < num_indirect_blocks; j++) {
        /************************ Address use checks **************************/
        // Check that each address that is used by inode is valid
        if ((*indirect_block < data_block || *indirect_block > 1023) &&
               *indirect_block != 0) {
          fprintf(stderr,"bad address in inode.\n");
          exit(1);
        }

        if (*indirect_block > 0) {
          // Increase use count for all checked bits to compare later(mark bitmap)
          data_bitmap[*indirect_block]++;

          // Check that each address in use is also marked in use in the bitmap
          if (!data_bitmap[*indirect_block]) {
            fprintf(stderr,"address used by inode but marked free in bitmap.\n");
            exit(1);
          }

           ///////////// TODO MOVE TO OUTSIDE OF IF IF FAILS. > 0 COND NOT THERE
          // Check that any address in use is only used once
          if (data_bitmap[*indirect_block] > 2) {  // 2 since just incremented 1 extra
            fprintf(stderr,"address used more than once.\n");
            exit(1);
          }
        }

        /********************* Directory specific checks **********************/
        if (dip->type == 1) {
          num_dir_entries = BSIZE / sizeof(struct dirent);
          dir_entry = (struct dirent *)(img_indirect_block + (*indirect_block * BSIZE));
          for (k = 0; k < num_dir_entries; k++) {
            if (dir_entry->inum != 0) {
              ref_back[dir_entry->inum] = curr_entry_inum;
              num_refs[dir_entry->inum]++;
            }
            dir_entry++;
          }
        }
        indirect_block++;
      }
    dip++;
    }
  }

  // figure out where the bitmap is

  // do other stuff (rest of p)

  return 0;
}
