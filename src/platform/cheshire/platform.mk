# Architecture definition
ARCH:=riscv
# CPU definition
CPU:=
# Interrupt controller definition
IRQC:=PLIC

drivers := sbi_uart

platform_description:=cheshire_desc.c

platform-cppflags =
platform-cflags = 
platform-asflags =
platform-ldflags =
