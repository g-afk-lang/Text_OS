.POSIX:

ISODIR := iso
MULTIBOOT := $(ISODIR)/boot/main.elf
MAIN := main.iso

.PHONY: clean run

$(MAIN):
	as -32 boot.S -o boot.o

	gcc -c kernel.cpp -ffreestanding -m32 -o kernel.o 

	gcc -c types.cpp -ffreestanding -m32 -o types.o 


	gcc -c terminal_io.cpp -ffreestanding -m32 -o terminal_io.o 

	gcc -c terminal_hooks.cpp -ffreestanding -m32 -o terminal_hooks.o 

	gcc -c stdlib_hooks.cpp -ffreestanding -m32 -o stdlib_hooks.o 

	gcc -c iostream_wrapper.cpp -ffreestanding -m32 -o iostream_wrapper.o 

	gcc -c interrupts.cpp -ffreestanding -m32 -o interrupts.o 


	gcc -c string.cpp -ffreestanding -m32 -o string.o 

	gcc -c test.cpp -ffreestanding -m32 -o test.o 

	gcc -c test2.cpp -ffreestanding -m32 -o test2.o 


	gcc -c hardware_specs.cpp -ffreestanding -m32 -o hardware_specs.o 

	gcc -c pci.cpp -ffreestanding -m32 -o pci.o 

	gcc -c io_port.cpp -ffreestanding -m32 -o io_port.o 
	
	gcc -c dma_memory.cpp -ffreestanding -m32 -o dma_memory.o 

	gcc -ffreestanding -m32 -nostdlib -o '$(MULTIBOOT)' -T linker.ld boot.o kernel.o string.o types.o terminal_io.o terminal_hooks.o stdlib_hooks.o iostream_wrapper.o interrupts.o test.o test2.o hardware_specs.o io_port.o pci.o dma_memory.o -lgcc

	grub-mkrescue -o '$@' '$(ISODIR)'

clean:
	rm -f *.o '$(MULTIBOOT)' '$(MAIN)'

run: $(MAIN)
	qemu-system-i386 -cdrom '$(MAIN)'
	# Would also work.
	#qemu-system-i386 -hda '$(MAIN)'
	#qemu-system-i386 -kernel '$(MULTIBOOT)'
