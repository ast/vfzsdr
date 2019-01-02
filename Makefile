obj-m+=vfzsdr.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean

install: all
	sudo cp vfzsdr.ko /lib/modules/$(shell uname -r)
	sudo depmod -a
