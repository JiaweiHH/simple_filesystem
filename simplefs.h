/*
 * 磁盘存储数据结构定义
 * block 0: 超级块
 * block 1～1023 数据块
 * block 1024 开始 32个inode块
 * 
 * 目前是固定的，后期可以改成用位图来查找
 */

#define SIMPLE_BLOCK_SIZE 4096   //块大小为512B
#define SIMPLE_MAGIC 123456     //魔幻数
#define SIMPLE_SUPER_BLOCK_NO 0
#define SIMPLE_INODE_BLOCK_BASE 1024
#define SIMPLE_DATA_BLOCK_BASE 1
#define SIMPLE_DATA_BLOCK_COUNT 1023
#define SIMPLE_FILENAME_MAX 128
#define SIMPLE_PER_FILE_BLOCK 8
#define SIMPLE_FILE_TYPE_DIR 1
#define SIMPLE_FILE_TYPE_FILE 2

#define CURRENT_TIME (current_kernel_time())

struct simple_superblock{
    int s_magic;  //魔幻数
    int s_inodecount;  //使用过得inode数
    int s_blockcount;  //总块数
    //todo 后期可以添加数据块位图和inode块位图
    unsigned short s_blockmap[SIMPLE_DATA_BLOCK_COUNT];  //数据块使用位图
};

struct simple_inode{
    int i_ino;  //inode编号
    int i_type;  //文件类型，目录或普通文件
    int dir_child_count;  //节点下的文件数量
    int file_size;  //文件大小，目录只有一个块大小
    // unsigned short i_blockmap[SIMPLE_PER_FILE_BLOCK];  //暂时不用
    int data_block_num;  //目前一个文件只有一个数据块
};

//目录项
struct simple_dir_record{
    int i_ino;  
    char filename[SIMPLE_FILENAME_MAX];
};