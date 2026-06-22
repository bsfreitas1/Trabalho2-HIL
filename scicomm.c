#include "scicomm.h"
#include <driverlib.h>

void protocolSendData(unsigned int sci_base, void *data, uint16_t byteSize) {
    uint16_t sciBuffer;
    uint16_t *dataPtr = (uint16_t *)data;

    for (uint16_t i = 0; i < byteSize*2; i++) {
        sciBuffer = (uint16_t)(((uint16_t *)data)[i / 2] >> (8 * (i % 2))) & 0x00FF;
        SCI_writeCharArray(sci_base, &sciBuffer, 1);
    }
}

void protocolReceiveData(unsigned int sci_base, void *data, uint16_t byteSize) {
    uint16_t sciBuffer;
    uint16_t *dataPtr = (uint16_t *)data;

    for (uint16_t i = 0; i < byteSize*2; i++) {
        SCI_readCharArray(sci_base, &sciBuffer, 1);
        if (i % 2 == 0)
            dataPtr[i / 2] = (dataPtr[i / 2] & 0xFF00) | (sciBuffer & 0x00FF);
        else
            dataPtr[i / 2] = (dataPtr[i / 2] & 0x00FF) | ((sciBuffer & 0x00FF) << 8);
    }
}
