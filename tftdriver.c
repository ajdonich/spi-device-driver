
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/uaccess.h>

MODULE_AUTHOR("AJ Donich");
MODULE_LICENSE("GPL");

// This gets defined (or not) in Makefile, uncomment to hard-code
// #define SPI_TFT_DEBUG 1

#undef PDEBUG
#ifdef SPI_TFT_DEBUG
#  define PDEBUG(fmt, args...) printk(KERN_DEBUG "tftdriver: " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

// #define ID_SPI_TFT_DEVICE 0
// #define    SPI_CPHA    0x01
// #define    SPI_CPOL    0x02

static struct spi_device *spi_tft_device = NULL;
static struct cdev tft_cdevice;
// static struct mutex lock;

static int spi_tft_probe(struct spi_device *spi) {
    int err;

    unsigned int maxfreq = spi->max_speed_hz;
    if ((err = of_property_read_u32(spi->dev.of_node, "spi-max-frequency", &maxfreq)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::of_property_read_u32('spi-max-frequency')\n", err);

    spi->max_speed_hz = min(spi->max_speed_hz, maxfreq);
    spi->bits_per_word = 8;
    spi->rt = true;

    if ((err = spi_setup(spi)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::spi_setup\n", err);
    
    spi_tft_device = spi;
    PDEBUG("spi_driver.probe() function: spi_tft_probe was called");
    return err;
}

static int spi_tft_remove(struct spi_device *spi) {
    spi_tft_device = NULL;
    PDEBUG("spi_driver.remove() function: spi_tft_remove was called");
    return 0;
}

static int tft_open(struct inode *inode, struct file *filp) {
    PDEBUG("tft_open mode %u, offset %lld, flags %u", filp->f_mode, filp->f_pos, filp->f_flags);
	filp->private_data = (void *)&tft_cdevice; // Just for good measure; tft_cdevice handle accessible above

    if (filp->f_mode & FMODE_READ) PDEBUG("  FMODE_READ");
    if (filp->f_mode & FMODE_WRITE) PDEBUG("  FMODE_WRITE");
    if (filp->f_mode & FMODE_LSEEK) PDEBUG("  FMODE_LSEEK");
    if (filp->f_mode & FMODE_PREAD) PDEBUG("  FMODE_PREAD");
    if (filp->f_mode & FMODE_PWRITE) PDEBUG("  FMODE_PWRITE");
    if (filp->f_mode & FMODE_EXEC) PDEBUG("  FMODE_EXEC");

    if (filp->f_flags & O_CREAT) PDEBUG("  O_CREAT");
    if (filp->f_flags & O_TRUNC) PDEBUG("  O_TRUNC");
    if (filp->f_flags & O_EXCL) PDEBUG("  O_EXCL");
    if (filp->f_flags & O_NOCTTY) PDEBUG("  O_NOCTTY");
    if (filp->f_flags & O_NONBLOCK) PDEBUG("  O_NONBLOCK");
    if (filp->f_flags & O_APPEND) PDEBUG("  O_APPEND");
    if (filp->f_flags & O_DSYNC) PDEBUG("  O_DSYNC");
    if (filp->f_flags & O_DIRECTORY) PDEBUG("  O_DIRECTORY");
    if (filp->f_flags & O_NOFOLLOW) PDEBUG("  O_NOFOLLOW");
    if (filp->f_flags & O_LARGEFILE) PDEBUG("  O_LARGEFILE");
    if (filp->f_flags & O_DIRECT) PDEBUG("  O_DIRECT");
    if (filp->f_flags & O_NOATIME) PDEBUG("  O_NOATIME");
    if (filp->f_flags & O_CLOEXEC) PDEBUG("  O_CLOEXEC");
    return 0;
}

static int tft_release(struct inode *inode, struct file *filp) {
    PDEBUG("tft_release mode %u, offset %lld, flags %u", filp->f_mode, filp->f_pos, filp->f_flags);
    return 0;
}

static ssize_t tft_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    size_t nread, ncopy;
    const char *output = "tft_read called\n";
    nread = strlen(output);

    ncopy = nread - copy_to_user((void __user *)buf, (const void *)output, nread);
    PDEBUG("copy %zu of %zu bytes in tft_read::copy_to_user\n", ncopy, nread);
    return ncopy;
}

static ssize_t tft_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    size_t ncopy;
    char *inbuffer;
    
    if (count == 0) return 0;
    if ((inbuffer = (char *)kmalloc(count+1, GFP_KERNEL)) == NULL) {
        printk(KERN_ERR "[ENOMEM] in tft_write::kmalloc\n");
        return -ENOMEM;
    }

    ncopy = count - copy_from_user((void *)inbuffer, (const void __user *)buf, count);
    PDEBUG("copy %zu of %zu bytes in tft_write::copy_from_user\n", ncopy, count);
    
    inbuffer[count] = '\0';
    PDEBUG("%s\n", inbuffer);
    kfree((void *)inbuffer);
    return ncopy;
}

static const struct of_device_id of_tft_match[] = {
    { .compatible = "ilitek,spitft" },
    { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_tft_match);

static struct spi_driver spi_tft_driver = {
    .driver = {
        .name = "spitft",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(of_tft_match),
    },
    .probe =    spi_tft_probe,
    .remove =   spi_tft_remove,
};

static struct file_operations tft_fops = {
    .owner =    THIS_MODULE,
    .read =     tft_read,
    .write =    tft_write,
    .open =     tft_open,
    .release =  tft_release,
};

static int tft_setup_cdev(struct cdev *cdev, dev_t devno) {
    int err;
    cdev->owner = THIS_MODULE;
    cdev_init(cdev, &tft_fops);

    // Device goes live in cdev_add call
    if ((err = cdev_add(cdev, devno, 1)) < 0) {
        printk(KERN_ERR "[errno %i] in tft_setup_cdev::cdev_add\n", -err);
    }
    return err;
}

static int __init tft_init_module(void) {
    int err;
    dev_t devno = 0;

    // Allocate one devno (dynamic major, minor starts at 0)
    if ((err = alloc_chrdev_region(&devno, 0, 1, "tftchar")) < 0) {
        printk(KERN_ERR "[errno %i] in tft_init_module::alloc_chrdev_region\n", -err);
        return err;
    }
    
    memset(&tft_cdevice, 0, sizeof(struct cdev)); 
    if( (err = tft_setup_cdev(&tft_cdevice, devno)) < 0 ) {
        unregister_chrdev_region(devno, 1);
        return err;
    }

    printk(KERN_NOTICE "tftchar registered at %x (%i, %i)\n", devno, MAJOR(devno), MINOR(devno));
    return spi_register_driver(&spi_tft_driver);
}

static void __exit tft_cleanup_module(void) {
    dev_t devno = tft_cdevice.dev;
    cdev_del(&tft_cdevice);
    unregister_chrdev_region(devno, 1);
    spi_unregister_driver(&spi_tft_driver);
}

module_init(tft_init_module);
module_exit(tft_cleanup_module);
