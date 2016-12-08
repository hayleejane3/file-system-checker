#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/mman.h>

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size
#define NDIRECT 12
// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

#define SB_NINODES 200
#define SB_SIZE 1024

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

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

// Buffers
void *img_indirect_block;  // Image read from file
struct superblock *sb;     // Superblock
char *bitmap_buf;          // Bitmap buffer
int data_block;            // Data block

// Data structures to track usage
// TODO pass around arrays instead of declaring ninodes, size
int dir_inodes[SB_NINODES];  // Keep track of inodes that are directories
int ref_to_parent[SB_NINODES];  // Reference from dir_entry to parent directory
int ref_back[SB_NINODES];  // Reference back to dir_entry
int num_refs[SB_NINODES];  // Number of references to each of the inodes
int num_refs_real[SB_NINODES];  // nlinks if inode used, 1(from type != 0) o/w
int data_bitmap[SB_SIZE];

// Boolean used for root directory check
int found_root = 0;  // Set to 1 when root directory is found

int process_inode(struct dinode *dip){
  // Check that each inode is either unallocated or one of the valid types
  if (dip->type < 0 || dip->type > 3) {
    fprintf(stderr, "ERROR: bad inode.\n");
    exit(1);
  }

  // Don't need to process unallocated inodes
  if(dip->type == 0) {
		return 1;
  }

	int i, j, curr_entry_inum, block_use, bitmap_value;
  struct dirent *dir_entry;
  uint *indirect_block;
  int num_dir_entries = BSIZE / sizeof(struct dirent);
  int num_indirect_blocks = BSIZE / sizeof(uint);

  /****************************** DIRECT BLOCKS *******************************/
	for (i = 0; i <= NDIRECT; i++) {
		if (dip->addrs[i] != 0 &&
        (dip->addrs[i] < data_block || dip->addrs[i] > 1023)) {
			fprintf(stderr, "ERROR: bad address in inode.\n");
			exit(1);
		}

		block_use = dip->addrs[i];
		bitmap_value = data_bitmap[block_use];

		if (block_use > 0) {
      // Increase use count for all checked bits to compare later(mark bitmap)
			data_bitmap[block_use]++;

      // Check that any address in use is only used once.
      if (data_bitmap[block_use] > 2 ) {
  			fprintf(stderr, "ERROR: address used more than once.\n");
  			exit(1);
  		}

      // Check that each address in use is also marked in use in the bitmap
      if (bitmap_value == 0) {
        fprintf(stderr, "ERROR: address used by inode but marked free in ");
        fprintf(stderr, "bitmap.\n");
        exit(1);
  		}
		}

    /*********************** Directory specific checks ************************/
    if (dip->type == 1) {
			//read the 1st data block of the directory to check for . and ..
			dir_entry = (struct dirent*)(img_indirect_block + (block_use * BSIZE));
      j = 0;
			if(i == 0){
        if (strcmp(dir_entry->name,".") ||
              strcmp((dir_entry + 1)->name, "..")) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");
          exit(1);
        }

        // This inode is a directory
        dir_inodes[dir_entry->inum] = 1;

      	curr_entry_inum = dir_entry->inum;  // To be used later
        ref_to_parent[curr_entry_inum] = (dir_entry + 1)->inum;

        // If . and .. found, then root directory has been found
        if(dir_entry->inum == 1 && (dir_entry + 1)->inum == 1 &&
        found_root == 0) {
          found_root = 1;
        }

        // Skip over . and .. and continue processing
        j += 2;
        dir_entry += 2;
			}

      // Process indirect blocks later
			if (i < NDIRECT) {
				for ( ; j < num_dir_entries; j++) {
					if (dir_entry->inum != 0) {
            num_refs[dir_entry->inum]++;
						ref_back[dir_entry->inum] = curr_entry_inum;
					}
          dir_entry++;
				}
			}
		}

	}
  /***************************** INDIRECT BLOCKS ******************************/
	block_use = dip->addrs[NDIRECT];
	indirect_block = (uint*)(img_indirect_block + (block_use * BSIZE));
	for (i = 0; i < num_indirect_blocks; i++) {
		if (*indirect_block != 0 &&
          (*indirect_block < data_block || *indirect_block > 1023)) {
			fprintf(stderr, "ERROR: bad address in inode.\n");
			exit(1);
		}

    // KEEP ORDER OF NEXT 3 IF BLOCKS. FAILS IF CHANGE
    // Check that each address in use is also marked in use in the bitmap
    if (*indirect_block > 0 && data_bitmap[*indirect_block] == 0) {
      fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.");
      fprintf(stderr, "\n");
      exit(1);
    }

    // Increase use count for all checked bits to compare later(mark bitmap)
    if (*indirect_block > 0) {
      data_bitmap[*indirect_block]++;
    }

    // Check that any address in use is only used once
    if (data_bitmap[*indirect_block] > 2) {  // 2 since just incremented 1 extra
      fprintf(stderr, "ERROR: address used more than once.\n");
      exit(1);
    }

    /*********************** Directory specific checks ************************/
		if(dip->type == 1){
      dir_entry =
        (struct dirent*)(img_indirect_block + (*indirect_block * BSIZE));
			for (j = 0; j < num_dir_entries; j++){
        if (dir_entry->inum != 0) {
          num_refs[dir_entry->inum]++;
			  	ref_back[dir_entry->inum] = curr_entry_inum;
        }
        dir_entry++;
			}
		}
    indirect_block++;
	}
	return 0;
}

