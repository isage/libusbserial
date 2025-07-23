#ifndef __DEVICELIST_H__
#define __DEVICELIST_H__

#include <stdint.h>

typedef enum
{
  TYPE_UNKNOWN,
  TYPE_FTDI,
  TYPE_CH34X,
  TYPE_PL2303,
} SerialDeviceType;

typedef struct
{
  SerialDeviceType type;
  uint16_t idVendor;
  uint16_t idProduct;
} serialdevice_t;

extern serialdevice_t _devices[];

#endif // __DEVICELIST_H__
