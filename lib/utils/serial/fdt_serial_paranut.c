/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi/sbi_console.h>

volatile uint8_t tohost __attribute__((section(".tohost")));

static const struct fdt_match serial_paranut_match[] = {
	{ .compatible = "paranut,tohost" },
	{ },
};

static void paranut_putc(char ch)
{
	tohost = ch;
	while (tohost > 0);
}

static int paranut_getc(void)
{
	return -1;
}

static struct sbi_console_device paranut_console = {
	.name = "paranut",
	.console_putc = paranut_putc,
	.console_getc = paranut_getc
};

static int serial_paranut_init(void *fdt, int nodeoff,
			    const struct fdt_match *match)
{
	sbi_console_set_device(&paranut_console);
	return 0;
}

struct fdt_serial fdt_serial_paranut = {
	.match_table = serial_paranut_match,
	.init = serial_paranut_init
};
