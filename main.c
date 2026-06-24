#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "shared_vars.h"
#include <math.h>

// ==========================================================
// VARIAVEIS COMPARTILHADAS CPU <-> CLA (Message RAM)
// ==========================================================

// CLA escreve, CPU le (Cla1ToCpuMsgRAM = CLA1_MSGRAMLOW)
#pragma DATA_SECTION(i_meas,        "Cla1ToCpuMsgRAM")
float i_meas = 0.0f;

#pragma DATA_SECTION(duty_cycle_cla,"Cla1ToCpuMsgRAM")
float duty_cycle_cla = 0.0f;

#pragma DATA_SECTION(adc_raw,       "Cla1ToCpuMsgRAM")
float adc_raw = 0.0f;

// CPU escreve, CLA le (CpuToCla1MsgRAM = CLA1_MSGRAMHIGH)
#pragma DATA_SECTION(i_ref,         "CpuToCla1MsgRAM")
float i_ref = 0.0f;

#pragma DATA_SECTION(v_grid_ff,     "CpuToCla1MsgRAM")
float v_grid_ff = 0.0f;

#pragma DATA_SECTION(X_scale,       "CpuToCla1MsgRAM")
float X_scale = 1.0f;

#pragma DATA_SECTION(I_PEAK_BASE,   "CpuToCla1MsgRAM")
float I_PEAK_BASE = 12.0f;

// ==========================================================
// PARAMETROS DA PLANTA HIL
// Tustin: L=3mH, R=L*w0/(X/R)=37.7mOhm, Ts=5us
// ==========================================================
#define PHI_1       0.9999371686f
#define PHI_2       0.0004166535f
#define VDC         400.0f
#define V_PEAK      311.13f          // 220 Vrms * sqrt(2)
#define THETA_STEP  0.001884955f     // 2*pi*60*5e-6 (passo a 5 us)
#define DAC_OFFSET  2048.0f
#define DAC_GAIN    102.4f           // +/-20A -> +/-2048 counts

// ==========================================================
// BUFFERS DE LOG
// Cada buffer em um bloco GS dedicado (ramgs0..3), definidos no
// .cmd. Isso garante enderecos fixos e conhecidos, facilitando
// a configuracao do Graph Tool e do Memory Browser do CCS.
//
// Para usar no Graph Tool:
//   Tools -> Graph -> Single Time
//   Start Address: &log_ig  (ou o endereco hex: 0xC000 para GS0)
//   Acquisition Buffer Size: LOG_SIZE (2000)
//   Data Size: 32-bit float
// ==========================================================
#define LOG_SIZE     2000
#define WINDOW_SIZE  250    

#define TRIGGER_DELAY 1000
static uint32_t sample_counter = 0;
static bool triggered = false;

#pragma DATA_SECTION(log_ig,    "ramgs0")
float log_ig[LOG_SIZE];       // Corrente medida ig (A)

#pragma DATA_SECTION(log_vg,    "ramgs1")
float log_vg[LOG_SIZE];       // Tensao da rede vg (V)

#pragma DATA_SECTION(log_iref,  "ramgs2")
float log_iref[LOG_SIZE];     // Referencia de corrente i_ref (A)

#pragma DATA_SECTION(log_power, "ramgs3")
float log_power[LOG_SIZE];    // Potencia ativa media movel (W)
                               // p_media[k] = media(vg*ig) nos ultimos
                               // WINDOW_SIZE amostras

// Estado do log
uint16_t condicao_de_disparo = 0;
uint16_t log_index  = 0;
uint16_t enable_log = 1;  // 1: gravando | 0: buffer cheio (trava)
                           // Para regravar: setar enable_log=1 e log_index=0
                           // no Watch Window do CCS

// ==========================================================
// VARIAVEIS DE ESTADO DA PLANTA
// ==========================================================
float ig_k1   = 0.0f;
float Vinv_k1 = 0.0f;
float vg_k1   = 0.0f;
float theta   = 0.0f;

// Prototipo (definido abaixo do main)
__interrupt void cla1Isr1(void);

