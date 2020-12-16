#include "simplefs.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>

static struct kmem_cache *simplefs_inode_cache = NULL;

/* inode operations */
static struct dentry *simplefs_lookup(struct inode *parent, struct dentry *child, unsigned int flags);
static int simplefs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);

static int simple_iterate(struct file *filp, struct dir_context *ctx){
    printk("iterate called\n");
    return 0;
}

//普通文件操作
static struct file_operations simple_fops;

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
    // .iterate = 
};

/* ==========inode_operations========== */

//dir父节点，dentry当前创建的文件的dentry
static int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl){
    printk("create 调用了\n");
    struct super_block *sb = dir->i_sb;

    //分配一个inode
    struct inode *inode = new_inode(sb);
    inode->i_sb = sb;
    inode->i_op = &simple_iops;
    inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;

    //设置inode编号
    struct simple_superblock *simple_sb = (struct simple_superblock *)sb->s_fs_info;
    inode->i_ino = simple_sb->s_inodecount++;

    //分配磁盘存储inode并设置编号
    struct simple_inode *simple_inode = kmem_cache_alloc(simplefs_inode_cache, GFP_KERNEL);
    inode->i_private = simple_inode;
    simple_inode->i_ino = inode->i_ino;

    //获取data block
    int i;
    for(i = 0; i < SIMPLE_DATA_BLOCK_COUNT; ++i){
        if(simple_sb->s_blockmap[i] == 0){
            simple_inode->data_block_num = i;
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
    //写入数据
    parent_dir_inode_disk->dir_child_count = parent_dir_inode->dir_child_count;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    // dir/inode
    inode_init_owner(inode, dir, mode);
    //这样子 lookup 函数可以在父目录看到新创建的inode信息
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
    printk("lookup called\n");
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

    //根目录inode，和sb相关联
    struct inode *root_inode = new_inode(sb);

    //开始填充inode结构体
    inode_init_owner(root_inode, NULL, 0777|S_IFDIR);
    root_inode->i_ino = 0;
    root_inode->i_sb = sb;
    root_inode->i_op = &simple_iops;
    root_inode->i_fop = &simple_dops;
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = CURRENT_TIME;
    //获取磁盘存储的inode结构体
    bh = sb_bread(sb, SIMPLE_INODE_BLOCK_BASE);
    struct simple_inode *root_dinode = (struct simple_inode *)bh->b_data;
    root_inode->i_private = root_dinode;

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