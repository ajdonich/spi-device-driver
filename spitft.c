#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>

#include "spitft.h"


MODULE_AUTHOR("AJ Donich");
MODULE_LICENSE("GPL");

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
int send_command(ili9341_dev *spidev, uint8_t cmdcode) {
    int err;
    struct spi_transfer cmdtrans = {
        .tx_buf = (const void *)&cmdcode,
        .len = sizeof(uint8_t)
    };

    struct spi_message cmdmsg;
    spi_message_init(&cmdmsg);
    spi_message_add_tail(&cmdtrans, &cmdmsg);

    gpiod_set_value(spidev->dc_pin, LOW);
    if ((err = spi_sync(spidev->ili9341, &cmdmsg)) != 0)
        printk(KERN_ERR "[%i] in send_command::spi_sync\n", -err);
    gpiod_set_value(spidev->dc_pin, HIGH);
    return err;
}
EXPORT_SYMBOL(send_command);

// Send a multi-byte SPI block (in a single spi_transfer object) 
int send_data(ili9341_dev *spidev, const uint8_t *data, uint32_t nbytes) {
    int err;
    struct spi_transfer dtrans = {
        .tx_buf = (const void *)data,
        .len = nbytes
    };

    struct spi_message dmsg;
    spi_message_init(&dmsg);
    spi_message_add_tail(&dtrans, &dmsg);
    
    if ((err = spi_sync(spidev->ili9341, &dmsg)) != 0)
        printk(KERN_ERR "[%i] in send_data::spi_sync\n", -err);
    return err;
}
EXPORT_SYMBOL(send_data);

// Read a multi-byte SPI block (in a single spi_transfer object) 
int read_data(ili9341_dev *spidev, uint8_t *data, uint32_t nbytes) {
    int err;
    struct spi_transfer dtrans = {
        .rx_buf = (void *)data,
        .len = nbytes
    };

    struct spi_message dmsg;
    spi_message_init(&dmsg);
    spi_message_add_tail(&dtrans, &dmsg);
    
    if ((err = spi_sync(spidev->ili9341, &dmsg)) != 0)
        printk(KERN_ERR "[%i] in read_data::spi_sync\n", -err);
    return err;
}
EXPORT_SYMBOL(read_data);

// Send SPI "transaction" block (multiple spi_transfer objects) 
int send_transaction(ili9341_dev *spidev, struct spi_transfer trans[], uint32_t ntrans) {
    int err;
    struct spi_message dmsg;
    spi_message_init(&dmsg);
    for (int i=0; i<ntrans; i++)
        spi_message_add_tail(&trans[i], &dmsg);

    if ((err = spi_sync(spidev->ili9341, &dmsg)) != 0)
        printk(KERN_ERR "[%i] in send_transaction::spi_sync\n", -err);
    return err;
}
EXPORT_SYMBOL(send_transaction);

int draw_rect(ili9341_dev *spidev, Rect rect, RGB color) {
    uint32_t nbytes = rect.w * sizeof(uint16_t);
    uint8_t *line16 = (uint8_t *)kmalloc(nbytes, GFP_KERNEL);
    if (line16 == NULL) {
        printk(KERN_ERR "[ENOMEM] in draw_rect::kmalloc\n");
        return -ENOMEM;
    }

    fill_line16(line16, color, rect.w);
    set_addr_window(spidev, rect.x, rect.y, rect.w, rect.h);

    send_command(spidev, ILI9341_RAMWR);
    for (uint32_t i=0; i<rect.h; ++i)
        send_data(spidev, line16, nbytes);
    
    // NOP to terminate RAMWR cmd
    send_command(spidev, ILI9341_NOP);
    kfree((void *)line16);
    return 0;
}
EXPORT_SYMBOL(draw_rect);


