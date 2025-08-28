#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/sysinfo.h>
#include <linux/uaccess.h>
#include <uapi/linux/spi/spi.h>

#include "spitft.h"

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

#define MAX_UINT16 0x10000
#define MIN(a,b) a <= b ? a : b 
#define MAX(a,b) a >= b ? a : b 


static ili9341_dev tft_spidev;
static struct cdev tft_cdev;

static uint8_t write_mode = GIF_MODE;
static uint8_t *frame_buffer = NULL;
static Rect window = { 0,0,0,0 };
static int yidx = -1;

inline uint8_t rand8(void) {
    uint8_t value;
    get_random_bytes((void *)&value, 1);
    return value;
}

inline uint16_t rand16(void) {
    uint16_t value;
    get_random_bytes((void *)&value, 2);
    return value;
}

static int spi_tft_probe(struct spi_device *spi) {
    unsigned int maxfreq;
    int err;

    maxfreq = spi->max_speed_hz;
    if ((err = of_property_read_u32(spi->dev.of_node, "spi-max-frequency", &maxfreq)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::of_property_read_u32('spi-max-frequency')\n", err);
    
    PDEBUG("spi_driver.probe() function: spi_tft_probe was called");
    PDEBUG("of_property_read_u32(spi-max-frequency): %u (max: %u)", maxfreq, spi->max_speed_hz);
    spi->max_speed_hz = MIN(spi->max_speed_hz, maxfreq);
    spi->bits_per_word = 8;
    spi->mode = SPI_MODE_0;
    spi->rt = true;

    if ((err = spi_setup(spi)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::spi_setup\n", err);
    
    tft_spidev.dc_pin = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_HIGH);
    if (tft_spidev.dc_pin) PDEBUG("devm_gpiod_get(dc-gpio): GPIO%i", desc_to_gpio(tft_spidev.dc_pin));

    tft_spidev.reset_pin = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
    if (tft_spidev.reset_pin) PDEBUG("devm_gpiod_get(reset-gpio): GPIO%i", desc_to_gpio(tft_spidev.reset_pin));

    tft_spidev.ili9341 = spi;
    return err;
}

static int spi_tft_remove(struct spi_device *spi) {
    tft_spidev.ili9341 = NULL;
    PDEBUG("spi_driver.remove() function: spi_tft_remove was called");
    return 0;
}

static int tft_open(struct inode *inode, struct file *filp) {
    PDEBUG("tft_open mode %u, offset %lld, flags %u", filp->f_mode, filp->f_pos, filp->f_flags);
	filp->private_data = (void *)&tft_cdev; // Just for good measure; tft_cdev handle accessible above

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
    int err, ncopy;
    uint8_t *data;

    set_addr_window(&tft_spidev, 0, 0, ILI9341_TFTWIDTH, ILI9341_TFTHEIGHT);
    send_command(&tft_spidev, ILI9341_RAMRD);

    if ((data = (uint8_t *)kmalloc(count, GFP_KERNEL)) == NULL) {
        printk(KERN_ERR "[ENOMEM] in tft_read::kmalloc\n");
        return -ENOMEM;
    }

    ncopy = 0;
    if((err = read_data(&tft_spidev, data, (uint32_t)count)) == 0)
        ncopy = count - copy_to_user((void __user *)buf, (const void *)data, count);

    kfree(data);
    PDEBUG("copy %i of %i bytes in tft_read::copy_to_user", ncopy, count);
    return (ssize_t)(err == 0 ? ncopy : err);
}

static ssize_t tft_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    static Rect rect = { 0,0,0,0 };
    static int iframe = 0, fidx = 0;

    size_t ncopy;
    uint8_t *inbuffer;
    uint8_t randval;
    RGB randcol;

    switch (write_mode) {
    case NOP_MODE: {
        PDEBUG("write_mode NOP_MODE\n");
        send_command(&tft_spidev, ILI9341_NOP);
        break;
    }
    case RECT_MODE: {
        randval = rand8();
        if ((randval & 0x03) == 0) randcol = RED;
        else if ((randval & 0x03) == 1) randcol = GREEN;
        else if ((randval & 0x03) == 2) randcol = BLUE;
        else randcol = (RGB){rand8(), rand8(), rand8()};

        rect.x = (uint32_t)rand16() * (ILI9341_TFTWIDTH - 20) / MAX_UINT16;
        rect.y = (uint32_t)rand16() * (ILI9341_TFTHEIGHT - 20) / MAX_UINT16;
        rect.w = (uint32_t)rand16() * (ILI9341_TFTWIDTH - rect.x) / MAX_UINT16;
        rect.h = (uint32_t)rand16() * (ILI9341_TFTHEIGHT - rect.y) / MAX_UINT16;
        PDEBUG("write_mode RECT_MODE: {%i, %i, %i, %i}\n", rect.x, rect.y, rect.w, rect.h);
        draw_rect(&tft_spidev, rect, randcol);
        break;
    }
    case GIF_MODE: {
        if (yidx == -1) {
            if (count != sizeof(Rect)) {
                printk(KERN_ERR "[Bad Rect size: %zu] in tft_write\n", count);
                break;
            }

            // First line of an image-data-block must be rect coords of the sub-frame-window
            ncopy = count - copy_from_user((void *)&window, (const void __user *)buf, count);
            PDEBUG("Window %i: (%i, %i, %i, %i)\n", iframe, window.x, window.y, window.w, window.h);
            iframe += 1;
            yidx = 0;
            break;
        }
        
        if (yidx < window.h) {
            // Write to specific window in the frame_buffer
            fidx = ((window.y + yidx)*ILI9341_TFTWIDTH + window.x)*2;
            ncopy = count - copy_from_user((void *)&frame_buffer[fidx], (const void __user *)buf, count);
            yidx += 1;
        }

        if (yidx == window.h) {
            // Update the entire frame to the TFT, screen is small enough and
            // this prevents repetitive CASET/PASET/RAMWR commands every frame
            send_data(&tft_spidev, frame_buffer, (ILI9341_TFTWIDTH * ILI9341_TFTHEIGHT)*2);
            yidx = -1;
        }
        break;
    }
    default:
        printk(KERN_ERR "[Bad write_mode: %u] in tft_write\n", write_mode);
        break;
    }

    if (write_mode != GIF_MODE) kfree((void *)inbuffer);
    return ncopy;
}

// Read ioctl command from user space and apply the requested write_mode
long tft_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	// Validate cmd is the one we recognize
	if (_IOC_TYPE(cmd) != SPITFT_IOC_MAGIC || _IOC_NR(cmd) > SPITFT_IOC_MAXNR || cmd != SPITFT_IOCWRMODE) {
        printk(KERN_ERR "[ENOTTY] in tft_ioctl\n");
        return -ENOTTY;
    }
    else if (copy_from_user((void *)&write_mode, (const void __user *)arg, sizeof(uint8_t)) != 0) {
        printk(KERN_ERR "[EFAULT] in tft_ioctl::__copy_from_user\n");
        return -EFAULT; 
    }
    else if (write_mode > GIF_MODE) {
        printk(KERN_ERR "[EINVAL %u] in tft_ioctl\n", write_mode);
        write_mode = NOP_MODE;
        return -EINVAL;
    }

    if (write_mode == GIF_MODE) {
        set_addr_window(&tft_spidev, 0, 0, ILI9341_TFTWIDTH, ILI9341_TFTHEIGHT);
        send_command(&tft_spidev, ILI9341_RAMWR);
    } 

    PDEBUG("set write_mode: %u in tft_ioctl\n", write_mode);
	return 0;
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
    .unlocked_ioctl = tft_ioctl,
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
    
    memset(&tft_cdev, 0, sizeof(struct cdev)); 
    if( (err = tft_setup_cdev(&tft_cdev, devno)) < 0 ) {
        unregister_chrdev_region(devno, 1);
        return err;
    }

    printk(KERN_NOTICE "tftchar registered at %x (%i, %i)\n", devno, MAJOR(devno), MINOR(devno));
    if( (err = spi_register_driver(&spi_tft_driver)) < 0 ) {
        printk(KERN_ERR "[errno %i] in tft_init_module::spi_register_driver\n", -err);
        return err;
    }
    
    init_tft_display(&tft_spidev);
    if ((frame_buffer = (uint8_t *)kmalloc(ILI9341_TFTWIDTH*ILI9341_TFTHEIGHT*2, GFP_KERNEL)) == NULL) {
        printk(KERN_ERR "[ENOMEM] in tft_init_module::kmalloc\n");
        return -ENOMEM;
    }
    return 0;
}

static void __exit tft_cleanup_module(void) {
    dev_t devno;

    send_command(&tft_spidev, ILI9341_DISPOFF); mdelay(150); // Display off
    send_command(&tft_spidev, ILI9341_SLPIN); mdelay(150); // Enter Sleep

    devno = tft_cdev.dev;
    cdev_del(&tft_cdev);
    unregister_chrdev_region(devno, 1);
    spi_unregister_driver(&spi_tft_driver);
    kfree(frame_buffer);
}

module_init(tft_init_module);
module_exit(tft_cleanup_module);

