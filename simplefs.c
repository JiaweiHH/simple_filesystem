#include "simplefs.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>

static struct kmem_cache *simplefs_inode_cache = NULL;

/* 通用函数 */
static struct inode *simplefs_get_inode(struct super_block *sb, struct inode *dir, struct simple_inode *simple_inode, int mode);

/* inode operations */
static struct dentry *simplefs_lookup(struct inode *parent, struct dentry *child, unsigned int flags);
static int simplefs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);

/* dir operations */
static int simplefs_iterate(struct file *filp, struct dir_context *ctx);

/* file operations */
static ssize_t simplefs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
static ssize_t simplefs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos); //读写文件本质操作就是移动文件偏移量，copy 文件数据

//普通文件操作
static struct file_operations simple_fops = {
    .write = simplefs_write,
    .read = simplefs_read,
};

static struct inode_operations simple_iops = {
    // .lookup = simple_lookup,
    .lookup = simplefs_lookup,
    .mkdir = simplefs_mkdir,  //创建目录
    .create = simplefs_create,  //创建一个文件或者目录
};

static struct super_operations simple_sops;

//目录操作
static struct file_operations simple_dops = {
    .owner = THIS_MODULE, 
    .iterate = simplefs_iterate,
};

/* ==========通用函数========== */

static struct inode *simplefs_get_inode(struct super_block *sb, struct inode *dir, struct simple_inode *simple_inode, int mode){
    //分配inode，和sb相关联
    struct inode *inode = new_inode(sb);

    //填充 inode
    inode->i_mode = mode;
    inode->i_sb = sb;
    inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
    inode->i_op = &simple_iops;
    inode->i_private = simple_inode;
    inode->i_ino = simple_inode->i_ino;

    inode_init_owner(inode, dir, mode);
    
    if(S_ISDIR(mode)){
        inode->i_fop = &simple_dops;
        set_nlink(inode, 2);  //设置链接计数
        simple_inode->i_type = SIMPLE_FILE_TYPE_DIR;
        i_size_write(inode, SIMPLE_BLOCK_SIZE);
        simple_inode->dir_child_count = 0;
    }else if(S_ISREG(mode)){
        inode->i_fop = &simple_fops;
        simple_inode->i_type = SIMPLE_FILE_TYPE_FILE;
    }

    return inode;
}

/* ==========file_operations========== */

/*
 * 读取文件。如果以此读取没有读完整个len长度则会继续调用，
 * 因此需要判断 *ppos+len > file_size，以及 *ppos >= file_size
 */
static ssize_t simplefs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos){
    //获取相关数据结构
    printk("read called. 准备读取 %d 字节数据，文件偏移: %lld\n", len, *ppos);
    struct inode *inode = filp->f_inode;
    struct super_block *sb = inode->i_sb;
    struct simple_inode *simple_inode = (struct simple_inode *)inode->i_private;
    if(*ppos >= simple_inode->file_size){
        printk("error. file_size: %d, ppos: %lld\n", simple_inode->file_size, *ppos);
        return 0;
    }
    
    //读取数据
    struct buffer_head *bh = sb_bread(sb, simple_inode->data_block_num + SIMPLE_DATA_BLOCK_BASE);
    char *buffer = (char *)bh->b_data;
    buffer += *ppos;
    len = min(len, simple_inode->file_size - *ppos);  //修改读取文件长度，防止越界
    if(copy_to_user(buf, buffer, min(len, simple_inode->file_size - *ppos)))
        return -EFAULT;
    *ppos += len;

    //更新inode文件访问时间
    inode->i_atime = CURRENT_TIME;
    return len;
}

/*
 * 写文件
 * filp: 要写的文件, buf: 用户空间准备写入的数据, len: 数据长度, ppos: 文件偏移量
 */
