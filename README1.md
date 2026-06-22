# Exemplo de Integração CPU-CLA no TMS320F28379D

Este projeto demonstra um exemplo prático de configuração, alocação de memória e troca de dados entre a CPU principal (CPU1) e o Control Law Accelerator (CLA) no microcontrolador C2000 TMS320F28379D. Além da comunicação básica utilizando a abordagem de arquivos do Linker (`.cmd`) e o framework **DriverLib**, o projeto exemplifica a integração e o uso da biblioteca **CLAmath** para execução de funções trigonométricas nativas no coprocessador.

## Funcionamento do Exemplo

O fluxo básico de processamento consiste em:

1. **CPU1** escreve um valor flutuante na variável de entrada (`fVal`) localizada na RAM de mensagem de CPU para CLA (`CpuToCla1MsgRAM`).
2. **CPU1** força o disparo da **Task 1** do CLA através da função `CLA_forceTasks(myCLA0_BASE, CLA_TASKFLAG_1)`.
3. O **CLA** processa a `Cla1Task1()`, realizando o cálculo trigonométrico via biblioteca CLAmath (ex: `CLAsin`), executa um loop de atraso e salva o resultado na variável `fResult` dentro da RAM de mensagem oposta (`Cla1ToCpuMsgRAM`).
4. Ao encerrar a tarefa, a interrupção de fim de tarefa do CLA (`cla1Isr1`) é gerada na CPU1 para ler o resultado de volta.

## Estrutura de Memória Compartilhada

Para que as variáveis globais sejam acessadas por ambos os núcleos, elas são explicitamente mapeadas em seções específicas de memória de mensagem (Message RAMs) através de pragmas no arquivo `main.c`:

```c
#pragma DATA_SECTION(fVal,"CpuToCla1MsgRAM");
float fVal;

#pragma DATA_SECTION(fResult,"Cla1ToCpuMsgRAM");
float fResult;

```

## Alinhamento: Hardware (memcfg) vs Linker (.cmd)

A configuração correta do ecossistema do CLA exige consistência absoluta entre o controle de acesso de hardware e o mapa de alocação de software.

### 1. Configuração de Hardware (`main.c` / SysConfig memcfg)

Neste projeto, o bloco de memória **RAMLS0** está configurado para uso **exclusivo da CPU**.
Apenas o bloco **RAMLS1** foi repassado para o CLA para atuar como memória de dados local. Isso permite que o CLA acesse variáveis locais, tabelas de consulta matemáticas e hospede a pilha do compilador.

### 2. Mapeamento no Linker (`2837xD_RAM_CLA_lnk_cpu1.cmd`)

Para manter a coerência com o hardware e evitar inconsistências na compilação, as seções genéricas de dados do CLA foram consolidadas e direcionadas exclusivamente para a **RAMLS1**:

```linker
SECTIONS
{
    /* CLA específico mapeado em conformidade com o memcfg */
    Cla1Prog         : > RAMLS4,           PAGE = 0
    CLADataLS0       : > RAMLS1,           PAGE = 1
    CLADataLS1       : > RAMLS1,           PAGE = 1

    Cla1ToCpuMsgRAM  : > CLA1_MSGRAMLOW,   PAGE = 1
    CpuToCla1MsgRAM  : > CLA1_MSGRAMHIGH,  PAGE = 1
}

```

## Integração com a Biblioteca Matemática (CLAmath)

O uso de funções trigonométricas no CLA (como `CLAsin` ou `CLAcos`) exige configurações adicionais para garantir que as tabelas de interpolação sejam acessíveis:

1. **Mapeamento das Tabelas (Lookup Tables):** O Linker foi configurado para alocar a seção `CLA1mathTables` (obrigatoriamente grafada com "m" minúsculo e "s" no final) dentro da RAM acessível ao CLA (`RAMLS1`). Se essa seção não estiver devidamente declarada no `.cmd` ou ficar inacessível, as funções matemáticas retornarão `0.0`.
```linker
/* Dentro da condicional CLA_C no arquivo .cmd */
CLA1mathTables   : > RAMLS1,       PAGE = 1

```


2. **Precisão de Ponto Flutuante (32-bits):** A arquitetura do CLA é uma FPU de 32-bits e não suporta o tipo `double` (64-bits) nativamente. Todas as constantes decimais passadas para as funções matemáticas no código (`.cla`) devem ser explicitamente tipadas como `float` utilizando o sufixo `f` (ex: `0.5f * 3.14f`), evitando cálculos errôneos ou acesso a dados lixo na memória.

## Requisito do Compilador CLA C

O gerenciamento da variável local `i` do loop `for` dentro da tarefa do CLA exige o uso da seção `.scratchpad`. Para que o tamanho e os símbolos dessa área sejam resolvidos corretamente, a flag **`CLA_C`** deve estar explicitamente definida nas propriedades do projeto:

`Project Properties -> C2000 Linker -> Advanced Options -> Command File Preprocessing -> --define=CLA_C`

Isso ativa a condicional necessária dentro do arquivo `.cmd`:

```linker
#ifdef CLA_C
   CLA_SCRATCHPAD_SIZE = 0x100;
   .scratchpad      : > RAMLS1,       PAGE = 1
   .bss_cla         : > RAMLS1,       PAGE = 1
   .const_cla       : > RAMLS1,       PAGE = 1
   CLA1mathTables   : > RAMLS1,       PAGE = 1
#endif

```