#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "scicomm.h"
#include "shared_vars.h"

// Mapeamento nas Message RAMs (Obrigatorio para o CLA acessar)
#pragma DATA_SECTION(i_meas, "CpuToCla1MsgRAM");
float i_meas = 0.0f;

#pragma DATA_SECTION(i_ref, "CpuToCla1MsgRAM");
float i_ref = 0.0f;

#pragma DATA_SECTION(v_grid_ff, "CpuToCla1MsgRAM");
float v_grid_ff = 0.0f;

#pragma DATA_SECTION(duty_cycle_cla, "Cla1ToCpuMsgRAM");
float duty_cycle_cla = 0.0f;

volatile uint16_t cla_task_complete = 0;

void main(void)
{
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Board_init();

    EINT;
    ERTM;

    while (1)
    {
        // Verifica se há pelo menos 12 bytes na FIFO (3 floats * 4 bytes cada)
        if (SCI_getRxFIFOStatus(SCI0_BASE) > 11)
        {
            // Recebe os parâmetros na ordem: I_med, I_ref, V_grid
            protocolReceiveData(SCI0_BASE, &i_meas, sizeof(float));
            protocolReceiveData(SCI0_BASE, &i_ref, sizeof(float));
            protocolReceiveData(SCI0_BASE, &v_grid_ff, sizeof(float));

            SCI_resetRxFIFO(SCI0_BASE);
            
            // Executa o CLA
            cla_task_complete = 0;
            CLA_forceTasks(myCLA0_BASE, CLA_TASKFLAG_1);

            // Aguarda o processamento
            while (cla_task_complete == 0);

            // Envia o sinal modulante (Duty Cycle) de volta ao PLECS
            protocolSendData(SCI0_BASE, &duty_cycle_cla, sizeof(float));
        }
    }
}

__interrupt void cla1Isr1(void)
{
    cla_task_complete = 1;
    Interrupt_clearACKGroup(INT_myCLA01_INTERRUPT_ACK_GROUP);
}
