#include "simplefs.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>

static struct kmem_cache *simplefs_inode_cache = NULL;

static int simple_iterate(struct file *filp, struct dir_context *ctx){
    printk("iterate called\n");
    return 0;
}

//普通文件操作
static struct file_operations simple_fops;

static struct inode_operations simple_iops;
static struct super_operations simple_sops;

// //目录操作
// static struct file_operatinos simplefs_dops;

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
    root_inode->i_ino = SIMPLE_INODE_BLOCK_BASE;
    root_inode->i_sb = sb;
    root_inode->i_op = &simple_iops;
    root_inode->i_fop = &simple_fops;
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

static struct dentry *simple_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data){
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
    .mount = simple_mount,
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