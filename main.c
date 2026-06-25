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


float Vinv_k = 0.0f;
float Vinv_avg = 0.0f;
uint16_t Vinv_count = 0;
float X_scale = 1.0f;
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

// NOVO: log bruto dos 4 sinais de chaveamento, para diagnostico do
// caminho EPWM -> GPIO -> Vinv_k. Empacotado em bits: s1|s2<<1|s3<<2|s4<<3
// Precisa de um bloco GS livre no .cmd (ex: ramgs4). Se nao existir,
// ajuste para outro bloco disponivel ou reduza LOG_SIZE de outro buffer.
#pragma DATA_SECTION(log_s, "ramgs8")
uint16_t log_s[LOG_SIZE];



volatile uint16_t dbg_s1 = 0;
volatile uint16_t dbg_s2 = 0;
volatile uint16_t dbg_s3 = 0;
volatile uint16_t dbg_s4 = 0;

uint16_t log_index  = 0;
uint16_t enable_log = 1;  

uint16_t condicao_de_disparo = 0;
uint16_t teste_protecao = 0;
uint16_t reset_trip = 0;  



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
    Vinv_k = 0.0f;
    float V_A = s1 ? VDC : 0.0f;
    float V_B = s3 ? VDC : 0.0f;
    Vinv_k = V_A - V_B;

    // Adicione variável global:
    Vinv_avg += Vinv_k;  
    Vinv_count++;

    
    static uint16_t plant_log_idx = 0;
    if (plant_log_idx < LOG_SIZE)
    {
        log_s[plant_log_idx] = s1 | (s2 << 1) | (s3 << 2) | (s4 << 3);
        plant_log_idx++;
    }

    // Tensao da rede e referencia de corrente (sincronizados)
    float sin_theta = sinf(theta);
    float vg_k = V_PEAK * sin_theta;
    i_ref = X_scale * I_PEAK_BASE * sin_theta;

    theta += THETA_STEP;
    if (theta >= 6.2831853f) theta -= 6.2831853f;

    // Equacao de estado discreta (Tustin)
    float ig_k = (PHI_1 * ig_k1)
               + (PHI_2 * (Vinv_k + Vinv_k1 - vg_k - vg_k1));


    if ((EPWM_getTripZoneFlagStatus(myEPWM1_BASE) & EPWM_TZ_FLAG_OST) != 0)
    {
        ig_k = 0.0f;
    }


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

     

    // Garante que DAC começa em zero (corrente zero = 2048 counts)
    DAC_setShadowValue(myDAC1_BASE, 2048);
    

    // Limpa latch do CMPSS e trip zone após DAC estabilizar
    CMPSS_clearFilterLatchHigh(myCMPSS0_BASE);
    CMPSS_clearFilterLatchLow(myCMPSS0_BASE);
    EPWM_clearTripZoneFlag(myEPWM1_BASE, EPWM_TZ_FLAG_OST);
    EPWM_clearTripZoneFlag(myEPWM2_BASE, EPWM_TZ_FLAG_OST);

    

    

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
            DEVICE_DELAY_US(25000);
            X_scale = 1.0f;     
            condicao_de_disparo = 0;
        }

        if (teste_protecao == 1) 
        {
            
            enable_log = 1;
            log_index = 0;
            DEVICE_DELAY_US(25000);
            X_scale = 1.6f;     
            teste_protecao = 0;
            DEVICE_DELAY_US(25000);
            X_scale = 1.0f;
        }

        if (reset_trip == 1)
        {
            CMPSS_clearFilterLatchHigh(myCMPSS0_BASE);
            CMPSS_clearFilterLatchLow(myCMPSS0_BASE);
            EPWM_clearTripZoneFlag(myEPWM1_BASE, EPWM_TZ_FLAG_OST | EPWM_TZ_FLAG_DCAEVT1);
            EPWM_clearTripZoneFlag(myEPWM2_BASE, EPWM_TZ_FLAG_OST | EPWM_TZ_FLAG_DCAEVT1);
            reset_trip = 0;
            enable_log = 1;
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
        log_vg[log_index] = (Vinv_count > 0) ? (Vinv_avg / (float)Vinv_count) : 0.0f;
        Vinv_avg   = 0.0f;
        Vinv_count = 0;
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