// Initialization sequence adapted from https://github.com/adafruit/Adafruit_ILI9341, written by Limor Fried/Ladyada 
// for Adafruit Industries, MIT license. Please see https://github.com/adafruit/Adafruit_ILI9341/blob/master/README.md
void init_tft_display(ili9341_dev *spidev) {
    send_command(spidev, 0x01); mdelay(150); // SW Reset (doesn't hurt but only really need if RESET pin is floating)
    send_command(spidev, 0xEF); send_data(spidev, (const uint8_t[]){0x03, 0x80, 0x02}, 3);
    send_command(spidev, 0xCF); send_data(spidev, (const uint8_t[]){0x00, 0xC1, 0x30}, 3); 
    send_command(spidev, 0xED); send_data(spidev, (const uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4); 
    send_command(spidev, 0xE8); send_data(spidev, (const uint8_t[]){0x85, 0x00, 0x78}, 3); 
    send_command(spidev, 0xCB); send_data(spidev, (const uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5); 
    send_command(spidev, 0xF7); send_data(spidev, (const uint8_t[]){0x20}, 1);
    send_command(spidev, 0xEA); send_data(spidev, (const uint8_t[]){0x00, 0x00}, 2); 
    
    send_command(spidev, 0xC0); send_data(spidev, (const uint8_t[]){0x23}, 1); // Power control VRH[5:0]
    send_command(spidev, 0xC1); send_data(spidev, (const uint8_t[]){0x10}, 1); // Power control SAP[2:0];BT[3:0]
    send_command(spidev, 0xC5); send_data(spidev, (const uint8_t[]){0x3e, 0x28}, 2); // VCM control
    send_command(spidev, 0xC7); send_data(spidev, (const uint8_t[]){0x86}, 1); // VCM control2
    send_command(spidev, 0x36); send_data(spidev, (const uint8_t[]){0x48}, 1); // Memory Access Control
    send_command(spidev, 0x37); send_data(spidev, (const uint8_t[]){0x00}, 1); // Vertical scroll zero
    send_command(spidev, 0x3A); send_data(spidev, (const uint8_t[]){0x55}, 1); 
    send_command(spidev, 0xB1); send_data(spidev, (const uint8_t[]){0x00, 0x18}, 2); 
    send_command(spidev, 0xB6); send_data(spidev, (const uint8_t[]){0x08, 0x82, 0x27}, 3); // Display Function Control
    send_command(spidev, 0xF2); send_data(spidev, (const uint8_t[]){0x00}, 1); // Gamma Function Disable
    send_command(spidev, 0x26); send_data(spidev, (const uint8_t[]){0x01}, 1); // Gamma curve selected
    send_command(spidev, 0xE0); send_data(spidev, (const uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15); // Set Gamma
    send_command(spidev, 0xE1); send_data(spidev, (const uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15); // Set Gamma
    send_command(spidev, 0x11); mdelay(150); // Exit Sleep
    send_command(spidev, 0x29); mdelay(150); // Display on
}
EXPORT_SYMBOL(init_tft_display);

int set_addr_window(ili9341_dev *spidev, uint16_t x1, uint16_t y1, uint16_t w, uint16_t h) {
    static uint16_t old_x1 = 0xffff, old_x2 = 0xffff;
    static uint16_t old_y1 = 0xffff, old_y2 = 0xffff;
    static uint8_t data[4];
    uint16_t x2, y2;

    x2 = (x1 + w - 1);
    if (x1 != old_x1 || x2 != old_x2) {
        uint8_t *dptr = data;
        dptr = pack_MSB16(dptr, x1);
        dptr = pack_MSB16(dptr, x2);
        send_command(spidev, ILI9341_CASET);
        send_data(spidev, data, 4);
        old_x1 = x1;
        old_x2 = x2;
    }

    y2 = (y1 + h - 1);
    if (y1 != old_y1 || y2 != old_y2) {
        uint8_t *dptr = data;
        dptr = pack_MSB16(dptr, y1);
        dptr = pack_MSB16(dptr, y2);
        send_command(spidev, ILI9341_PASET);
        send_data(spidev, data, 4);
        old_y1 = y1;
        old_y2 = y2;
    }

    return 0;
}
EXPORT_SYMBOL(set_addr_window);