// ==========================================================
// ISR DA PLANTA — CPU1, Timer0, 5 us
// ==========================================================
__interrupt void INT_myCPUTIMER0_ISR(void)
{
    
    // Le sinais de chaveamento (saidas do ePWM -> GPIOs 6-9)
    uint16_t s1 = GPIO_readPin(6);
    uint16_t s2 = GPIO_readPin(7);
    uint16_t s3 = GPIO_readPin(8);
    uint16_t s4 = GPIO_readPin(9);

    // Tensao de saida do inversor (modelo de chaves ideais)
    float Vinv_k = 0.0f;
    if      (s1 && s4) Vinv_k =  VDC;
    else if (s2 && s3) Vinv_k = -VDC;

    // Tensao da rede e referencia de corrente (sincronizados)
    float sin_theta = sinf(theta);
    float vg_k = V_PEAK * sin_theta;
    i_ref = X_scale * I_PEAK_BASE * sin_theta;

    theta += THETA_STEP;
    if (theta >= 6.2831853f) theta -= 6.2831853f;

    // Equacao de estado discreta (Tustin)
    float ig_k = (PHI_1 * ig_k1)
               + (PHI_2 * (Vinv_k + Vinv_k1 - vg_k - vg_k1));

    // Publica tensao da rede para feed-forward na CLA
    v_grid_ff = vg_k;

    // Converte corrente para DAC e atualiza saida
    // DAC -> ADCINA0 internamente (sem jumper externo)
    uint16_t dac_val = (uint16_t)(ig_k * DAC_GAIN + DAC_OFFSET);
    if (dac_val > 4095) dac_val = 4095;
    DAC_setShadowValue(myDAC1_BASE, dac_val);

    // Atualiza estados
    ig_k1   = ig_k;
    Vinv_k1 = Vinv_k;
    vg_k1   = vg_k;

    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

// ==========================================================
// MAIN
// ==========================================================
void main(void)
{
    Device_init();
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Board_init();

    // Parametros iniciais (Cenario 1: 1 pu de potencia ativa)
    X_scale     = 1.0f;
    I_PEAK_BASE = 12.5f;  // Ipk nominal = Pn/(Vg_rms) * sqrt(2) = 2000/220 * 1.414 = 12.86 A

    EINT;
    ERTM;

    while (1)
    {
        
        if (condicao_de_disparo == 1) 
        {
            enable_log = 1;
            log_index = 0;
            DEVICE_DELAY_US(25000);
            X_scale = 0.5f;     
            condicao_de_disparo = 0;
        }
        
    }
}

// ==========================================================
// ISR DE FIM DE TASK DA CLA — executada a 20 kHz
// Responsavel pelo logging dos 4 sinais.
// Calculo de potencia media movel feito aqui (CPU1) para nao
// sobrecarregar a CLA com variaveis estaticas extras.
// ==========================================================
__interrupt void cla1Isr1(void)
{
    if (enable_log)
    {
        // Estado da media movel de potencia
        static float power_window[WINDOW_SIZE] = {0.0f};
        static float running_sum               =  0.0f;
        static uint16_t win_idx               =  0;

        // Potencia instantanea
        float p_inst = v_grid_ff * i_meas;

        // Atualiza janela deslizante (O(1), sem loop)
        running_sum -= power_window[win_idx];
        power_window[win_idx] = p_inst;
        running_sum += p_inst;
        win_idx++;
        if (win_idx >= WINDOW_SIZE) win_idx = 0;

        // Grava no buffer de log
        log_ig   [log_index] = i_meas;
        log_vg   [log_index] = v_grid_ff;
        log_iref [log_index] = i_ref;
        log_power[log_index] = running_sum / (float)WINDOW_SIZE;

        log_index++;
        if (log_index >= LOG_SIZE)
        {
            log_index  = 0;
            enable_log = 0;  // Trava apos preencher o buffer
            // Para regravar: no Watch Window, setar enable_log=1
        }
    }

        
    GPIO_togglePin(PINO_DEBUG_FREQ_CLA);

    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP11);
}