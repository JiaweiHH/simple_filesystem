make
sudo insmod simplefs.ko
sudo mount -t simplefs -o loop ./test.img test
