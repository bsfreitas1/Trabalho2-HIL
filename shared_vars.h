#ifndef SHARED_VARS_H_
#define SHARED_VARS_H_

// Variáveis de entrada (recebidas do PLECS via SCI)
extern float i_meas;
extern float i_ref;
extern float v_grid_ff;

// Variável de saída (calculada pelo CLA)
extern float duty_cycle_cla;

#endif /* SHARED_VARS_H_ */
