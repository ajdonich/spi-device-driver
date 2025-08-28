#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "AnimatedGIF/src/AnimatedGIF.h"
#include "AnimatedGIF/src/gif.inl"
#include "../spitft.h"


enum OUTPUT_TYPE {
    STDOUT = 1,
    CDEVICE
};

GIFIMAGE gif;
uint8_t *pStart;
int iFrame, iRow;
int devfd;

// Global signal handler flags
volatile sig_atomic_t _exitflag = 0;  // SIGINT/SIGTERM

void exitSigHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) _exitflag = 1;
}

int MilliTime()
{
int iTime;
struct timespec res;
    clock_gettime(CLOCK_MONOTONIC, &res);
    iTime = 1000*res.tv_sec + res.tv_nsec/1000000;
    return iTime;
} /* MilliTime() */


void GIFDraw(GIFDRAW *pDraw) {
    ssize_t nwritten;
    if (iRow == 0) {
        Rect window = { pDraw->iX, pDraw->iY, pDraw->iWidth, pDraw->iHeight };
        nwritten = write(devfd, (void *)&window, sizeof(Rect));
        if (nwritten == -1 || nwritten != sizeof(Rect)) {
           printf("ERROR: [%s] in GIFDraw::write(Rect) (%zi of %zi)\n", strerror(errno), nwritten, sizeof(Rect));
        }
    }

    nwritten = write(devfd, (void *)pDraw->pPixels, pDraw->iWidth*2);
    if (nwritten == -1 || nwritten != pDraw->iWidth*2) {
        printf("ERROR: [%s] in GIFDraw::write() (%zi of %i)\n", strerror(errno), nwritten, pDraw->iWidth*2);
    }
    iRow += 1;
}

void GIFDrawStd(GIFDRAW *pDraw) {
    if (iRow == 0)
        printf("Metrics %i: %i, %i, %i, %i\n", iFrame, pDraw->iX, pDraw->iY, pDraw->iWidth, pDraw->iHeight);

    printf("0x%02X", pDraw->pPixels[0]);
    for (int i=1; i<pDraw->iWidth*2; ++i) {
        printf(",0x%02X", pDraw->pPixels[i]);
    }
    printf("\n");
    iRow += 1;
}

bool readTest(void) {
    size_t count = ILI9341_TFTWIDTH * ILI9341_TFTHEIGHT * 2;
    uint8_t *buffer = (uint8_t *)malloc(count);
    ssize_t nread = read(devfd, (void *)buffer, count);
    if(nread == -1) {
        printf("ERROR: [%s] in readTest::read()\n", strerror(errno));
        free((void *)buffer);
        return false;
    }
    else if (nread < count) {
        printf("WARNING: only received %zi bytes in readTest::read()\n", nread);
    }
    
    printf("Metrics 0: 0, 0, %i, %i\n", ILI9341_TFTWIDTH, ILI9341_TFTHEIGHT);
    for (int i=0; i<nread; ++i) {
        if ((i+1) % (ILI9341_TFTWIDTH*2))
            printf("0x%02X,", buffer[i]);
        else printf("0x%02X\n", buffer[i]);
    }

    free((void *)buffer);
    return true;
}

bool rectTest(void) {
    uint8_t dummybyte = 1;
    if ((write(devfd, (void *)&dummybyte, 1)) == -1) {
        printf("ERROR: [%s] in rectTest::write(Rect)\n", strerror(errno));
        return false;
    }
    return true;
}

void print_usage(void) {
    printf("Usage: TftGifStreamer  [-t | -r] [-s] [-d number] <path/to/gif>\n");
    printf("  -t Run random rectangle draw test\n");
    printf("  -r Run read display test\n");
    printf("  -s Write to stdout (rather than /dev/tftchar) \n");
    printf("  -d Set write delay b/w frames (sec)\n");
}

int main(int argc, char *argv[]) {
    int iTime, opt;
    int w, h;
    int status = 1;
    int touput = CDEVICE;
    uint32_t delay = 0;
    uint8_t write_mode = GIF_MODE;
    int ret = EXIT_SUCCESS;

    while ((opt = getopt(argc, argv, "strd:")) != -1) {
        switch (opt) {
        case 's':
            touput = STDOUT;
            break;
        case 't':
            if (write_mode == NOP_MODE) {
                print_usage();
                exit(EXIT_FAILURE);
            }
            write_mode = RECT_MODE;
            break;
        case 'r':
            if (write_mode == RECT_MODE) {
                print_usage();
                exit(EXIT_FAILURE);
            }
            write_mode = NOP_MODE;
            break;
        case 'd':
            delay = atoi(optarg); // optarg holds the argument for -d
            break;
        default: // Handles unknown options or missing arguments
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (write_mode == GIF_MODE && optind >= argc) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (touput == CDEVICE) {
        const char *devname = "/dev/tftchar";
        if ((devfd = open(devname, O_RDWR)) == -1) {
            printf("ERROR: [%s] in main::open(%s)\n", strerror(errno), devname);
            exit(EXIT_FAILURE);
        }

        if(ioctl(devfd, SPITFT_IOCWRMODE, &write_mode) == -1) {
            printf("ERROR: [%s] in main::ioctl(2)\n", strerror(errno));
            ret = EXIT_FAILURE;
            goto close_out;
        }
    }

    if (write_mode == NOP_MODE) {
        if (!readTest()) {
            ret = EXIT_FAILURE;
            goto close_out;
        }
    }
    else if (write_mode == RECT_MODE) {
        while(!_exitflag) {
            if (!rectTest()) {
                ret = EXIT_FAILURE;
                goto close_out;
            }
            sleep(delay > 0 ? delay : 1);
        }
    }
    else if (write_mode == GIF_MODE) {
        memset(&gif, 0, sizeof(gif));
        GIF_begin(&gif, GIF_PALETTE_RGB565_BE);
        if (GIF_openFile(&gif, argv[optind], touput == STDOUT ? GIFDrawStd : GIFDraw)) {
            w = gif.iCanvasWidth;
            h = gif.iCanvasHeight;
            gif.pFrameBuffer = (uint8_t*)malloc(w * h * 3);
            pStart = &gif.pFrameBuffer[w*h];
            gif.ucDrawType = GIF_DRAW_COOKED;
            while (!_exitflag) {
                iFrame = iRow = 0;
                iTime = MilliTime();
                while (!_exitflag && status > 0) {
                    if ((status = GIF_playFrame(&gif, NULL, NULL)) == -1) {
                        printf("ERROR: [%i], in main::GIF_playFrame\n", gif.iError);
                        ret = EXIT_FAILURE;
                        goto close_out;
                    }
                    iFrame += 1;
                    iRow = 0;

                    if (delay > 0) sleep(delay);
                }
                iTime = MilliTime() - iTime;
                printf("%d frames in %d ms\n", iFrame, iTime);
                GIF_reset(&gif);
                status = 1;
            }
        }
    }

    close_out:
        GIF_close(&gif);
        if (gif.pFrameBuffer) free((void*)gif.pFrameBuffer);
        if (touput == CDEVICE) close(devfd);
        return ret;
}
