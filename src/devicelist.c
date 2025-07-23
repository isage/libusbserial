#include "devicelist.h"

serialdevice_t _devices[] = {{TYPE_FTDI, 0x0403, 0x6001},
                             {TYPE_FTDI, 0x0403, 0x6010},
                             {TYPE_FTDI, 0x0403, 0x6011},
                             {TYPE_FTDI, 0x0403, 0x6014},
                             {TYPE_FTDI, 0x0403, 0x6015},

                             {TYPE_CH34X, 0x1a86, 0x5523},
                             {TYPE_CH34X, 0x1a86, 0x7522},
                             {TYPE_CH34X, 0x1a86, 0x7523},
                             {TYPE_CH34X, 0x2184, 0x0057},
                             {TYPE_CH34X, 0x4348, 0x5523},
                             {TYPE_CH34X, 0x9986, 0x7523},

                             {TYPE_UNKNOWN, 0x0000, 0x0000}}; // Null
