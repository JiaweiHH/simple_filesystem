# 学习往Linux添加文件系统

## 设计

目前最简单的实现方式，之后可以改进

+------------+-------------+-------------------+
| superblock | data blocks | inode blocks      | 
+------------+-------------+-------------------+

## 编译和运行

编译模块：

```shell
$ make
$ make insmod simplefs.ko
```

创建挂载点目录，格式化设备文件

```shell
$ mkdir -p test
$ dd if=/dev/zero of=test.img bs=1M count=50
$ make mkfs
$ ./mkfs.simple ./test.img
```

挂载和卸载文件系统

```shell
$ sudo mount -t simplefs -o loop ./test.img test
$ sudo umount -v test
```
