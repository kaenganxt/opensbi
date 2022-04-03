export CROSS_COMPILE=/opt/ees/riscv/riscv64--glibc--bleeding-edge-2020.08-1/bin/riscv64-buildroot-linux-gnu-
export ARCH=riscv
make PLATFORM=generic FW_PAYLOAD_PATH=../linux/arch/riscv/boot/Image PLATFORM_RISCV_ABI=ilp32 PLATFORM_RISCV_ISA=rv32ima FW_TEXT_START=0x10000000 PLATFORM_RISCV_XLEN=32 FW_FDT_PATH=../linux/arch/riscv/boot/dts/paranut/paranut.dtb
riscv64-buildroot-linux-gnu-objdump -d build/platform/generic/firmware/fw_payload.elf > fw.dump
