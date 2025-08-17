
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <uapi/linux/spi/spi.h>

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


#define ILI9341_TFTWIDTH 240U  ///< ILI9341 max TFT width
#define ILI9341_TFTHEIGHT 320U ///< ILI9341 max TFT height
#define ILI9341_NPIXELS 76800U

#define ILI9341_NOP 0x00     ///< No-op register
#define ILI9341_SWRESET 0x01 ///< Software reset register
#define ILI9341_RDDID 0x04   ///< Read display identification information
#define ILI9341_RDDST 0x09   ///< Read Display Status

#define ILI9341_SLPIN 0x10  ///< Enter Sleep Mode
#define ILI9341_SLPOUT 0x11 ///< Sleep Out
#define ILI9341_PTLON 0x12  ///< Partial Mode ON
#define ILI9341_NORON 0x13  ///< Normal Display Mode ON

#define ILI9341_RDMODE 0x0A     ///< Read Display Power Mode
#define ILI9341_RDMADCTL 0x0B   ///< Read Display MADCTL
#define ILI9341_RDPIXFMT 0x0C   ///< Read Display Pixel Format
#define ILI9341_RDIMGFMT 0x0D   ///< Read Display Image Format
#define ILI9341_RDSELFDIAG 0x0F ///< Read Display Self-Diagnostic Result

#define ILI9341_INVOFF 0x20   ///< Display Inversion OFF
#define ILI9341_INVON 0x21    ///< Display Inversion ON
#define ILI9341_GAMMASET 0x26 ///< Gamma Set
#define ILI9341_DISPOFF 0x28  ///< Display OFF
#define ILI9341_DISPON 0x29   ///< Display ON

#define ILI9341_CASET 0x2A ///< Column Address Set
#define ILI9341_PASET 0x2B ///< Page Address Set
#define ILI9341_RAMWR 0x2C ///< Memory Write
#define ILI9341_RAMRD 0x2E ///< Memory Read

#define ILI9341_PTLAR 0x30    ///< Partial Area
#define ILI9341_VSCRDEF 0x33  ///< Vertical Scrolling Definition
#define ILI9341_MADCTL 0x36   ///< Memory Access Control
#define ILI9341_VSCRSADD 0x37 ///< Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A   ///< COLMOD: Pixel Format Set

#define ILI9341_FRMCTR1 0xB1 ///< Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_FRMCTR2 0xB2 ///< Frame Rate Control (In Idle Mode/8 colors)
#define ILI9341_FRMCTR3 0xB3 ///< Frame Rate control (In Partial Mode/Full Colors)
#define ILI9341_INVCTR 0xB4  ///< Display Inversion Control
#define ILI9341_DFUNCTR 0xB6 ///< Display Function Control

#define ILI9341_PWCTR1 0xC0 ///< Power Control 1
#define ILI9341_PWCTR2 0xC1 ///< Power Control 2
#define ILI9341_PWCTR3 0xC2 ///< Power Control 3
#define ILI9341_PWCTR4 0xC3 ///< Power Control 4
#define ILI9341_PWCTR5 0xC4 ///< Power Control 5
#define ILI9341_VMCTR1 0xC5 ///< VCOM Control 1
#define ILI9341_VMCTR2 0xC7 ///< VCOM Control 2

#define ILI9341_RDID1 0xDA ///< Read ID 1
#define ILI9341_RDID2 0xDB ///< Read ID 2
#define ILI9341_RDID3 0xDC ///< Read ID 3
#define ILI9341_RDID4 0xDD ///< Read ID 4

#define ILI9341_GMCTRP1 0xE0 ///< Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1 ///< Negative Gamma Correction

#define LOW  0
#define HIGH 1

#define SPI_DC_LOW()  gpiod_set_value(dc_pin, LOW);
#define SPI_DC_HIGH() gpiod_set_value(dc_pin, HIGH);

#define RESET_LOW() gpiod_set_value(reset_pin, LOW)
#define RESET_HIGH() gpiod_set_value(reset_pin, HIGH)

#define BLACK (RGB){0,0,0}
#define RED (RGB){63,0,0}
#define GREEN (RGB){0,63,0}
#define BLUE (RGB){0,0,63}


static struct spi_device *spi_ili9341 = NULL;
struct gpio_desc *dc_pin, *reset_pin;
static struct cdev tft_cdevice;
// static struct mutex lock;

