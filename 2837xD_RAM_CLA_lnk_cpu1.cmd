// The user must define CLA_C in the project linker settings if using the
// CLA C compiler
#ifdef CLA_C
CLA_SCRATCHPAD_SIZE = 0x100;
--undef_sym=__cla_scratchpad_end
--undef_sym=__cla_scratchpad_start
#endif

MEMORY
{
PAGE 0 :
   BEGIN           : origin = 0x000000, length = 0x000002
   RAMM0           : origin = 0x000123, length = 0x0002DD
   RAMD0           : origin = 0x00B000, length = 0x000800
   RAMD1           : origin = 0x00B800, length = 0x000800
   RAMLS4          : origin = 0x00A000, length = 0x000800
   RAMLS5          : origin = 0x00A800, length = 0x000800
   RESET           : origin = 0x3FFFC0, length = 0x000002

PAGE 1 :
   BOOT_RSVD       : origin = 0x000002, length = 0x000121
   RAMM1           : origin = 0x000400, length = 0x0003F8
   RAMLS0          : origin = 0x008000, length = 0x000800
   RAMLS1          : origin = 0x008800, length = 0x000800
   RAMLS2          : origin = 0x009000, length = 0x000800
   RAMLS3          : origin = 0x009800, length = 0x000800

   /* Blocos GS dedicados para logs - 1000 floats = 4000 bytes cada.
      Cada bloco RAMGS tem 0x1000 = 4096 words (8192 bytes em C28x
      onde 1 word = 16 bits). Um float ocupa 2 words (32 bits), entao
      1000 floats = 2000 words = 0x7D0. Sobra folga em cada bloco. */
   RAMGS0          : origin = 0x00C000, length = 0x001000  /* log_ig    */
   RAMGS1          : origin = 0x00D000, length = 0x001000  /* log_vg    */
   RAMGS2          : origin = 0x00E000, length = 0x001000  /* log_iref  */
   RAMGS3          : origin = 0x00F000, length = 0x001000  /* log_power */
   RAMGS4_7        : origin = 0x010000, length = 0x004000
   RAMGS8          : origin = 0x014000, length = 0x001000
   RAMGS9          : origin = 0x015000, length = 0x001000
   RAMGS10         : origin = 0x016000, length = 0x001000
   RAMGS11         : origin = 0x017000, length = 0x001000
   RAMGS12         : origin = 0x018000, length = 0x001000
   RAMGS13         : origin = 0x019000, length = 0x001000
   RAMGS14         : origin = 0x01A000, length = 0x001000
   RAMGS15         : origin = 0x01B000, length = 0x000FF8

   EMIF1_CS0n      : origin = 0x80000000, length = 0x10000000
   EMIF1_CS2n      : origin = 0x00100000, length = 0x00200000
   EMIF1_CS3n      : origin = 0x00300000, length = 0x00080000
   EMIF1_CS4n      : origin = 0x00380000, length = 0x00060000
   EMIF2_CS0n      : origin = 0x90000000, length = 0x10000000
   EMIF2_CS2n      : origin = 0x00002000, length = 0x00001000

   CANA_MSG_RAM    : origin = 0x049000, length = 0x000800
   CANB_MSG_RAM    : origin = 0x04B000, length = 0x000800

   CLA1_MSGRAMLOW  : origin = 0x001480, length = 0x000080
   CLA1_MSGRAMHIGH : origin = 0x001500, length = 0x000080
}

SECTIONS
{
   codestart        : > BEGIN,    PAGE = 0
   .text            : > RAMGS4_7, PAGE = 1
   .cinit           : > RAMM0,   PAGE = 0
   .switch          : > RAMM0,   PAGE = 0
   .reset           : > RESET,   PAGE = 0, TYPE = DSECT
   .stack           : > RAMM1,   PAGE = 1

   /* Buffers de log - um bloco GS dedicado por sinal (endereco fixo,
      facil de configurar no Graph Tool do CCS: basta apontar para o
      simbolo do array ou usar o endereco de inicio do bloco GS) */
   ramgs0           : > RAMGS0,  PAGE = 1   /* log_ig    */
   ramgs1           : > RAMGS1,  PAGE = 1   /* log_vg    */
   ramgs2           : > RAMGS2,  PAGE = 1   /* log_iref  */
   ramgs3           : > RAMGS3,  PAGE = 1   /* log_power */
   ramgs8           : > RAMGS8,  PAGE = 1   /* log_power */

#if defined(__TI_EABI__)
   .bss             : > RAMLS2,  PAGE = 1
   .bss:output      : > RAMLS2,  PAGE = 1
   .init_array      : > RAMM0,   PAGE = 0
   .const           : > RAMLS3,  PAGE = 1
   .data           : >> RAMLS2 | RAMLS3, PAGE = 1
   .sysmem          : > RAMLS3,  PAGE = 1
#else
   .pinit           : > RAMM0,   PAGE = 0
   .ebss            : > RAMLS2,  PAGE = 1
   .econst          : > RAMLS3,  PAGE = 1
   .esysmem         : > RAMLS3,  PAGE = 1
#endif

   .em1_cs0         : > EMIF1_CS0n, PAGE = 1
   .em1_cs2         : > EMIF1_CS2n, PAGE = 1
   .em1_cs3         : > EMIF1_CS3n, PAGE = 1
   .em1_cs4         : > EMIF1_CS4n, PAGE = 1
   .em2_cs0         : > EMIF2_CS0n, PAGE = 1
   .em2_cs2         : > EMIF2_CS2n, PAGE = 1

   /* CLA: programa em RAMLS4 (PAGE 0), dados em RAMLS1 (PAGE 1) */
   Cla1Prog         : > RAMLS4,              PAGE = 0
   CLADataLS0       : > RAMLS1,              PAGE = 1
   CLADataLS1       : > RAMLS0,              PAGE = 1

   Cla1ToCpuMsgRAM  : > CLA1_MSGRAMLOW,     PAGE = 1
   CpuToCla1MsgRAM  : > CLA1_MSGRAMHIGH,    PAGE = 1

#ifdef __TI_COMPILER_VERSION__
   #if __TI_COMPILER_VERSION__ >= 15009000
    .TI.ramfunc : {} > RAMM0, PAGE = 0
   #else
    ramfuncs    :    > RAMM0, PAGE = 0
   #endif
#endif

#ifdef CLA_C
   CLAscratch       :
                     { *.obj(CLAscratch)
                     . += CLA_SCRATCHPAD_SIZE;
                     *.obj(CLAscratch_end) } > RAMLS1, PAGE = 1
   .scratchpad      : > RAMLS1, PAGE = 1
   .bss_cla         : > RAMLS1, PAGE = 1
   .const_cla       : > RAMLS1, PAGE = 1
   CLA1mathTables   : > RAMLS1, PAGE = 1
#endif
}
