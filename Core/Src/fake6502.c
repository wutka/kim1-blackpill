/* Fake6502 CPU emulator core v1.1 *******************
 * (c)2011 Mike Chambers (miker00lz@gmail.com)       *
 *****************************************************
 * v1.1 - Small bugfix in BIT opcode, but it was the *
 *        difference between a few games in my NES   *
 *        emulator working and being broken!         *
 *        I went through the rest carefully again    *
 *        after fixing it just to make sure I didn't *
 *        have any other typos! (Dec. 17, 2011)      *
 *                                                   *
 * v1.0 - First release (Nov. 24, 2011)              *
 *****************************************************
 * LICENSE: This source code is released into the    *
 * public domain, but if you use it please do give   *
 * credit. I put a lot of effort into writing this!  *
 *                                                   *
 *****************************************************
 * Fake6502 is a MOS Technology 6502 CPU emulation   *
 * engine in C. It was written as part of a Nintendo *
 * Entertainment System emulator I've been writing.  *
 *                                                   *
 * A couple important things to know about are two   *
 * defines in the code. One is "UNDOCUMENTED" which, *
 * when defined, allows Fake6502 to compile with     *
 * full support for the more predictable             *
 * undocumented instructions of the 6502. If it is   *
 * undefined, undocumented opcodes just act as NOPs. *
 *                                                   *
 * The other define is "NES_CPU", which causes the   *
 * code to compile without support for binary-coded  *
 * decimal (BCD) support for the ADC and SBC         *
 * opcodes. The Ricoh 2A03 CPU in the NES does not   *
 * support BCD, but is otherwise identical to the    *
 * standard MOS 6502. (Note that this define is      *
 * enabled in this file if you haven't changed it    *
 * yourself. If you're not emulating a NES, you      *
 * should comment it out.)                           *
 *                                                   *
 * If you do discover an error in timing accuracy,   *
 * or operation in general please e-mail me at the   *
 * address above so that I can fix it. Thank you!    *
 *                                                   *
 *****************************************************
 * Usage:                                            *
 *                                                   *
 * Fake6502 requires you to provide two external     *
 * functions:                                        *
 *                                                   *
 * uint8_t read6502(uint16_t address)                *
 * void write6502(uint16_t address, uint8_t value)   *
 *                                                   *
 * You may optionally pass Fake6502 the pointer to a *
 * function which you want to be called after every  *
 * emulated instruction. This function should be a   *
 * void with no parameters expected to be passed to  *
 * it.                                               *
 *                                                   *
 * This can be very useful. For example, in a NES    *
 * emulator, you check the number of clock ticks     *
 * that have passed so you can know when to handle   *
 * APU events.                                       *
 *                                                   *
 * To pass Fake6502 this pointer, use the            *
 * hookexternal(void *funcptr) function provided.    *
 *                                                   *
 * To disable the hook later, pass NULL to it.       *
 *****************************************************
 * Useful functions in this emulator:                *
 *                                                   *
 * void reset6502()                                  *
 *   - Call this once before you begin execution.    *
 *                                                   *
 * void exec6502(uint32_t tickcount)                 *
 *   - Execute 6502 code up to the next specified    *
 *     count of clock ticks.                         *
 *                                                   *
 * void step6502()                                   *
 *   - Execute a single instrution.                  *
 *                                                   *
 * void irq6502()                                    *
 *   - Trigger a hardware IRQ in the 6502 core.      *
 *                                                   *
 * void nmi6502()                                    *
 *   - Trigger an NMI in the 6502 core.              *
 *                                                   *
 * void hookexternal(void *funcptr)                  *
 *   - Pass a pointer to a void function taking no   *
 *     parameters. This will cause Fake6502 to call  *
 *     that function once after each emulated        *
 *     instruction.                                  *
 *                                                   *
 *****************************************************
 * Useful variables in this emulator:                *
 *                                                   *
 * uint32_t clockticks6502                           *
 *   - A running total of the emulated cycle count.  *
 *                                                   *
 * uint32_t instructions                             *
 *   - A running total of the total emulated         *
 *     instruction count. This is not related to     *
 *     clock cycle timing.                           *
 *                                                   *
 *****************************************************/