static ssize_t simplefs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos){
    printk("write called\n");

    //获取相关数据结构
    struct inode *inode = filp->f_inode;
    struct super_block *sb = inode->i_sb;
    struct simple_inode *simple_inode = (struct simple_inode *)inode->i_private;  //内存中的磁盘上的 inode 结构体

    struct buffer_head *bh = sb_bread(sb, simple_inode->data_block_num + SIMPLE_DATA_BLOCK_BASE);
    char *buffer = (char *)bh->b_data;  //buffer 为指向data block的指针

    //开始 write 操作
    buffer += *ppos;  //buffer 指针向后移动到偏移量的位置
    if(copy_from_user(buffer, buf, len))  //写数据到 buffer
        return -EFAULT;
    *ppos += len;
    simple_inode->file_size = *ppos;
    mark_buffer_dirty(bh);  //标记buffer_head为脏
    sync_dirty_buffer(bh);  //往磁盘写入脏数据

    //更新inode文件数据和属性修改时间
    inode->i_mtime = inode->i_ctime = CURRENT_TIME;

    //更新磁盘上的 simple_inode 文件大小数据
    bh = sb_bread(sb, simple_inode->i_ino + SIMPLE_INODE_BLOCK_BASE);
    struct simple_inode *simple_inode_disk = (struct simple_inode *)bh->b_data;
    simple_inode_disk->file_size = simple_inode->file_size;  //内存中的 simple_inode 和磁盘上的数据不同步
    mark_buffer_dirty(bh);  //标记buffer_head为脏
    sync_dirty_buffer(bh);  //往磁盘写入脏数据

    //更新 inode 中的文件大小属性
    i_size_write(inode, *ppos);

    brelse(bh);
    return len;
}

/* ==========dir_operations========== */

/*
 * 遍历目录项
 * filp: 目录文件, ctx: 存储目录项内容
 */
static int simplefs_iterate(struct file *filp, struct dir_context *ctx){
    struct inode *inode = filp->f_inode;
    struct super_block *sb = inode->i_sb;
    struct simple_inode *simple_inode = (struct simple_inode *)inode->i_private;

    if(ctx->pos){
        return 0;
    }

    //判断是不是目录
    if(simple_inode->i_type != SIMPLE_FILE_TYPE_DIR){
        printk("不是一个目录\n");
        return -ENOTDIR;
    }

    //TODO: 目前还有问题。添加 . 和 ..
    if(!dir_emit_dots(filp, ctx)){
        printk(". and .. add error\n");
        return 0;
    }

    //获取data block地址
    struct buffer_head *bh = sb_bread(sb, SIMPLE_DATA_BLOCK_BASE + simple_inode->data_block_num);
    struct simple_dir_record *dir_record = (struct simple_dir_record *)bh->b_data;

    //遍历目录项
    int i;
    for(i = 0; i < simple_inode->dir_child_count; ++i){
        //填充目录项
        printk("filename: %s, ino: %d\n", dir_record->filename, dir_record->i_ino);
        dir_emit(ctx, dir_record->filename, SIMPLE_FILENAME_MAX, dir_record->i_ino, DT_UNKNOWN);
        ctx->pos ++;
        dir_record++;
    }

    brelse(bh);
    return 0;
}

/* ==========inode_operations========== */

/*
 * 创建 inode，创建文件的时候只需创建一个inode就可以了
 * dir: 父节点, dentry: 当前创建的文件的dentry
 */