typedef struct {
    uint8_t R; 
    uint8_t G;
    uint8_t B;
} RGB;

typedef struct {
    int x, y; // position (px)
    uint16_t w, h; // size (px)
} Rect;

// Packs a uint16_t in MSB-first format
uint8_t *pack_MSB16(uint8_t *data, uint16_t val) {
    *data = (uint8_t)(val >> 8); data++;
    *data = (uint8_t)(val & 0xFF); data++;
    return data;
}

// Packs in 16-bit RGB-565 format
uint8_t *pack_RGB16(uint8_t *data, RGB color) {
    *data = (color.R << 3) | (color.G >> 5); data++;
    *data = ((color.G & 0x7) << 5) | (color.B & 0x1F); data++;
    return data;
}

// Packs a length npixels data buffer of color in RGB-565 format
void fill_line16(uint8_t *line, RGB color, size_t npixels) {
    for (int i=0; i<npixels; i++) 
        line = pack_RGB16(line, color);
}

// Send a 1-byte SPI command 
int send_command(uint8_t cmdcode) {
    int err;
    struct spi_transfer cmdtrans = {
        .tx_buf = (const void *)&cmdcode,
        .len = sizeof(uint8_t)
    };

    struct spi_message cmdmsg;
    spi_message_init(&cmdmsg);
    spi_message_add_tail(&cmdtrans, &cmdmsg);
    
    SPI_DC_LOW();
    if ((err = spi_sync(spi_ili9341, &cmdmsg)) != 0)
        printk(KERN_ERR "[%i] in send_command::spi_sync\n", -err);
    SPI_DC_HIGH();
    return err;
}

// Send a multi-byte SPI block (in a single spi_transfer object) 
int send_data(const uint8_t *data, uint32_t nbytes) {
    int err;
    struct spi_transfer dtrans = {
        .tx_buf = (const void *)data,
        .len = nbytes
    };

    struct spi_message dmsg;
    spi_message_init(&dmsg);
    spi_message_add_tail(&dtrans, &dmsg);
    
    if ((err = spi_sync(spi_ili9341, &dmsg)) != 0)
        printk(KERN_ERR "[%i] in send_command::spi_sync\n", -err);
    return err;
}

