root/boot/scratch.elf: src/scratch.s assemble
	./assemble $< -o $@

all: .attach

run: .attach
	VBoxManage startvm scratch

.attach: scratch.vdi .vm
	VBoxManage storageattach scratch --storagectl SATA --port 0 --type hdd --medium $<
	touch $@

scratch.vdi: scratch.iso .deleteDisk.sh .vm
	chmod +x .deleteDisk.sh
	./.deleteDisk.sh
	VBoxManage convertfromraw $< $@ --format VDI

.vm:
	VBoxManage createvm --name scratch --ostype Other_64 --register
	VBoxManage storagectl scratch --name SATA --add sata --controller IntelAHCI
	touch $@

scratch.iso: root/boot/grub/grub.cfg root/boot/scratch.elf
	grub-mkrescue -o $@ root

assemble: tools/assembler.c
	gcc $< -o $@

kill: .vm
	VBoxManage controlvm scratch poweroff

clean:
	chmod +x .deleteDisk.sh
	./.deleteDisk.sh
	rm -f *.iso assemble root/boot/*.elf

