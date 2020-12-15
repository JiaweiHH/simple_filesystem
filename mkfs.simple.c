#include "simplefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static int fd;

int write_to_block(int block_num, void *data, int data_size){
    //写偏移量
	off_t s_out = lseek(fd, block_num * SIMPLE_BLOCK_SIZE, SEEK_SET);
	if(s_out == -1)
		return -1;
	return write(fd, data, data_size);
}

int main(int argc, char *argv[]){
	if(argc != 2){
        printf("没有特定的设备\n");
    }
	fd = open(argv[1], O_RDWR);
	//在第0块写超级块的数据
	struct simple_superblock *sb = malloc(sizeof(struct simple_superblock));
	sb->s_magic = SIMPLE_MAGIC;
	sb->s_blockcount = 1 + 32 + 1024;
	sb->s_inodecount = 1;
	sb->s_blockmap[0] = 1;
	write_to_block(0, sb, sizeof(struct simple_superblock));
	free(sb);

	/*
	---------------------------------------------------
	|sb|      1024 data blocks       | 32 inode blocks|
	---------------------------------------------------
	*/

	//写根 inode 的数据
	struct simple_inode *root = malloc(sizeof(struct simple_inode));

	root->i_ino = SIMPLE_INODE_BLOCK_BASE;
	root->i_type = SIMPLE_FILE_TYPE_DIR;
	write_to_block(SIMPLE_INODE_BLOCK_BASE, root, sizeof(struct simple_inode));
	free(root);

    int	res = close(fd); // close the disk "file"
	if(res == -1)
		printf("关闭文件出错，文件 fd: %d\n", fd);
	
	printf("格式化完成!\n");
	return 0;
}