void init_tft_display(void) {
    send_command(0x01); mdelay(150); // SW Reset (doesn't hurt but only really need if RESET pin is floating)
    send_command(0xEF); send_data((const uint8_t[]){0x03, 0x80, 0x02}, 3);
    send_command(0xCF); send_data((const uint8_t[]){0x00, 0xC1, 0x30}, 3); 
    send_command(0xED); send_data((const uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4); 
    send_command(0xE8); send_data((const uint8_t[]){0x85, 0x00, 0x78}, 3); 
    send_command(0xCB); send_data((const uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5); 
    send_command(0xF7); send_data((const uint8_t[]){0x20}, 1);
    send_command(0xEA); send_data((const uint8_t[]){0x00, 0x00}, 2); 
    send_command(0xC0); send_data((const uint8_t[]){0x23}, 1); // Power control VRH[5:0]
    send_command(0xC1); send_data((const uint8_t[]){0x10}, 1); // Power control SAP[2:0];BT[3:0]
    send_command(0xC5); send_data((const uint8_t[]){0x3e, 0x28}, 2); // VCM control
    send_command(0xC7); send_data((const uint8_t[]){0x86}, 1); // VCM control2
    send_command(0x36); send_data((const uint8_t[]){0x48}, 1); // Memory Access Control
    send_command(0x37); send_data((const uint8_t[]){0x00}, 1); // Vertical scroll zero
    send_command(0x3A); send_data((const uint8_t[]){0x55}, 1); 
    send_command(0xB1); send_data((const uint8_t[]){0x00, 0x18}, 2); 
    send_command(0xB6); send_data((const uint8_t[]){0x08, 0x82, 0x27}, 3); // Display Function Control
    send_command(0xF2); send_data((const uint8_t[]){0x00}, 1); // Gamma Function Disable
    send_command(0x26); send_data((const uint8_t[]){0x01}, 1); // Gamma curve selected
    send_command(0xE0); send_data((const uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15); // Set Gamma
    send_command(0xE1); send_data((const uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15); // Set Gamma
    send_command(0x11); mdelay(150); // Exit Sleep
    send_command(0x29); mdelay(150); // Display on
}

int set_addr_window(uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) {
    static uint16_t old_x1 = 0xffff, old_x2 = 0xffff;
    static uint16_t old_y1 = 0xffff, old_y2 = 0xffff;
    static uint8_t data[4];
    uint16_t x2, y2;

    x2 = (x1 + w - 1);
    if (x1 != old_x1 || x2 != old_x2) {
        uint8_t *dptr = data;
        dptr = pack_MSB16(dptr, x1);
        dptr = pack_MSB16(dptr, x2);
        send_command(ILI9341_CASET);
        send_data(data, 4);
        old_x1 = x1;
        old_x2 = x2;
    }

    y2 = (y1 + h - 1);
    if (y1 != old_y1 || y2 != old_y2) {
        uint8_t *dptr = data;
        dptr = pack_MSB16(dptr, y1);
        dptr = pack_MSB16(dptr, y2);
        send_command(ILI9341_PASET);
        send_data(data, 4);
        old_y1 = y1;
        old_y2 = y2;
    }

    return 0;
}

int draw_rect(Rect rect, RGB color) {
    uint32_t nbytes = rect.w * sizeof(uint16_t);
    uint8_t *line16 = (uint8_t *)kmalloc(nbytes, GFP_KERNEL);
    if (line16 == NULL) {
        printk(KERN_ERR "[ENOMEM] in draw_rect::kmalloc\n");
        return -ENOMEM;
    }

    fill_line16(line16, color, rect.w);
    set_addr_window(rect.x, rect.y, rect.w, rect.h);

    send_command(ILI9341_RAMWR);
    for (uint32_t i=0; i<rect.h; ++i)
        send_data(line16, nbytes);
    
    // NOP to terminate RAMWR cmd
    send_command(ILI9341_NOP);
    kfree((void *)line16);
    return 0;
}

static int spi_tft_probe(struct spi_device *spi) {
    PDEBUG("spi_driver.probe() function: spi_tft_probe was called");
    unsigned int maxfreq;
    int err;

    maxfreq = spi->max_speed_hz;
    if ((err = of_property_read_u32(spi->dev.of_node, "spi-max-frequency", &maxfreq)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::of_property_read_u32('spi-max-frequency')\n", err);
    
    PDEBUG("of_property_read_u32(spi-max-frequency): %u (%u)", maxfreq, spi->max_speed_hz);
    spi->max_speed_hz = min(spi->max_speed_hz, maxfreq);
    spi->bits_per_word = 8;
    spi->mode = SPI_MODE_0;
    spi->rt = true;

    if ((err = spi_setup(spi)) != 0)
        printk(KERN_WARNING "%i in spi_tft_probe::spi_setup\n", err);
    
    dc_pin = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_HIGH);
    if (dc_pin) PDEBUG("devm_gpiod_get(dc-gpio): GPIO%i", desc_to_gpio(dc_pin));

    reset_pin = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_HIGH);
    if (reset_pin) PDEBUG("devm_gpiod_get(reset-gpio): GPIO%i", desc_to_gpio(reset_pin));

    spi_ili9341 = spi;
    return err;
}

static int spi_tft_remove(struct spi_device *spi) {
    spi_ili9341 = NULL;
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
    PDEBUG("copy %zu of %zu bytes in tft_read::copy_to_user", ncopy, nread);
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
    PDEBUG("copy %zu of %zu bytes in tft_write::copy_from_user", ncopy, count);
    inbuffer[count] = '\0';
    
    if ( strncmp(inbuffer, "INIT", count) == 0 ) {
        init_tft_display();
        PDEBUG("/dev/tftchar command: %s", inbuffer);
    }    
    else if ( strncmp(inbuffer, "HWRESET", count) == 0 ) {
        RESET_LOW(); mdelay(500);
        RESET_HIGH();
        PDEBUG("/dev/tftchar command: %s", inbuffer);
    }
    else if ( strncmp(inbuffer, "RECT", count) == 0 ) {
        static RGB colors[] = { RED, GREEN, BLUE };
        static int tnumb = 1;

        Rect rect = { 
            .x = ILI9341_TFTWIDTH/2 - 25, 
            .y = ILI9341_TFTHEIGHT/2 - 50,
            .w = 50, .h = 100, 
        };

        draw_rect(rect, colors[tnumb % 3]);
        tnumb += 1;

        PDEBUG("/dev/tftchar command: %s", inbuffer);
    }
    else PDEBUG("Unknown /dev/tftchar command: %s", inbuffer);

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
