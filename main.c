#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "scicomm.h"
#include "shared_vars.h"

// Mapeamento explícito nas Message RAMs de acordo com o Linker
#pragma DATA_SECTION(vo, "CpuToCla1MsgRAM");
float vo = 0.0f;

#pragma DATA_SECTION(duty_cycle_cla, "Cla1ToCpuMsgRAM");
float duty_cycle_cla = 0.0f;



// Flag de sincronização entre a CPU e o CLA
volatile uint16_t cla_task_complete = 0;

void main(void)
{
    // Inicialização do sistema e dos periféricos via SysConfig
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Board_init();

    EINT;
    ERTM;

    while (1)
    {
        // Verifica se o pacote de 4 bytes (float) do PLECS chegou à FIFO da SCI
        if (SCI_getRxFIFOStatus(SCI0_BASE) > 3)
        {
            // Recebe a tensão de saída do conversor
            protocolReceiveData(SCI0_BASE, &vo, sizeof(float));

            // Sinaliza o início e força a execução da Task 1 no CLA
            cla_task_complete = 0;
            CLA_forceTasks(myCLA0_BASE, CLA_TASKFLAG_1);

            // Aguarda o término do cálculo pelo CLA
            while (cla_task_complete == 0)
            {
                // Loop de espera ativo até a ISR ser chamada
            }

            // Envia a razão cíclica calculada de volta para o simulador
            protocolSendData(SCI0_BASE, &duty_cycle_cla, sizeof(float));
        }
    }
}

// Interrupção executada quando o CLA finaliza a Task 1
__interrupt void cla1Isr1(void)
{
    cla_task_complete = 1;
    Interrupt_clearACKGroup(INT_myCLA01_INTERRUPT_ACK_GROUP);
}