#include <stdio.h>
#include <stdint.h>

//6502 defines
#define UNDOCUMENTED //when this is defined, undocumented opcodes are handled.
                     //otherwise, they're simply treated as NOPs.

#define NES_CPU      //when this is defined, the binary-coded decimal (BCD)
                     //status flag is not honored by ADC and SBC. the 2A03
                     //CPU in the Nintendo Entertainment System does not
                     //support BCD operation.

#define FLAG_CARRY     0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define SAVEACCUM(x) a = (uint8_t)((x) & 0x00FF)


//flag modifier macros
#define setcarry() status |= FLAG_CARRY
#define clearcarry() status &= (~FLAG_CARRY)
#define setzero() status |= FLAG_ZERO
#define clearzero() status &= (~FLAG_ZERO)
#define setinterrupt() status |= FLAG_INTERRUPT
#define clearinterrupt() status &= (~FLAG_INTERRUPT)
#define setdecimal() status |= FLAG_DECIMAL
#define cleardecimal() status &= (~FLAG_DECIMAL)
#define setoverflow() status |= FLAG_OVERFLOW
#define clearoverflow() status &= (~FLAG_OVERFLOW)
#define setsign() status |= FLAG_SIGN
#define clearsign() status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc(n) {\
    if ((n) & 0x00FF) clearzero();\
        else setzero();\
}

#define signcalc(n) {\
    if ((n) & 0x0080) setsign();\
        else clearsign();\
}

#define carrycalc(n) {\
    if ((n) & 0xFF00) setcarry();\
        else clearcarry();\
}

#define overflowcalc(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n) ^ (uint16_t)(m)) & ((n) ^ (o)) & 0x0080) setoverflow();\
        else clearoverflow();\
}


//6502 CPU registers
#ifdef GLOBAL_REGISTERS
register uint16_t pc asm ("r6");
register uint8_t a asm ("r7");
register uint8_t x asm ("r8");
register uint8_t y asm ("r9");
register uint8_t status asm ("r10");
uint8_t sp;

#else
uint16_t pc;
uint8_t sp, a, x, y, status;
#endif


//externally supplied functions
extern uint8_t read6502(uint16_t address);
extern void write6502(uint16_t address, uint8_t value);

//a few general functions used by various other functions
void push16(uint16_t pushval) {
    write6502(BASE_STACK + sp, (pushval >> 8) & 0xFF);
    write6502(BASE_STACK + ((sp - 1) & 0xFF), pushval & 0xFF);
    sp -= 2;
}

void push8(uint8_t pushval) {
    write6502(BASE_STACK + sp--, pushval);
}

uint16_t pull16() {
    uint16_t temp16;
    temp16 = read6502(BASE_STACK + ((sp + 1) & 0xFF)) | ((uint16_t)read6502(BASE_STACK + ((sp + 2) & 0xFF)) << 8);
    sp += 2;
    return(temp16);
}

uint8_t pull8() {
    return (read6502(BASE_STACK + ++sp));
}

void reset6502() {
    pc = (uint16_t)read6502(0xFFFC) | ((uint16_t)read6502(0xFFFD) << 8);
    a = 0;
    x = 0;
    y = 0;
    sp = 0xFD;
}


static void (*optable[256])();

//addressing mode functions, calculates effective addresses

