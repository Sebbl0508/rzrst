# HID Driver for "Razer USB Sound Card"
At the moment this just activates the Sidetone feature and sets 100% volume upon connection of the Soundcard. In the future i want to implement a way to set the state (ON/OFF) and volume (0 <-> 100) from userspace.

## Status
- Added generic netlink interface for sending commands (volume, state)
  - (Volume doesn't work yet, since idk how to convert integers to 16-bit floats in kernel code)
- ⚠️ I don't recommend using this yet, since i've got to secure the "global" state with a mutex
  - (I don't want to know what happens if you send netlink commands in a loop or remove the usb device while the driver is processing a request)

## How to build & test
(You have to have kernel headers installed, often the package is named `linux-headers`)
- Build: `make build` (Ignore CMake, it's just for development with CLion)
- Insert Module: `sudo insmod rzrst.ko`
- Remove Module: `sudo rmmod rzrst`
