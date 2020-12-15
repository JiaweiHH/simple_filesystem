# 学习往Linux添加文件系统

## 挂载

```shell
make mkfs
dd if=/dev/zero of=test.img bs=1M count=50
./mkfs.simple ./test_img
sudo mount -t simplefs -o loop ./test.img /mnt
```
