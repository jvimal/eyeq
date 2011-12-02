#include <linux/version.h>
#include <linux/module.h>
#include "vq.h"
#include "rx.h"
#include "tx.h"

MODULE_AUTHOR("Vimal <j.vimal@gmail.com>");
MODULE_DESCRIPTION("Perf Isolation");
MODULE_VERSION("1");
MODULE_LICENSE("GPL");

char *iso_param_dev;
MODULE_PARM_DESC(iso_param_dev, "Interface to operate perfiso.");
module_param(iso_param_dev, charp, 0);

static int iso_init(void);
static void iso_exit(void);

static int iso_init() {

	if(iso_param_dev == NULL) {
		/*
		  printk(KERN_INFO "perfiso: need iso_param_dev, the interface to protect.\n");
		  goto err;
		*/
		iso_param_dev = "peth2\0";
		printk(KERN_INFO "perfiso: operating on %s\n", iso_param_dev);
	}

	iso_params_init();

	if(iso_rx_init())
		goto err;

	if(iso_tx_init())
		goto err;

	return 0;
 err:
	return -1;
}

static void iso_exit() {
	iso_tx_exit();
	iso_rx_exit();
	iso_params_exit();
	printk(KERN_INFO "perfiso: goodbye.\n");
}

module_init(iso_init);
module_exit(iso_exit);

/* Local Variables: */
/* indent-tabs-mode:t */
/* End: */

