# HID Driver for "Razer USB Sound Card"
At the moment this just activates the Sidetone feature and sets 100% volume upon connection of the Soundcard. In the future i want to implement a way to set the state (ON/OFF) and volume (0 <-> 100) from userspace.

## How to build & test
(You have to have kernel headers installed, often the package is named `linux-headers`)
- Build: `make build` (Ignore CMake, it's just for development with CLion)
- Insert Module: `sudo insmod rzrst.ko`
- Remove Module: `sudo rmmod rzrst`
