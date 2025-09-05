## Linux SPI Kernel Driver for ILI9341 TFT LCD Display 

This project implements an SPI driver designed to load on a Raspberry Pi 4B and enable RGB image data to be streamed onto an ILI9341 TFT display through a character device node in `/dev`. In addition to the driver code in this repo, an [example application](https://github.com/ajdonich/spi-device-driver/tree/main/examples) is included that opens, decodes, and streams GIF files into the driver *[it leverages another open source submodule called [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) for GIF decoding]*.  

This was a final project for a Linux embedded systems development series at CU Boulder. It is not intended to be a full-featured graphics driver, but rather to solidify my skills rolling a custom embedded Linux distro in Yocto, using device trees and overlays and developing a moderately complex Kernel module for a specific hardware stack, and deploying within Linux's driver abstraction architecture.


### For further information see: 
+ Project demo video: [youtube.com/spi-device-driver](https://youtu.be/Ar2Ma9GNPYA)
+ Project Yocto build repo: [github.com/cu-ecen-aeld/final-project-ajdonich](https://github.com/cu-ecen-aeld/final-project-ajdonich)
+ Project proposal wiki: [github.com/cu-ecen-aeld/final-project-ajdonich/wiki](https://github.com/cu-ecen-aeld/final-project-ajdonich/wiki)
