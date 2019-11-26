#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"

//we should only modify this file

#define MAX_SIZE 34376597504

/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block) {
    int phy_block_nr = 0;

    assert(log_block_nr >= 0);
    if (log_block_nr < NR_DIRECT_BLOCKS) {
        phy_block_nr = (int) in->in.i_block_nr[log_block_nr]; //read within first 10 data blocks
    } else {
        log_block_nr -= NR_DIRECT_BLOCKS;

        if (log_block_nr >= NR_INDIRECT_BLOCKS) {//super large case
            //TBD(); 
            if (in->in.i_dindirect > 0) {
                log_block_nr -= NR_INDIRECT_BLOCKS; //now holds the 
                int indirect_nr = log_block_nr / NR_INDIRECT_BLOCKS;
                int indirect_ix = log_block_nr % NR_INDIRECT_BLOCKS;
                if (indirect_nr >= NR_INDIRECT_BLOCKS) {
                    return -EFBIG;
                }
                read_blocks(in->sb, block, in->in.i_dindirect, 1);
                indirect_nr = ((int *) block)[indirect_nr]; //physical nr
                if (indirect_nr > 0) {
                    read_blocks(in->sb, block, indirect_nr, 1);
                    phy_block_nr = ((int *) block)[indirect_ix];
                }
            }
        } else if (in->in.i_indirect > 0) {
            read_blocks(in->sb, block, in->in.i_indirect, 1); //now "block" will hold the indirect block
            phy_block_nr = ((int *) block)[log_block_nr];
        }

    }
    if (phy_block_nr > 0) {
        read_blocks(in->sb, block, phy_block_nr, 1);
    } else {
        /* we support sparse files by zeroing out a block that is not
         * allocated on disk. */
        bzero(block, BLOCK_SIZE);
    }
    return phy_block_nr;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size) {
    char block[BLOCK_SIZE];
    long block_nr = start / BLOCK_SIZE; // logical block number in the file
    long block_ix = start % BLOCK_SIZE; //  index or offset in the block
    int ret;

    assert(buf);
    if (start + (off_t) size > in->in.i_size) {//reading exceeds boundary
        size = in->in.i_size - start;
    }
    if (block_ix + size > BLOCK_SIZE) {//larger than one block
        //TBD();
        long remaining_size = size;
        long current_pos = 0; //current buf position
        long current_read_size;
        while (remaining_size > 0) {

            if (block_ix > 0) {//first block
                if ((ret = testfs_read_block(in, block_nr, block)) < 0) {
                    return ret;
                }
                current_read_size = BLOCK_SIZE - block_ix; //first not full block
                memcpy(buf + current_pos, block + block_ix, current_read_size);
                block_nr++;
                remaining_size -= current_read_size;
                current_pos += current_read_size;
                block_ix = 0;
            } else {
                assert(block_ix == 0);
                if (remaining_size < BLOCK_SIZE) {//reaching end
                    current_read_size = remaining_size;
                } else if (remaining_size >= BLOCK_SIZE) {
                    current_read_size = BLOCK_SIZE;
                }

                if ((ret = testfs_read_block(in, block_nr, block)) < 0) {
                    return ret;
                }

                memcpy(buf + current_pos, block + block_ix, current_read_size);
                block_nr++;
                remaining_size -= current_read_size;
                current_pos += current_read_size;

            }
        }

    } else {
        if ((ret = testfs_read_block(in, block_nr, block)) < 0)
            return ret;
        memcpy(buf, block + block_ix, size);

    }
    /* return the number of bytes read or any error */
    return size;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block) {
    int phy_block_nr;
    char indirect[BLOCK_SIZE];
    char dindirect[BLOCK_SIZE];
    int indirect_allocated = 0;
    int dindirect_allocated = 0;

    assert(log_block_nr >= 0);
    phy_block_nr = testfs_read_block(in, log_block_nr, block);

    /* phy_block_nr > 0: block exists, so we don't need to allocate it, 
       phy_block_nr < 0: some error */
    if (phy_block_nr != 0)
        return phy_block_nr;

    /* allocate a direct block */
    if (log_block_nr < NR_DIRECT_BLOCKS) {
        assert(in->in.i_block_nr[log_block_nr] == 0);
        phy_block_nr = testfs_alloc_block_for_inode(in);
        if (phy_block_nr >= 0) {
            in->in.i_block_nr[log_block_nr] = phy_block_nr;
        }
        return phy_block_nr;
    }


    log_block_nr -= NR_DIRECT_BLOCKS;
    if (log_block_nr >= NR_INDIRECT_BLOCKS) {
        //TBD();

        log_block_nr -= NR_INDIRECT_BLOCKS;
        int indirect_nr = log_block_nr / NR_INDIRECT_BLOCKS;
        if (indirect_nr >= NR_INDIRECT_BLOCKS) {
            return -EFBIG;
        }
        int indirect_ix = log_block_nr % NR_INDIRECT_BLOCKS;
        if (in->in.i_dindirect == 0) {
            bzero(dindirect, BLOCK_SIZE);
            phy_block_nr = testfs_alloc_block_for_inode(in);
            if (phy_block_nr < 0)
                return phy_block_nr;
            dindirect_allocated = 1;
            in->in.i_dindirect = phy_block_nr;
        } else {
            read_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
        }

        if (((int*) dindirect)[indirect_nr] == 0) {
            /*allocate indirect block*/
            bzero(indirect, BLOCK_SIZE);
            phy_block_nr = testfs_alloc_block_for_inode(in);
            if (phy_block_nr < 0) {
                if (dindirect_allocated) {
                    testfs_free_block_from_inode(in, in->in.i_dindirect);
                    in->in.i_dindirect = 0;
                }
                return phy_block_nr;
            }
            indirect_allocated = 1;
            ((int*) dindirect)[indirect_nr] = phy_block_nr;
        } else {
            read_blocks(in->sb, indirect, ((int*) dindirect)[indirect_nr], 1);
        }

        /* allocate direct block */
        assert(((int *) indirect)[indirect_ix] == 0);
        phy_block_nr = testfs_alloc_block_for_inode(in);

        if (phy_block_nr >= 0) {
            ((int *) indirect)[indirect_ix] = phy_block_nr;
            write_blocks(in->sb, indirect, ((int*) dindirect)[indirect_nr], 1);
            write_blocks(in->sb, dindirect, in->in.i_dindirect, 1);
        } else {
            if (indirect_allocated) {
                testfs_free_block_from_inode(in, ((int*) dindirect)[indirect_nr]);
                ((int*) dindirect)[indirect_nr] = 0;
            }
            if (dindirect_allocated) {
                testfs_free_block_from_inode(in, in->in.i_dindirect);
                in->in.i_dindirect = 0;
            }
        }

    } else {
        if (in->in.i_indirect == 0) { /* allocate an indirect block */
            bzero(indirect, BLOCK_SIZE);
            phy_block_nr = testfs_alloc_block_for_inode(in);
            if (phy_block_nr < 0)
                return phy_block_nr;
            indirect_allocated = 1;
            in->in.i_indirect = phy_block_nr;
        } else { /* read indirect block */
            read_blocks(in->sb, indirect, in->in.i_indirect, 1);
        }

        /* allocate direct block */
        assert(((int *) indirect)[log_block_nr] == 0);
        phy_block_nr = testfs_alloc_block_for_inode(in);

        if (phy_block_nr >= 0) {
            /* update indirect block */
            ((int *) indirect)[log_block_nr] = phy_block_nr;
            write_blocks(in->sb, indirect, in->in.i_indirect, 1);
        } else if (indirect_allocated) {
            /* there was an error while allocating the direct block, 
             * free the indirect block that was previously allocated */
            testfs_free_block_from_inode(in, in->in.i_indirect);
            in->in.i_indirect = 0;
        }
    }
    return phy_block_nr;

}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size) {
    char block[BLOCK_SIZE];
    long block_nr = start / BLOCK_SIZE; // logical block number in the file
    long block_ix = start % BLOCK_SIZE; //  index or offset in the block
    int ret;
    off_t current_pos = start;
    if (block_ix + size > BLOCK_SIZE) {
        //TBD();
        long remaining_size = size;
        long writing_pos = 0;
        long writing_size;
        while (remaining_size > 0) {
            if (block_ix > 0) {
                ret = testfs_allocate_block(in, block_nr, block);
                if (ret < 0) {
                    return ret;
                }



                writing_size = BLOCK_SIZE - block_ix;
                memcpy(block + block_ix, buf + writing_pos, writing_size);
                write_blocks(in->sb, block, ret, 1);
                writing_pos += writing_size;
                block_nr++;
                remaining_size -= writing_size;
                block_ix = 0;

                if (writing_size > 0)
                    in->in.i_size = MAX(in->in.i_size, current_pos + (off_t) writing_size);
                current_pos += (off_t) writing_size;

            } else {
                assert(block_ix == 0);
                if (remaining_size < BLOCK_SIZE) {
                    writing_size = remaining_size;
                } else if (remaining_size >= BLOCK_SIZE) {
                    writing_size = BLOCK_SIZE;
                }
                ret = testfs_allocate_block(in, block_nr, block);
                if (ret < 0) {
                    return ret;
                }

                memcpy(block + block_ix, buf + writing_pos, writing_size);
                write_blocks(in->sb, block, ret, 1);
                writing_pos += writing_size;
                block_nr++;
                remaining_size -= writing_size;
                if (writing_size > 0)
                    in->in.i_size = MAX(in->in.i_size, current_pos + (off_t) writing_size);
                current_pos += (off_t) writing_size;
            }
        }

    }/* ret is the newly allocated physical block number */
    else {
        ret = testfs_allocate_block(in, block_nr, block);
        if (ret < 0)
            return ret;
        memcpy(block + block_ix, buf, size);
        write_blocks(in->sb, block, ret, 1);

        /* increment i_size by the number of bytes written. */
        if (size > 0)
            in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
        
        /* return the number of bytes written or any error */
    }
    in->i_flags |= I_FLAGS_DIRTY;
    return size;
}

