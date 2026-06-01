#ifndef _DEVICE_ID_H
#define _DEVICE_ID_H 1

#include <stdint.h>

typedef struct device_id {
    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
} device_id_t;


#endif // _DEVICE_ID_H