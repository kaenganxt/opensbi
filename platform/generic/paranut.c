#include <platform_override.h>
#include <sbi_utils/fdt/fdt_helper.h>

static const struct fdt_match paranut_match[] = {
	{ .compatible = "hsa-ees,paranut" },
	{ },
};

const struct platform_override paranut = {
	.match_table = paranut_match,
};
