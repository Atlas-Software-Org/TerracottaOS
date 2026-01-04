.SUFFIXES:

ARCH := x86_64

QEMUFLAGS := -m 6G #-serial stdio

override IMAGE_NAME := terra-$(ARCH)

HOST_CC := cc
HOST_CFLAGS := -g -O0 -pipe
HOST_CPPFLAGS := -g -O0 -pipe
HOST_LDFLAGS := -g
HOST_LIBS :=

.PHONY: all-iso
all-iso: $(IMAGE_NAME).iso

.PHONY: all
all: $(IMAGE_NAME).hdd

.PHONY: initrd
initrd:
	@echo "Creating initrd.img..."
	mkdir -p kernel/bin-$(ARCH)
	rm -rf kernel/bin-$(ARCH)/initrd.img
	cd initrd && tar -cf "../kernel/bin-$(ARCH)/initrd.img" -H ustar ./*
	@echo "initrd.img created at kernel/bin-$(ARCH)/initrd.img"

.PHONY: run
run: run-$(ARCH)

.PHONY: run-hdd
run-hdd: run-hdd-$(ARCH)

run-x86_64: edk2-ovmf $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-machine q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-device ahci,id=ahci0 \
		-drive id=disk,if=none,file=$(IMAGE_NAME).iso,format=raw \
		-device ide-hd,drive=disk,bus=ahci0.0 \
		$(QEMUFLAGS)

.PHONY: run-hdd-x86_64
run-hdd-x86_64: edk2-ovmf $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-machine q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-device ahci,id=ahci0 \
		-drive id=disk,if=none,file=$(IMAGE_NAME).hdd,format=raw \
		-device ide-hd,drive=disk,bus=ahci0.0 \
		$(QEMUFLAGS)

.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M q35 \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: run-hdd-bios
run-hdd-bios: $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M q35 \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

edk2-ovmf:
	curl -L https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz | gunzip | tar -xf -

limine/limine:
	rm -rf limine
	git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.x-binary --depth=1
	$(MAKE) -C limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

kernel/.deps-obtained:
	./kernel/get-deps

.PHONY: kernel
kernel: kernel/.deps-obtained
	$(MAKE) -C kernel

$(IMAGE_NAME).iso: limine/limine kernel
	@rm -rf iso_root
	@mkdir -p iso_root/boot
	@cp -v kernel/bin-$(ARCH)/kernel iso_root/boot/
	@mkdir -p iso_root/boot/limine
	@cp -v limine.conf iso_root/boot/limine/
	@mkdir -p iso_root/EFI/BOOT
ifeq ($(ARCH),x86_64)
	@cp -v limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	@cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	@cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	@cp -v kernel/bin-$(ARCH)/initrd.img iso_root/boot/initrd.img
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	@./limine/limine bios-install $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),aarch64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTAA64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),riscv64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTRISCV64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),loongarch64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTLOONGARCH64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
	@rm -rf iso_root

$(IMAGE_NAME).hdd: limine/limine kernel
	@rm -f $(IMAGE_NAME).hdd
	@dd if=/dev/zero bs=1M count=64 of=$(IMAGE_NAME).hdd
ifeq ($(ARCH),x86_64)
	@PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00 -m 1
	@./limine/limine bios-install $(IMAGE_NAME).hdd
else
	@PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00
endif
	@mformat -i $(IMAGE_NAME).hdd@@1M &> /dev/null
	@mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI
	@mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI/BOOT
	@mmd -i $(IMAGE_NAME).hdd@@1M ::/boot
	@mmd -i $(IMAGE_NAME).hdd@@1M ::/boot/limine
	@mcopy -i $(IMAGE_NAME).hdd@@1M kernel/bin-$(ARCH)/kernel ::/boot &> /dev/null
	@mcopy -i $(IMAGE_NAME).hdd@@1M limine.conf ::/boot/limine &> /dev/null
ifeq ($(ARCH),x86_64)
	@mcopy -i $(IMAGE_NAME).hdd@@1M limine/limine-bios.sys ::/boot/limine &> /dev/null
	@mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTX64.EFI ::/EFI/BOOT &> /dev/null
	@mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTIA32.EFI ::/EFI/BOOT &> /dev/null
endif
ifeq ($(ARCH),aarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTAA64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),riscv64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTRISCV64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),loongarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTLOONGARCH64.EFI ::/EFI/BOOT
endif

.PHONY: clean
clean:
	$(MAKE) -C kernel clean
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	$(MAKE) -C kernel distclean
	rm -rf iso_root *.iso *.hdd kernel-deps limine ovmf