#define IMM uint16_t ea = pc++
#define ZP uint16_t ea = (uint16_t)read6502((uint16_t)pc++)
#define ZPX uint16_t ea = ((uint16_t)read6502((uint16_t)pc++) + (uint16_t)x) & 0xFF
#define ZPY uint16_t ea = ((uint16_t)read6502((uint16_t)pc++) + (uint16_t)y) & 0xFF
#define REL uint16_t reladdr = (uint16_t)read6502(pc++); if (reladdr & 0x80) reladdr |= 0xFF00
#define ABSO uint16_t ea = (uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8); pc += 2
#define ABSX  uint16_t ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8)); ea += (uint16_t)x; pc += 2
#define ABSXNP uint16_t ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8)); ea += (uint16_t)x; pc += 2
#define ABSY uint16_t ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8)); ea += (uint16_t)y; pc += 2
#define ABSYNP uint16_t ea = ((uint16_t)read6502(pc) | ((uint16_t)read6502(pc+1) << 8)); ea += (uint16_t)y; pc += 2
#define IND uint16_t eahelp, eahelp2; eahelp = (uint16_t)read6502(pc) | (uint16_t)((uint16_t)read6502(pc+1) << 8); eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); uint16_t ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp2) << 8); pc += 2
#define INDX uint16_t eahelp; eahelp = (uint16_t)(((uint16_t)read6502(pc++) + (uint16_t)x) & 0xFF); uint16_t ea = (uint16_t)read6502(eahelp & 0x00FF) | ((uint16_t)read6502((eahelp+1) & 0x00FF) << 8)
#define INDY uint16_t eahelp, eahelp2; eahelp = (uint16_t)read6502(pc++); eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); uint16_t ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp2) << 8); ea += (uint16_t)y
#define INDYNP uint16_t eahelp, eahelp2; eahelp = (uint16_t)read6502(pc++); eahelp2 = (eahelp & 0xFF00) | ((eahelp + 1) & 0x00FF); uint16_t ea = (uint16_t)read6502(eahelp) | ((uint16_t)read6502(eahelp2) << 8); ea += (uint16_t)y

#define GETVALUE (uint16_t)read6502(ea)
#define GETVALUE16 (uint16_t)read6502(ea) (uint16_t)read6502(ea) | ((uint16_t)read6502(ea+1) << 8)
#define PUTVALUE(x) write6502(ea, (x & 0xff))

//instruction handler functions