/**
 * Set up the data bitmap based on the bit value for the data
 */
int setup_data_bitmap() {
  int i;
  for (i = data_block; i < sb->size; i++) {
    if ((bitmap_buf[i / 8] & (0x1 << (i % 8))) > 0) {
      data_bitmap[i] = 1;
    } else {
      data_bitmap[i] = 0;
    }
  }
  return 0;
}

/**
 * Bitmap marking checks
 */
int bitmap_checks() {
  int i;

  // Check that For blocks marked in-use in bitmap, actually is in-use in an
  // inode or indirect block somewhere
  for (i = data_block; i < data_block + sb->nblocks + 1; i++) {
    if (data_bitmap[i] == 1) {  // Value should not be 1 if in use
      fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.");
      fprintf(stderr, "\n");
      exit(1);
    }
  }
  return 0;
}

/**
 * Do checks after processinf the inode block ...
 * IMP: KEEP AFTER THIS ORDER OF TESTS. TESTS FAIL OTHERWISE
 */
int post_processing_checks() {
  // Check bitmap markings
  bitmap_checks();

  int i;
  /************************ Inode usage count checks **************************/
  for (i = 0; i < sb->ninodes; i++) {
    /*********************** Directory specific checks ************************/
    if (dir_inodes[i] == 1) {
      // Check that each directory only appears in one other directory
      if (num_refs[i] > 1 || num_refs_real[i] > 1) {
        fprintf(stderr, "ERROR: directory appears more than once in file ");
        fprintf(stderr, "system.\n");
        exit(1);
      }

      // Check that each .. dir_entry in directory refers to the proper parent
      // inode, and parent inode points back to it
      if (ref_to_parent[i] != ref_back[i]) {
        fprintf(stderr, "ERROR: parent directory mismatch.\n");
        exit(1);
      }
    }

    /************************ Reference count checks **************************/
    // Check that gor inodes marked used in inode table, must be referred to
    // in at least one directory.
    if (num_refs[i] == 0 && num_refs_real[i] > 0) {
      fprintf(stderr, "ERROR: inode marked use but not found in a directory.");
      fprintf(stderr, "\n");
      exit(1);
    } // Check that for inode numbers referred to in a valid directory, actually
      // marked in use in inode table
      else if (num_refs[i] > 0 && num_refs_real[i] == 0) {
      fprintf(stderr, "ERROR: inode referred to in directory but marked free.");
      fprintf(stderr, "\n");
      exit(1);
    }

    /************************* File specific checks ***************************/
    // Check that reference counts (number of links) for regular files match
    // the number of times file is referred to in directories
    if (dir_inodes[i] == 0 && num_refs[i] != num_refs_real[i]) {
      fprintf(stderr, "ERROR: bad reference count for file.\n");
      exit(1);
    }
  }
  return 0;
}

/**
 * Process the inode table
 */
int process_inode_table(){
	int i, j;
  int inode_number = 0;  // Index of inode being processed
  int num_inode_blocks = sb->ninodes / IPB;  // Number of inode blocks

  setup_data_bitmap();

  // Set up links for root directory inode (always at index 1)
	ref_back[ROOTINO] = 1;
	num_refs[ROOTINO] = 1;

  // Process all of the indode blocks
  for (i = 2; i < num_inode_blocks + 2; i++) {
		struct dinode *dip = (struct dinode*)(img_indirect_block + (i * BSIZE));

    // Process each inode of this block
		for (j = 0; j < IPB; j++){
			process_inode(dip);

      // Mark number of actual uses for the inode
      if (dip->type != 0) {
        if (dip->nlink > 1) {
          num_refs_real[inode_number] = dip->nlink;
        } else {
          num_refs_real[inode_number] = 1;
        }
      }

      // Check that root directory exists, and it is inode number 1
      if (inode_number == 1 && found_root == 0) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
      }
			inode_number++;
      dip++;
		}
	}

	return 0;
}

/**
 * Read image from file and set up the superblock, bitmap and data buffers.
 * Call method to process the inode block. Error checking done along the way.
 */
int set_up_buffers(char *filename) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "image not found.\n");
    exit(1);
  }

  int rc;
  struct stat sbuf;
  rc = fstat(fd, &sbuf);
  assert(rc == 0);

  // Structure: unused block | superblock | inode table | bitmap | data blocks
  img_indirect_block = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_indirect_block != MAP_FAILED);

  // Superblock
  sb = (struct superblock*)(img_indirect_block + BSIZE);

  // Bitmap
  uint bitmap_block = sb->ninodes / IPB + 3;
  bitmap_buf = (char *)(img_indirect_block + (bitmap_block * BSIZE));

  // Data
  uint num_bit_blocks = sb->size / (BSIZE * 8) + 1;  // 8 bits/byte; 1 based ind
  data_block = bitmap_block + num_bit_blocks;

  return 0;
}

/**
 * Call methods...
 */
int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Usage: fscheck file_system_image\n");
    exit(1);
  }

  set_up_buffers(argv[1]);

	process_inode_table();

  post_processing_checks();

	return 0;
}