static int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl){
    struct super_block *sb = dir->i_sb;
    //获取磁盘上的超级块数据结构
    struct simple_superblock *simple_sb = (struct simple_superblock *)sb->s_fs_info;

    //分配磁盘存储inode并设置编号
    struct simple_inode *simple_inode = kmem_cache_alloc(simplefs_inode_cache, GFP_KERNEL);
    simple_inode->i_ino = simple_sb->s_inodecount++;

    //分配一个inode
    struct inode *inode = simplefs_get_inode(sb, dir, simple_inode, mode);
    //获取data block
    int i;
    for(i = 0; i < SIMPLE_DATA_BLOCK_COUNT; ++i){
        if(simple_sb->s_blockmap[i] == 0){
            simple_inode->data_block_num = i; //设置数据块编号
            simple_sb->s_blockmap[i] = 1;
            break;
        }
    }

    //获取内存中父inode的磁盘存储数据结构
    struct simple_inode *parent_dir_inode = (struct simple_inode *)dir->i_private;
    parent_dir_inode->dir_child_count++;
    //读取父inode对应的data block，往里面写入目录项
    struct buffer_head *bh = sb_bread(sb, parent_dir_inode->data_block_num + SIMPLE_DATA_BLOCK_BASE);
    struct simple_dir_record *dir_contents = (struct simple_dir_record *)bh->b_data;  //获取block的位置，目录项的起始地址

    dir_contents += parent_dir_inode->dir_child_count - 1;  //新目录项的起始地址
    strcpy(dir_contents->filename, dentry->d_name.name);  //写入文件名
    dir_contents->i_ino = simple_inode->i_ino; //写入索引节点编号

    mark_buffer_dirty(bh);  //标记buffer_head为脏
    sync_dirty_buffer(bh);  //往磁盘写入脏数据
    brelse(bh);

    //获取磁盘上父inode的存储数据结构
    bh = sb_bread(sb, SIMPLE_INODE_BLOCK_BASE + inode->i_ino);
    struct simple_inode *parent_dir_inode_disk = (struct simple_inode *)bh->b_data;
    //写入child count计数
    parent_dir_inode_disk->dir_child_count = parent_dir_inode->dir_child_count;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    //加入到父dentry的hash表
    d_add(dentry, inode);

    // d_instantiate(dentry, inode);
    return 0;    
}

static int simplefs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode){
    printk("mkdir called\n");
    int retval = simplefs_create(dir, dentry, mode|S_IFDIR, 0);
    return retval;
}

//chdir的时候必须要有这个函数，但是不会被调用
struct dentry *simplefs_lookup(struct inode *parent, struct dentry *child, unsigned int flags){
    return NULL;
}

/* ==========file_super========== */

static int simplefs_fill_super(struct super_block *sb, void *data, int silent){
    //超级块被读入内存的时候保存在bh结构体中，每一块对应一个bh。 从磁盘读取超级块
    struct buffer_head *bh = sb_bread(sb, SIMPLE_SUPER_BLOCK_NO);  
    //void *b_data 直接指向block的位置
    struct simple_superblock *simple_sb = (struct simple_superblock *)bh->b_data;

    sb->s_magic = simple_sb->s_magic;  //魔幻数
    sb->s_op = &simple_sops;  //操作集合
    sb->s_maxbytes = SIMPLE_PER_FILE_BLOCK * SIMPLE_BLOCK_SIZE;  //每个文件的最大大小
    sb->s_fs_info = simple_sb;  //superblock的私有域存放磁盘上的结构体

    //获取磁盘存储的inode结构体
    bh = sb_bread(sb, SIMPLE_INODE_BLOCK_BASE);
    struct simple_inode *root_dinode = (struct simple_inode *)bh->b_data;

    struct inode *root_inode = simplefs_get_inode(sb, NULL, root_dinode, 0777|S_IFDIR);

    //创建根目录
    sb->s_root = d_make_root(root_inode);
    if(!sb->s_root){
        printk("create root dentry failed\n");
        return -ENOMEM;
    }

    brelse(bh);
    return 0;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data){
    //超级块填充函数，创建根目录
    struct dentry *dentry = mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);
    if(!dentry)
        printk("mounted error\n");
    return dentry;
}

static struct file_system_type simple_fs_type = {
    .owner = THIS_MODULE,
    .name = "simplefs",
    .fs_flags = FS_REQUIRES_DEV,
    .mount = simplefs_mount,
    .kill_sb = kill_block_super,  //VFS 提供的销毁方法
};

static int __init init_simplefs(void){
    simplefs_inode_cache = kmem_cache_create("simplefs_inode_cache", sizeof(struct simple_inode), 0, 
                                                (SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD), NULL);
    return register_filesystem(&simple_fs_type);
}

static void __exit exit_simplefs(void){
    return unregister_filesystem(&simple_fs_type);
}

module_init(init_simplefs);
module_exit(exit_simplefs);
MODULE_AUTHOR("jiaweiHH");
MODULE_LICENSE("GPL");