#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define SUPERBLOCK_BLOCK 0
#define INODE_BITMAP_BLOCK 1
#define DATA_BITMAP_BLOCK 2
#define INODE_TABLE_START_BLOCK 3
#define INODE_TABLE_BLOCKS 5
#define DATA_BLOCKS_START 8
#define INODE_SIZE 256
#define INODE_COUNT ((INODE_TABLE_BLOCKS * BLOCK_SIZE) / INODE_SIZE)
#define MAGIC 0xD34D

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_start;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t inode_count;
    uint8_t reserved[4058];
} Superblock;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t file_size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint32_t links_count;
    uint32_t blocks;
    uint32_t direct;
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t reserved[156];
} Inode;
#pragma pack(pop)

uint8_t image[BLOCK_SIZE * TOTAL_BLOCKS];
bool modified = false;

void load_image(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open image");
        exit(1);
    }
    fread(image, 1, BLOCK_SIZE * TOTAL_BLOCKS, file);
    fclose(file);
}

void save_image(const char* filename) {
    if (!modified) return;
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to write image");
        exit(1);
    }
    fwrite(image, 1, BLOCK_SIZE * TOTAL_BLOCKS, file);
    fclose(file);
    printf("Filesystem image updated and saved.\n");
}

Superblock* get_superblock() {
    return (Superblock*)(image + BLOCK_SIZE * SUPERBLOCK_BLOCK);
}

uint8_t* get_inode_bitmap() {
    return image + BLOCK_SIZE * INODE_BITMAP_BLOCK;
}

uint8_t* get_data_bitmap() {
    return image + BLOCK_SIZE * DATA_BITMAP_BLOCK;
}

Inode* get_inode(int index) {
    if (index >= INODE_COUNT) return NULL;
    return (Inode*)(image + BLOCK_SIZE * INODE_TABLE_START_BLOCK + index * INODE_SIZE);
}

void check_and_fix_superblock(Superblock* sb) {
    printf("\n--- Superblock Check ---\n");
    if (sb->magic != MAGIC) { 
        printf("Fixing magic number.\n"); 
        sb->magic = MAGIC; 
        modified = true; 
    }

    if (sb->block_size != BLOCK_SIZE) { 
        printf("Fixing block size.\n"); 
        sb->block_size = BLOCK_SIZE; 
        modified = true;
    }

    if (sb->total_blocks != TOTAL_BLOCKS) { 
        printf("Fixing total blocks.\n"); 
        sb->total_blocks = TOTAL_BLOCKS; 
        modified = true; 
    }

    if (sb->inode_bitmap_block != INODE_BITMAP_BLOCK) { 
        printf("Fixing inode bitmap block.\n"); 
        sb->inode_bitmap_block = INODE_BITMAP_BLOCK; 
        modified = true;
    }

    if (sb->data_bitmap_block != DATA_BITMAP_BLOCK) { 
        printf("Fixing data bitmap block.\n"); 
        sb->data_bitmap_block = DATA_BITMAP_BLOCK; 
        modified = true; 
    }

    if (sb->inode_table_start != INODE_TABLE_START_BLOCK) { 
        printf("Fixing inode table start.\n"); 
        sb->inode_table_start = INODE_TABLE_START_BLOCK;
        modified = true; 
    }

    if (sb->first_data_block != DATA_BLOCKS_START) {
        printf("Fixing first data block.\n"); 
        sb->first_data_block = DATA_BLOCKS_START; 
        modified = true; 
    }

    if (sb->inode_size != INODE_SIZE) { 
        printf("Fixing inode size.\n"); 
        sb->inode_size = INODE_SIZE; 
        modified = true; 
    }

    if (sb->inode_count != INODE_COUNT) { 
        printf("Fixing inode count.\n"); 
        sb->inode_count = INODE_COUNT; 
        modified = true; 
    }
}

void fix_inode_bitmap(uint8_t* inode_bitmap) {
    printf("\n--- Inode Bitmap Check ---\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        Inode* inode = get_inode(i);
        bool valid = inode->links_count > 0 && inode->dtime == 0;
        bool marked = (inode_bitmap[i / 8] >> (i % 8)) & 1;
        if (valid && !marked) {
            printf("Fixing inode %d bitmap (should be set).\n", i);
            inode_bitmap[i / 8] |= (1 << (i % 8));
            modified = true;
        } else if (!valid && marked) {
            printf("Fixing inode %d bitmap (should be cleared).\n", i);
            inode_bitmap[i / 8] &= ~(1 << (i % 8));
            modified = true;
        }
    }
}

void fix_data_bitmap(uint8_t* data_bitmap) {
    printf("\n--- Data Bitmap Check ---\n");
    bool referenced[BLOCK_SIZE * 8] = {false};

    for (int i = 0; i < INODE_COUNT; i++) {
        Inode* inode = get_inode(i);
        if (inode->links_count == 0 || inode->dtime != 0) continue;

        if (inode->direct >= DATA_BLOCKS_START && inode->direct < TOTAL_BLOCKS) {
            int block = inode->direct - DATA_BLOCKS_START;
            referenced[block] = true;
            if (!((data_bitmap[block / 8] >> (block % 8)) & 1)) {
                printf("Fixing data bitmap: block %d used by inode %d.\n", inode->direct, i);
                data_bitmap[block / 8] |= (1 << (block % 8));
                modified = true;
            }
        }
    }

    for (int i = 0; i < TOTAL_BLOCKS - DATA_BLOCKS_START; i++) {
        bool marked = (data_bitmap[i / 8] >> (i % 8)) & 1;
        if (marked && !referenced[i]) {
            printf("Fixing data bitmap: block %d not used.\n", i + DATA_BLOCKS_START);
            data_bitmap[i / 8] &= ~(1 << (i % 8));
            modified = true;
        }
    }
}

void check_duplicate_blocks() {
    printf("\n--- Duplicate Block Check ---\n");
    int block_use_count[TOTAL_BLOCKS] = {0};
    for (int i = 0; i < INODE_COUNT; i++) {
        Inode* inode = get_inode(i);
        if (inode->links_count == 0 || inode->dtime != 0) continue;
        int blk = inode->direct;
        if (blk >= DATA_BLOCKS_START && blk < TOTAL_BLOCKS) {
            block_use_count[blk]++;
            if (block_use_count[blk] > 1) {
                printf("Warning: Duplicate usage of block %d.\n", blk);
            }
        }
    }
}

void check_bad_blocks() {
    printf("\n--- Bad Block Check ---\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        Inode* inode = get_inode(i);
        if (inode->links_count == 0 || inode->dtime != 0) continue;
        if (inode->direct < DATA_BLOCKS_START || inode->direct >= TOTAL_BLOCKS) {
            printf("Fixing bad block in inode %d.\n", i);
            inode->direct = 0;
            modified = true;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <vsfs.img>\n", argv[0]);
        return 1;
    }

    load_image(argv[1]);
    Superblock* sb = get_superblock();

    check_and_fix_superblock(sb);
    fix_inode_bitmap(get_inode_bitmap());
    fix_data_bitmap(get_data_bitmap());
    check_duplicate_blocks();
    check_bad_blocks();

    save_image(argv[1]);
    return 0;
}
