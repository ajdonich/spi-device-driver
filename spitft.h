#ifndef ILI9341_SPITFT_H
#define ILI9341_SPITFT_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define ILI9341_TFTWIDTH 240U  // ILI9341 max TFT width
#define ILI9341_TFTHEIGHT 320U // ILI9341 max TFT height
#define ILI9341_NPIXELS 76800U

#define BLACK (RGB){0,0,0}
#define RED (RGB){255,0,0}
#define GREEN (RGB){0,255,0}
#define BLUE (RGB){0,0,255}

#define NOP_MODE  0x00
#define RECT_MODE 0x01
#define GIF_MODE 0x02

#define LOW 0
#define HIGH 1

#define SPI_DC_LOW(dc_pin)  gpiod_set_value(dc_pin, LOW);
#define SPI_DC_HIGH(dc_pin) gpiod_set_value(dc_pin, HIGH);

#define RESET_LOW(reset_pin) gpiod_set_value(reset_pin, LOW)
#define RESET_HIGH(reset_pin) gpiod_set_value(reset_pin, HIGH)

typedef struct {
    uint8_t R; 
    uint8_t G;
    uint8_t B;
} RGB;

typedef struct {
    int x, y, w, h; // position and size (px)
} Rect;

// Arbitrary unused value from/based on https://github.com/torvalds/linux/blob/master/Documentation/userspace-api/ioctl/ioctl-number.rst
#define SPITFT_IOC_MAGIC 0x18

// Define a write command from the user point of view, using command number 1
#define SPITFT_IOCWRMODE _IOWR(SPITFT_IOC_MAGIC, 1, uint8_t)

// The maximum number of commands supported, used for bounds checking
#define SPITFT_IOC_MAXNR 1

#ifdef __KERNEL__
#define ILI9341_NOP 0x00     // No-op register
#define ILI9341_SWRESET 0x01 // Software reset register
#define ILI9341_RDDID 0x04   // Read display identification information
#define ILI9341_RDDST 0x09   // Read Display Status

#define ILI9341_SLPIN 0x10  // Enter Sleep Mode
#define ILI9341_SLPOUT 0x11 // Sleep Out
#define ILI9341_PTLON 0x12  // Partial Mode ON
#define ILI9341_NORON 0x13  // Normal Display Mode ON

#define ILI9341_RDMODE 0x0A     // Read Display Power Mode
#define ILI9341_RDMADCTL 0x0B   // Read Display MADCTL
#define ILI9341_RDPIXFMT 0x0C   // Read Display Pixel Format
#define ILI9341_RDIMGFMT 0x0D   // Read Display Image Format
#define ILI9341_RDSELFDIAG 0x0F // Read Display Self-Diagnostic Result

#define ILI9341_INVOFF 0x20   // Display Inversion OFF
#define ILI9341_INVON 0x21    // Display Inversion ON
#define ILI9341_GAMMASET 0x26 // Gamma Set
#define ILI9341_DISPOFF 0x28  // Display OFF
#define ILI9341_DISPON 0x29   // Display ON

#define ILI9341_CASET 0x2A // Column Address Set
#define ILI9341_PASET 0x2B // Page Address Set
#define ILI9341_RAMWR 0x2C // Memory Write
#define ILI9341_RAMRD 0x2E // Memory Read

#define ILI9341_PTLAR 0x30    // Partial Area
#define ILI9341_VSCRDEF 0x33  // Vertical Scrolling Definition
#define ILI9341_MADCTL 0x36   // Memory Access Control
#define ILI9341_VSCRSADD 0x37 // Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A   // COLMOD: Pixel Format Set

#define ILI9341_FRMCTR1 0xB1 // Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_FRMCTR2 0xB2 // Frame Rate Control (In Idle Mode/8 colors)
#define ILI9341_FRMCTR3 0xB3 // Frame Rate control (In Partial Mode/Full Colors)
#define ILI9341_INVCTR 0xB4  // Display Inversion Control
#define ILI9341_DFUNCTR 0xB6 // Display Function Control

#define ILI9341_PWCTR1 0xC0 // Power Control 1
#define ILI9341_PWCTR2 0xC1 // Power Control 2
#define ILI9341_PWCTR3 0xC2 // Power Control 3
#define ILI9341_PWCTR4 0xC3 // Power Control 4
#define ILI9341_PWCTR5 0xC4 // Power Control 5
#define ILI9341_VMCTR1 0xC5 // VCOM Control 1
#define ILI9341_VMCTR2 0xC7 // VCOM Control 2

#define ILI9341_RDID1 0xDA // Read ID 1
#define ILI9341_RDID2 0xDB // Read ID 2
#define ILI9341_RDID3 0xDC // Read ID 3
#define ILI9341_RDID4 0xDD // Read ID 4

#define ILI9341_GMCTRP1 0xE0 // Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1 // Negative Gamma Correction

typedef struct {
    struct spi_device *ili9341;
    struct gpio_desc *dc_pin, *reset_pin;
} ili9341_dev;

// Byte packing helper functions
uint8_t *pack_MSB16(uint8_t *data, uint16_t val);
uint8_t *pack_RGB16(uint8_t *data, RGB color);
void fill_line16(uint8_t *line, RGB color, size_t npixels);

// SPI interface API
int send_command(ili9341_dev *spidev, uint8_t cmdcode);
int send_data(ili9341_dev *spidev, const uint8_t *data, uint32_t nbytes);
int read_data(ili9341_dev *spidev, uint8_t *data, uint32_t nbytes);
int send_transaction(ili9341_dev *spidev, struct spi_transfer trans[], uint32_t ntrans);
int draw_rect(ili9341_dev *spidev, Rect rect, RGB color);

// ILI9341 specific commands
void init_tft_display(ili9341_dev *spidev);
int set_addr_window(ili9341_dev *spidev, uint16_t x1, uint16_t y1, uint16_t w, uint16_t h);
#endif // __KERNEL__

#endif // ILI9341_SPITFT_H