int
testfs_free_blocks(struct inode *in) {
    int i;
    int e_block_nr;

    /* last logical block number */
    e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

    /* remove direct blocks */
    for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
        if (in->in.i_block_nr[i] == 0)
            continue;
        testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
        in->in.i_block_nr[i] = 0;
    }
    e_block_nr -= NR_DIRECT_BLOCKS;

    /* remove indirect blocks */
    if (in->in.i_indirect > 0) {
        char block[BLOCK_SIZE];
        assert(e_block_nr > 0);
        read_blocks(in->sb, block, in->in.i_indirect, 1);
        for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
            if (((int *) block)[i] == 0)
                continue;
            testfs_free_block_from_inode(in, ((int *) block)[i]);
            ((int *) block)[i] = 0;
        }
        testfs_free_block_from_inode(in, in->in.i_indirect);
        in->in.i_indirect = 0;
    }

    e_block_nr -= NR_INDIRECT_BLOCKS;
    /* handle double indirect blocks */
    if (e_block_nr > 0) {
        //TBD();
        assert(in->in.i_dindirect > 0);
        char block[BLOCK_SIZE];
        read_blocks(in->sb, block, in->in.i_dindirect, 1);
        for (i = 0; i < NR_INDIRECT_BLOCKS; i++) {
            char tmp_block[BLOCK_SIZE];
            if (((int *) block)[i] == 0)
                continue;
            read_blocks(in->sb, tmp_block, ((int *) block)[i], 1);
            for (int j = 0; j < NR_INDIRECT_BLOCKS; j++) {
                if (((int *) tmp_block)[j] == 0)
                    continue;
                testfs_free_block_from_inode(in, ((int *) tmp_block)[j]);
            }
            testfs_free_block_from_inode(in, ((int *) block)[i]);
        }
        testfs_free_block_from_inode(in, in->in.i_dindirect);
        in->in.i_dindirect = 0;
    }

    in->in.i_size = 0;
    in->i_flags |= I_FLAGS_DIRTY;
    return 0;
}
