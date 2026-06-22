/*
 * scicomm.h
 *
 *  Created on: 13 de jun de 2025
 *      Author: Guilherme Márcio Soares
 */

#ifndef SRC_SCICOMM_H_
#define SRC_SCICOMM_H_
#include <stdint.h>

#define INT_SIZE 2U
#define FLOAT_SIZE 4U
#define PROTOCOL_HEADER_SIZE 3U

typedef enum
{
    CMD_NONE = 0,
    CMD_RECEIVE_INT,
    CMD_SEND_INT,
    CMD_RECEIVE_FLOAT,
    CMD_SEND_FLOAT,
    CMD_COUNT

} SCI_Command_e;

typedef struct
{
    SCI_Command_e cmd;
    uint16_t data_len;

} Protocol_Header_t;


void protocolSendData(unsigned int sci_base, void *data, uint16_t size);
void protocolReceiveData(unsigned int sci_base, void *data, uint16_t size);


#endif /* SRC_SCICOMM_H_ */
