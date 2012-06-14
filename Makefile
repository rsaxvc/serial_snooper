NAME=serial_snooper
obj-m += ${NAME}.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

test:
	sudo dmesg -c > /dev/null
	sudo insmod ${NAME}.ko
	sudo rmmod ${NAME}
	dmesg
