# 学习往Linux添加文件系统

## 挂载

**创建挂载目录和设备，格式化设备文件**

```shell
mkdir -p test
dd if=/dev/zero of=test.img bs=1M count=50
./mkfs.simple ./test.img
```

**编译挂载**

```shell
sudo ./mount.sh
```

**umount**

```shell
sudo ./umount.sh
```