static void adc_abso() {  // 0x6d
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_absx() {  // 0x7d
    ABSX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_absy() {  // 0x79
    ABSY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_imm() {  // 0x69
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_indx() {  // 0x61
    INDX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_indy() {  // 0x71
    INDY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_zp() {  // 0x65
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void adc_zpx() {  // 0x75
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);

    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);
    
    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void and_abso() {  // 0x2d
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_absx() {  // 0x3d
    ABSX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_absy() {  // 0x39
    ABSY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_imm() {  // 0x29
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_indx() {  // 0x21
    INDX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_indy() {  // 0x31
    INDY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_zp() {  // 0x25
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void and_zpx() {  // 0x35
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void asl_acc() {  // 0x0a
    uint8_t value = a;
    uint16_t result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void asl_abso() {  // 0x0e
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void asl_absx() {  // 0x1e
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void asl_zp() {  // 0x06
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void asl_zpx() {  // 0x16
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = value << 1;

    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void bcc_rel() {
    REL;
    if ((status & FLAG_CARRY) == 0) {
        pc += reladdr;
    }
}

static void bcs_rel() {  // 0x90
    REL;
    if ((status & FLAG_CARRY) == FLAG_CARRY) {
        pc += reladdr;
    }
}

static void beq_rel() {  // 0xf0
    REL;
    if ((status & FLAG_ZERO) == FLAG_ZERO) {
        pc += reladdr;
    }
}

static void bit_abso() {  // 0x2c
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bit_zp() {  // 0x24
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a & value;
   
    zerocalc(result);
    status = (status & 0x3F) | (uint8_t)(value & 0xC0);
}

static void bmi_rel() {  // 0x30
    REL;
    if ((status & FLAG_SIGN) == FLAG_SIGN) {
        pc += reladdr;
    }
}

static void bne_rel() {  // 0xd0
    REL;
    if ((status & FLAG_ZERO) == 0) {
        pc += reladdr;
    }
}

static void bpl_rel() {  // 0x10
    REL;
    if ((status & FLAG_SIGN) == 0) {
        pc += reladdr;
    }
}

static void brk() {  // 0x00
    pc++;
    push16(pc); //push next instruction address onto stack
    push8(status | FLAG_BREAK); //push CPU status to stack
    setinterrupt(); //set interrupt flag
    pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
}

static void bvc_rel() {  // 0x50
    REL;
    if ((status & FLAG_OVERFLOW) == 0) {
        pc += reladdr;
    }
}

static void bvs_rel() {  // 0x70
    REL;
    if ((status & FLAG_OVERFLOW) == FLAG_OVERFLOW) {
        pc += reladdr;
    }
}

static void clc() {  // 0x18
    clearcarry();
}

static void cld() {  // 0xd8
    cleardecimal();
}

static void cli() {  // 0x58
    clearinterrupt();
}

static void clv() {  // 0xb8
    clearoverflow();
}

static void cmp_abso() {  // 0xcd
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_absx() {  // 0xdd
    ABSX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_absy() {  // 0xd9
    ABSY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_imm() {  // 0xc9
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_indx() {  // 0xc1
    INDX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_indy() {  // 0xd1
    INDY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_zp() {  // 0xc5
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cmp_zpx() {  // 0xd5
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a - value;
   
    if (a >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (a == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx_abso() {  // 0xec
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)x - value;
   
    if (x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx_imm() {  // 0xe0
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)x - value;
   
    if (x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpx_zp() {  // 0xe4
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)x - value;
   
    if (x >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (x == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy_abso() {  // 0xcc
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)y - value;
   
    if (y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy_imm() {  // 0xc0
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)y - value;
   
    if (y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void cpy_zp() {  // 0xc4
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)y - value;
   
    if (y >= (uint8_t)(value & 0x00FF)) setcarry();
        else clearcarry();
    if (y == (uint8_t)(value & 0x00FF)) setzero();
        else clearzero();
    signcalc(result);
}

static void dec_abso() {  // 0xce
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void dec_absx() {  // 0xde
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void dec_zp() {  // 0xc6
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void dec_zpx() {  // 0xd6
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = value - 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void dex() {  // 0xca
    x--;
   
    zerocalc(x);
    signcalc(x);
}

static void dey() {  // 0x88
    y--;
   
    zerocalc(y);
    signcalc(y);
}

static void eor_abso() {  // 0x4d
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_absx() {  // 0x5d
    ABSX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_absy() {  // 0x59
    ABSY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_imm() {  // 0x49
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_indx() {  // 0x41
    INDX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_indy() {  // 0x51
    INDY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_zp() {  // 0x45
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void eor_zpx() {  // 0x55
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a ^ value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void inc_abso() {  // 0xee
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void inc_absx() {  // 0xfe
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void inc_zp() {  // 0xe6
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void inc_zpx() {  // 0xf6
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = value + 1;
   
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void inx() {  // 0xe8
    x++;
   
    zerocalc(x);
    signcalc(x);
}

static void iny() {  // 0xc8
    y++;
   
    zerocalc(y);
    signcalc(y);
}

static void jmp_abso() {  // 0xc4
    ABSO;
    pc = ea;
}

static void jmp_ind() {  // 0xc6
    IND;
    pc = ea;
}

static void jsr_abso() {  // 0x20
    ABSO;
    push16(pc - 1);
    pc = ea;
}

static void lda_abso() {  // 0xad
    ABSO;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_absx() {  // 0xbd
    ABSX;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_absy() {  // 0xb9
    ABSY;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_imm() {  // 0xa9
    IMM;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_indx() {  // 0xa1
    INDX;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_indy() {  // 0xb1
    INDY;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_zp() {  // 0xa5
    ZP;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void lda_zpx() {  // 0xb5
    ZPX;
    uint8_t value = GETVALUE;
    a = (uint8_t)(value & 0x00FF);
   
    zerocalc(a);
    signcalc(a);
}

static void ldx_abso() {  // 0xae
    ABSO;
    uint8_t value = GETVALUE;
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldx_absy() {  // 0xbe
    ABSY;
    uint8_t value = GETVALUE;
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldx_imm() {  // 0xa2
    IMM;
    uint8_t value = GETVALUE;
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldx_zp() {  // 0xa6
    ZP;
    uint8_t value = GETVALUE;
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldx_zpx() {  // 0xb6
    ZPX;
    uint8_t value = GETVALUE;
    x = (uint8_t)(value & 0x00FF);
   
    zerocalc(x);
    signcalc(x);
}

static void ldy_abso() {  // 0xac
    ABSO;
    uint8_t value = GETVALUE;
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void ldy_absx() {  // 0xbc
    ABSX;
    uint8_t value = GETVALUE;
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void ldy_imm() {  // 0xa0
    IMM;
    uint8_t value = GETVALUE;
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void ldy_zp() {  // 0xa4
    ZP;
    uint8_t value = GETVALUE;
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void ldy_zpx() {  // 0xb4
    ZPX;
    uint8_t value = GETVALUE;
    y = (uint8_t)(value & 0x00FF);
   
    zerocalc(y);
    signcalc(y);
}

static void lsr_abso() {  // 0x4e
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void lsr_absx() {  // 0x5e
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void lsr_acc() {  // 0x4a
    uint16_t result = a >> 1;
   
    if (a & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void lsr_zp() {  // 0x46
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void lsr_zpx() {  // 0x56
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = value >> 1;
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void nop() {
}

static void nop_abso() {
    pc += 2;
}

static void nop_absx() {
    pc += 2;
}

static void nop_absx_np() {
    pc += 2;
}

static void nop_absy() {
    pc += 2;
}

static void nop_imm() {
    pc++;
}

static void nop_indy() {
    pc++;
}

static void nop_zp() {
    pc++;
}

static void nop_zpx() {
    pc++;
}

static void ora_abso() {  // 0x0d
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_absx() {  // 0x1d
    ABSX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_absy() {  // 0x19
    ABSY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_imm() {  // 0x09
    IMM;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_indx() {  // 0x01
    INDX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_indy() {  // 0x02
    INDY;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_zp() {  // 0x05
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ora_zpx() {  // 0x15
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (uint16_t)a | value;
   
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void pha() {  // 0x48
    push8(a);
}

static void php() {  // 0x08
    push8(status | FLAG_BREAK);
}

static void pla() {  // 0x68
    a = pull8();
   
    zerocalc(a);
    signcalc(a);
}

static void plp() {  // 0x28
    status = pull8();
}

static void rol_acc() {  // 0x2a
    uint16_t result = (a << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void rol_abso() {  // 0x2e
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void rol_absx() {  // 0x3e
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void rol_zp() {  // 0x26
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void rol_zpx() {  // 0x36
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (value << 1) | (status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void ror_acc() {  // 0x6a
    uint16_t result = (a >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (a & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    SAVEACCUM(result);
}

static void ror_abso() {  // 0x6e
    ABSO;
    uint8_t value = GETVALUE;
    uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void ror_absx() {  // 0x7e
    ABSXNP;
    uint8_t value = GETVALUE;
    uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void ror_zp() {  // 0x66
    ZP;
    uint8_t value = GETVALUE;
    uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void ror_zpx() {  // 0x76
    ZPX;
    uint8_t value = GETVALUE;
    uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
    if (value & 1) setcarry();
        else clearcarry();
    zerocalc(result);
    signcalc(result);
   
    PUTVALUE(result);
}

static void rti() {  // 0x40
    status = pull8();
    pc = pull16();
}

static void rts() {  // 0x60
    pc = pull16() + 1;
}

static void sbc_abso() {  // 0xed
    ABSO;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_absx() {  // 0xfd
    ABSX;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_absy() {  // 0xf9
    ABSY;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_imm() {  // 0xe9
    IMM;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_indx() {  // 0xe1
    INDX;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_indy() {  // 0xf1
    INDY;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_zp() {  // 0xe5
    ZP;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sbc_zpx() {  // 0xf5
    ZPX;
    uint8_t value = 0x00ff ^ GETVALUE;
    uint16_t result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
    carrycalc(result);
    zerocalc(result);
    overflowcalc(result, a, value);
    signcalc(result);

    #ifndef NES_CPU
    if (status & FLAG_DECIMAL) {
        clearcarry();
        
        a -= 0x66;
        if ((a & 0x0F) > 0x09) {
            a += 0x06;
        }
        if ((a & 0xF0) > 0x90) {
            a += 0x60;
            setcarry();
        }
    }
    #endif
   
    SAVEACCUM(result);
}

static void sec() {  // 0x38
    setcarry();
}

static void sed() {  // 0xf8
    setdecimal();
}

static void sei() {  // 0x78
    setinterrupt();
}

static void sta_abso() {  // 0x8d
    ABSO;
    PUTVALUE(a);
}

static void sta_absx() {  // 0x9d
    ABSXNP;
    PUTVALUE(a);
}

static void sta_absy() {  // 0x99
    ABSYNP;
    PUTVALUE(a);
}

static void sta_indx() {  // 0x81
    INDX;
    PUTVALUE(a);
}

static void sta_indy() {  // 0x91
    INDYNP;
    PUTVALUE(a);
}

static void sta_zp() {  // 0x85
    ZP;
    PUTVALUE(a);
}

static void sta_zpx() {  // 0x95
    ZPX;
    PUTVALUE(a);
}

static void stx_abso() {  // 0x8e
    ABSO;
    PUTVALUE(x);
}

static void stx_zp() {  // 0x86
    ZP;
    PUTVALUE(x);
}

static void stx_zpx() {  // 0x96
    ZPX;
    PUTVALUE(x);
}

static void sty_abso() { // 0x8c
    ABSO;
    PUTVALUE(y);
}

static void sty_zp() { // 0x84
    ZP;
    PUTVALUE(y);
}

static void sty_zpx() { // 0x94
    ZPX;
    PUTVALUE(y);
}

static void tax() {  // 0xaa
    x = a;
   
    zerocalc(x);
    signcalc(x);
}

static void tay() {  // 0xa8
    y = a;
   
    zerocalc(y);
    signcalc(y);
}

static void tsx() {  // 0xba
    x = sp;
   
    zerocalc(x);
    signcalc(x);
}

static void txa() {  // 0x8a
    a = x;
   
    zerocalc(a);
    signcalc(a);
}

static void txs() {  // 0x9a
    sp = x;
}

static void tya() {  // 0x98
    a = y;
   
    zerocalc(a);
    signcalc(a);
}

//undocumented instructions
#ifdef UNDOCUMENTED
    static void lax_abso() { //  0xaf
        ABSO;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_absx() { //  0xbf
        ABSXNP;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_absy() { //  0xbb
        ABSYNP;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_indx() { //  0xa3
        INDX;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_indy() { //  0xb3
        INDYNP;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_zp() { //  0xa7
        ZP;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void lax_zpx() { //  0xb7
        ZPX;
        uint8_t value = GETVALUE;
        a = (uint8_t)(value & 0x00FF);
        x = (uint8_t)(value & 0x00FF);
   
        zerocalc(x);
        signcalc(x);
    }

    static void sax_abso() {  // 0x8f
        ABSO;
        PUTVALUE(a & x);
    }

    static void sax_indx() {  // 0x83
        INDX;
        PUTVALUE(a & x);
    }

    static void sax_zp() {  // 0x87
        ZP;
        PUTVALUE(a & x);
    }

    static void sax_zpx() {  // 0x97
        ZPX;
        PUTVALUE(a & x);
    }

    static void dcp_abso() {  // 0xcf
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_absx() {  // 0xdf
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_absy() {  // 0xdb
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_indx() {  // 0xc3
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_indy() {  // 0xd3
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_zp() {  // 0xc7
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void dcp_zpx() {  // 0xd7
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = value - 1;
        PUTVALUE(result);
        result = (uint16_t)a - value;
   
        if (a >= (uint8_t)(value & 0x00FF)) setcarry();
            else clearcarry();
        if (a == (uint8_t)(value & 0x00FF)) setzero();
            else clearzero();
        signcalc(result);
    }

    static void isb_abso() {  // 0xef
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_absx() {  // 0xff
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_absy() {  // 0xfb
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_indx() {  // 0xe3
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_indy() {  // 0xf3
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_zp() {  // 0xe7
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void isb_zpx() {  // 0xf7
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = value + 1;
   
        value = 0x00ff ^ value;
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
   
        carrycalc(result);
        zerocalc(result);
        overflowcalc(result, a, value);
        signcalc(result);

        #ifndef NES_CPU
        if (status & FLAG_DECIMAL) {
            clearcarry();
        
            a -= 0x66;
            if ((a & 0x0F) > 0x09) {
                a += 0x06;
            }
            if ((a & 0xF0) > 0x90) {
                a += 0x60;
                setcarry();
            }
        }
        #endif
   
        SAVEACCUM(result);
    }

    static void slo_abso() {  // 0x0f
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_absx() {  // 0x1f
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_absy() {  // 0x1b
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_indx() {  // 0x03
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_indy() {  // 0x13
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_zp() {  // 0x07
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void slo_zpx() {  // 0x17
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = a | (value << 1);

        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_abso() {  // 0x2f
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_absx() {  // 0x3f
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_absy() {  // 0x3b
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_indx() {  // 0x23
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_indy() {  // 0x33
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_zp() {  // 0x27
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rla_zpx() {  // 0x37
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = a & ((value << 1) | (status & FLAG_CARRY));
   
        carrycalc(result);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_abso() {  // 0x4f
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_absx() {  // 0x5f
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_absy() {  // 0x5b
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_indx() {  // 0x43
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_indy() {  // 0x53
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_zp() {  // 0x47
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void sre_zpx() {  // 0x57
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = a ^ (value >> 1);
   
        if (value & 1) setcarry();
            else clearcarry();
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_abso() {  // 0x6f
        ABSO;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_absx() {  // 0x7f
        ABSX;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_absy() {  // 0x7b
        ABSY;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_indx() {  // 0x63
        INDX;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_indy() {  // 0x73
        INDY;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_zp() {  // 0x67
        ZP;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

    static void rra_zpx() {  // 0x77
        ZPX;
        uint8_t value = GETVALUE;
        uint16_t result = (value >> 1) | ((status & FLAG_CARRY) << 7);
   
        if (value & 1) setcarry();
            else clearcarry();
                
        result = (uint16_t)a + value + (uint16_t)(status & FLAG_CARRY);
        zerocalc(result);
        signcalc(result);
   
        SAVEACCUM(result);
    }

#else
    #define lax nop
    #define sax nop
    #define dcp nop
    #define isb nop
    #define slo nop
    #define rla nop
    #define sre nop
    #define rra nop
#endif


static void (*optable[256])() = {
/*          |    0    |    1    |    2    |    3    |    4    |    5    |    6    |    7    |    8    |    9    |    A    |    B    |    C    |    D    |    E    |    F    |        */
/* 0 */      brk,       ora_indx,  nop,    slo_indx,  nop_zp,   ora_zp,  asl_zp,    slo_zp,    php,     ora_imm,  asl_acc,  nop_imm, nop_abso, ora_abso, asl_abso, slo_abso, /* 0 */
/* 1 */      bpl_rel,   ora_indy,  nop,    slo_indy,  nop_zpx,  ora_zpx, asl_zpx,   slo_zpx,   clc,     ora_absy, nop,     slo_absy, nop_absx, ora_absx, asl_absx, slo_absx, /* 1 */
/* 2 */      jsr_abso,  and_indx,  nop,    rla_indx,  bit_zp,   and_zp,  rol_zp,    rla_zp,    plp,     and_imm,  rol_acc,  nop_imm, bit_abso, and_abso, rol_abso, rla_abso, /* 2 */


/* 3 */      bmi_rel,   and_indy,  nop,    rla_indy,  nop_zpx,  and_zpx, rol_zpx,   rla_zpx,   sec,     and_absy,  nop,     rla_absy, nop_absx,and_absx, rol_absx, rla_absx, /* 3 */
/* 4 */      rti,       eor_indx,  nop,    sre_indx,  nop_zp,   eor_zp,  lsr_zp,    sre_zp,    pha,     eor_imm,  lsr_acc,  nop_imm,  jmp_abso, eor_abso, lsr_abso, sre_abso, /* 4 */
/* 5 */      bvc_rel,   eor_indy,  nop,    sre_indy,  nop_zpx,  eor_zpx, lsr_zpx,   sre_zpx,   cli,     eor_absy, nop,      sre_absy, nop_absx, eor_absx, lsr_absx, sre_absx, /* 5 */
/* 6 */      rts,       adc_indx,  nop,    rra_indx,  nop_zp,   adc_zp,  ror_zp,    rra_zp,    pla,     adc_imm,  ror_acc,  nop_imm,  jmp_ind,  adc_abso, ror_abso, rra_abso, /* 6 */
/* 7 */      bvs_rel,   adc_indy,  nop,    rra_indy,  nop_zpx,  adc_zpx, ror_zpx,   rra_zpx,   sei,     adc_absy,  nop,     rra_absy, nop_absx,adc_absx, ror_absx, rra_absx, /* 7 */
/* 8 */      nop_imm,   sta_indx,  nop,    sax_indx,  sty_zp,   sta_zp,  stx_zp,    sax_zp,    dey,     nop_imm,   txa,     nop_imm,  sty_abso, sta_abso, stx_abso, sax_abso, /* 8 */
/* 9 */      bcc_rel,   sta_indy,  nop,    nop_indy,  sty_zpx,  sta_zpx, stx_zpx,   sax_zpx,   tya,     sta_absy, txs,      nop_absy, nop_absx_np, sta_absx, nop_absy,  nop_absy, /* 9 */
/* A */      ldy_imm,   lda_indx, ldx_imm, lax_indx,  ldy_zp,   lda_zp,  ldx_zp,    lax_zp,    tay,     lda_imm,  tax,      nop_imm,  ldy_abso, lda_abso, ldx_abso, lax_abso, /* A */
/* B */      bcs_rel,   lda_indy,  nop,    lax_indy,  ldy_zpx,  lda_zpx, ldx_zpx,   lax_zpx,   clv,     lda_absy, tsx,      lax_absy, ldy_absx, lda_absx, ldx_absy, lax_absx, /* B */
/* C */      cpy_imm,   cmp_indx,  nop,    dcp_indx,  cpy_zp,   cmp_zp,  dec_zp,    dcp_zp,    iny,     cmp_imm,  dex,      nop_imm,  cpy_abso, cmp_abso, dec_abso, dcp_abso, /* C */
/* D */      bne_rel,   cmp_indy,  nop,    dcp_indy,  nop_zpx,  cmp_zpx, dec_zpx,   dcp_zpx,   cld,     cmp_absy,  nop,     dcp_absy, nop_absx, cmp_absx, dec_absx, dcp_absx, /* D */
/* E */      cpx_imm,   sbc_indx,  nop,    isb_indx,  cpx_zp,   sbc_zp,  inc_zp,    isb_zp,    inx,     sbc_imm,  nop,      sbc_imm,  cpx_abso, sbc_abso, inc_abso, isb_abso, /* E */
/* F */      beq_rel,   sbc_indy,  nop,    isb_indy,  nop_zpx,  sbc_zpx, inc_zpx,   isb_zpx,   sed,     sbc_absy,  nop,     isb_absy, nop_absx, sbc_absx, inc_absx, isb_absx  /* F */
};

void nmi6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)read6502(0xFFFA) | ((uint16_t)read6502(0xFFFB) << 8);
}

void irq6502() {
    push16(pc);
    push8(status);
    status |= FLAG_INTERRUPT;
    pc = (uint16_t)read6502(0xFFFE) | ((uint16_t)read6502(0xFFFF) << 8);
}

uint8_t callexternal = 0;
void (*loopexternal)();

void exec6502(uint32_t instrs) {
    while (instrs-- > 0) {
        uint8_t opcode = read6502(pc++);

        (*optable[opcode])();
    }

}

void step6502() {
    uint8_t opcode = read6502(pc++);
    (*optable[opcode])();
}

void hookexternal(void *funcptr) {
    if (funcptr != (void *)NULL) {
        loopexternal = funcptr;
        callexternal = 1;
    } else callexternal = 0;
}
