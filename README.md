cin >> input, acts as getline for char[n], perhaps an oversimplification...

limitations are: cin cannot directly accept int, use atoi

Features: keyboard interrupts, iostream, AHCI sata, XHCI USB, DMA, FAT32 filesystem, format, chkdsk, game engine, copy and paste, delete and touch commands. 

TODO: implement notepad, examine chkdsk upon mutiOS file writes, for spurious deletion, dump DMA and PCIE to file...

Recommended: format desired drive as fat32 first then dd the drive.

sudo dd if=/home/user/Desktop/Text_OS-main/main.iso of=/dev/sdX (replace X with your sata drive, the OS is programmed to select port 0 for all operations)


