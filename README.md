# VFZSDR

This is a Linux Kernel Module for controlling SM6VFZ's FPGA based
Software Defined Radio.  More information in his repository
https://github.com/danupp/fpgasdr.

Refer
to
[register-map.org](https://github.com/danupp/fpgasdr/blob/master/docs/register-map.org) for
full documentation on the i2c based protocol.

# Building

To build Linux out-tree-modules you need to have the source for the
kernel you are using available.

On the Raspberry Pi an easy way to install the sources is the
`rpi-source` utility. Check https://github.com/notro/rpi-source/wiki
for full instructions.


```bash
# Install kernel sources
$ rpi-source

# Clone this repo
$ git clone https://github.com/ast/vfzsdr.git

# Build
$ make

# Load module
sudo insmod vfzsdr.ko

# Check for errors
# You should see some debug messages from the module
$ sudo dmesg

```

# Using

I have only tested on Raspberry Pi 3 but it will probably work on
other platforms.

The module will create a device at `/dev/vfzsdr`. You can read the
status byte from this device. To use this device as a memeber of the
group `i2c` you can install the provided `udev` rule:

```bash
# On debian based systems:
$ sudo cp 90-vfzsdr.rules /etc/udev/rules.d/
```


```bash
# To use the options as a non-root user member of i2c group.
$ sudo chgrp -R i2c /sys/class/sdr/vfzsdr/

# To set audio gain to 10 (0 = max, 63 = mute):
$ echo 10 > /sys/class/sdr/vfzsdr/gain

# To set frequency to (close to) 14.000.000Hz:
$ echo 14000000 > /sys/class/sdr/vfzsdr/frequency

# To read frequency:
$ cat /sys/class/sdr/vfzsdr/frequency

# Read frequency, mode and filter width:
$ cat /sys/class/sdr/vfzsdr/status

# To change mode:
$ echo [AM|LSB|USB] > /sys/class/sdr/vfzsdr/mode

# To change filter width:
$ echo [n|w] > /sys/class/sdr/vfzsdr/filter

# To tune 10 hardware steps up:
$ echo 10 > /sys/class/sdr/vfzsdr/tune
# A hardware step is 120Mhz/2^25 = 3.5762...Hz

# Tune down:
$ echo -10 > /sys/class/sdr/vfzsdr/tune
```


# Bugs

Probably many. This is an initial release. **Use at your own risk**.

* Some functions are still not implemented.

# License

MIT License

Copyright (c) 2017 Albin Stigö

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
