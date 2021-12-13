# BOCHS虚拟机配置

## Linux 参考


develop/bochs/bochsrc.linux

```
###############################################################
# bochsrc.linux file for xbook kernel disk image on linux
# develop environment.
###############################################################

# how much memory the emulated machine will have
megs: 256

# filename of ROM images
romimage: file=$bxshare/BIOS-bochs-latest
vgaromimage: file=$bxshare/VGABIOS-lgpl-latest

#cpu: count=1, ips=10000000

# what disk images will be used 
floppya: 1_44=developments/image/a.img, status=inserted

# hard disk
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata0-master: type=disk, path="developments/image/c.img", cylinders=20, heads=16, spt=63
ata0-slave: type=disk, path="developments/image/d.img", cylinders=20, heads=16, spt=63

# choose the boot disk.
boot: a

# where do we send log messages?
log: developments/bochs/bochsout.txt

# enable the mouse
mouse: enabled=0

# enable key mapping, using US layout as default.
#
# NOTE: In Bochs 1.4, keyboard mapping is only 100% implemented on X windows.
# However, the key mapping tables are used in the paste function, so 
# in the DLX Linux example I'm enabling keyboard_mapping so that paste 
# will work.  Cut&Paste is currently implemented on win32 and X windows only.

com1: enabled=1, mode=file, dev="develop/bochs/output.txt"

keyboard: enabled=1, map=$bxshare/keymaps/x11-pc-us.map

```
## Windows 参考

develop/bochs/bochsrc.win

```
###############################################################
# bochsrc.win file for xbook Kernel disk image on Windows 
# environment.
###############################################################

cpu: model=pentium,count=1, ips=10000000
# how much memory the emulated machine will have
megs: 512

# filename of ROM images
romimage: file=$BXSHARE/BIOS-bochs-latest
vgaromimage: file=$BXSHARE/VGABIOS-lgpl-latest

# what disk images will be used 
floppya: 1_44=develop/image/a.img, status=inserted

# hard disk
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata0-master: type=disk, path="develop/image/c.img", cylinders=20, heads=16, spt=63
ata0-slave: type=disk, path="develop/image/d.img", cylinders=20, heads=16, spt=63

# choose the boot disk.
boot: a

# where do we send log messages?
log: develop/bochs/bochsout.txt

# enable the mouse
mouse: enabled=0, toggle=ctrl+f10

# enable key mapping, using US layout as default.
#
# NOTE: In Bochs 1.4, keyboard mapping is only 100% implemented on X windows.
# However, the key mapping tables are used in the paste function, so 
# in the DLX Linux example I'm enabling keyboard_mapping so that paste 
# will work.  Cut&Paste is currently implemented on win32 and X windows only.

keyboard: type=mf, serial_delay=250
keyboard: keymap=$BXSHARE/keymaps/x11-pc-us.map
keyboard: user_shortcut=ctrl-alt

com1: enabled=1, mode=file, dev="develop/bochs/output.txt"

#keyboard: enabled=1, map=$BXSHARE/keymaps/x11-pc-us.map
#keyboard_mapping: enabled=1, map=$BXSHARE/keymaps/x11-pc-fr.map
#keyboard_mapping: enabled=1, map=$BXSHARE/keymaps/x11-pc-de.map
#keyboard_mapping: enabled=1, map=$BXSHARE/keymaps/x11-pc-es.map

```
