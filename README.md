# libusbserial - PSVita usb-to-serial driver

PSVita driver for FTDI and CH34X usb-uart adapters.

## Building

* `mkdir build && cmake -DCMAKE_BUILD_TYPE=Release .. && make`

## Usage

* Install `libusbserial.skprx` (copy and add it to config). Alternatively, distribute it with your app and load on-demand.
* Run sample (or your app), connect FTDI/CH34X dongle (via powered Y-cable on vita).

## License

GPLv3, see LICENSE.md
