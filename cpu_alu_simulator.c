/* STREAMING_CHUNK:Defining standard C library headers and constants... */
/**
 * ============================================================================
 * PROJECT TITLE : 8-Bit Multi-Bit ALU & CPU Architecture Simulator
 * COURSES       : KCA105 (COA), KCA152 (COA Lab), KCA151 (C Programming Lab)
 * DESCRIPTION   : Complete menu-driven C simulator implementing:
 *                 1. Step-by-Step 8-Bit CPU & RTL Datapath (4 Sub-Cycles)
 *                 2. Gate-Level 8-Bit Multi-Bit ALU & Full Adder Carry Chain
 *                 3. Binary-to-Gray & Gray-to-Binary Code Converters (Exp 2)
 *                 4. Booth's Signed 2's Complement Multiplication (Unit II)
 *                 5. IEEE-754 32-Bit Floating-Point Extractor & Flip-Flops
 *
 * COMPILATION   : gcc cpu_alu_simulator.c -o cpu_simulator
 * EXECUTION     : ./cpu_simulator
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_ROM_LINES 32
#define RAM_SIZE      16
#define NUM_REGS      4

/* STREAMING_CHUNK:Defining data structures for Full Adder, ALU, and CPU... */
/**
 * Represents a single 1-Bit Full Adder hardware slice (KCA152 Exp 1)
 */
typedef struct {
    uint8_t a;      /* Input Bit A_i                       */
    uint8_t b;      /* Input Bit B_i (or inverted for SUB) */
    uint8_t cin;    /* Carry-In C_i                        */
    uint8_t prop;   /* Propagate: P_i = A_i XOR B_i        */
    uint8_t gen;    /* Generate : G_i = A_i AND B_i        */
    uint8_t sum;    /* Sum Bit  : S_i = P_i XOR C_i        */
    uint8_t cout;   /* Carry-Out: C_{i+1} = G_i OR (P_i & C_i) */
} FullAdderSlice;

/**
 * Represents the 4-bit Status Flag Register (Z, C, S, V)
 */
typedef struct {
    uint8_t Z;      /* Zero Flag     (Result == 0)         */
    uint8_t C;      /* Carry Flag    (Unsigned Carry-Out)  */
    uint8_t S;      /* Sign Flag     (MSB Bit 7 == 1)      */
    uint8_t V;      /* Overflow Flag (C6 XOR C7)           */
} StatusFlags;

/**
 * Holds the complete output of an 8-bit Multi-Bit ALU operation
 */
typedef struct {
    uint8_t result;
    StatusFlags flags;
    FullAdderSlice slices[8];
} ALUResult;

/* STREAMING_CHUNK:Defining Instruction Set Architecture (ISA) and CPU structures... */
/**
 * Opcodes for the 16-Bit Simple Instruction Set Computer (KCA152 Exp 9)
 */
typedef enum {
    OP_NOP   = 0x0,
    OP_MOVI  = 0x1, /* MOVI Rd, Imm  : Rd <- Imm                */
    OP_MOV   = 0x2, /* MOV  Rd, Rs   : Rd <- Rs                 */
    OP_LOAD  = 0x3, /* LOAD Rd, Addr : Rd <- RAM[Addr]          */
    OP_STORE = 0x4, /* STORE Rs, Addr: RAM[Addr] <- Rs          */
    OP_ADD   = 0x5, /* ADD  Rd, Rs   : Rd <- Rd + Rs            */
    OP_SUB   = 0x6, /* SUB  Rd, Rs   : Rd <- Rd - Rs            */
    OP_AND   = 0x7, /* AND  Rd, Rs   : Rd <- Rd & Rs            */
    OP_OR    = 0x8, /* OR   Rd, Rs   : Rd <- Rd | Rs            */
    OP_XOR   = 0x9, /* XOR  Rd, Rs   : Rd <- Rd ^ Rs            */
    OP_ALU1  = 0xA, /* Single-operand ALU: NOT, SHL, SHR, INC, DEC */
    OP_JMP   = 0xD, /* JMP  Addr     : PC <- Addr               */
    OP_BR    = 0xE, /* JZ / JNZ Addr : Conditional Branch       */
    OP_HLT   = 0xF  /* HLT           : Stop CPU execution       */
} Opcode;

/**
 * Represents a 16-bit encoded instruction in Program Memory (ROM)
 */
typedef struct {
    uint16_t word;          /* [15:12] Op | [11:10] Rd | [9:8] Rs | [7:0] Imm */
    uint8_t  op;
    uint8_t  rd;
    uint8_t  rs;
    uint8_t  imm;
    char     mnemonic[32];
    char     comment[64];
} Instruction;

/**
 * Complete Hardware State of the 8-Bit CPU (KCA152 Exp 5, 7, 8, 9)
 */
typedef struct {
    uint8_t     regs[NUM_REGS];         /* Four 8-bit Internal Registers: R0-R3 */
    uint8_t     PC;                     /* Program Counter                      */
    uint8_t     MAR;                    /* Memory Address Register              */
    uint16_t    MBR;                    /* Memory Buffer Register               */
    uint16_t    IR;                     /* Instruction Register                 */
    char        ir_text[32];            /* Human-readable current instruction   */
    StatusFlags flags;                  /* Z, C, S, V Status Flags              */
    uint8_t     ram[RAM_SIZE];          /* 16-Byte Data Memory (RAM)            */
    Instruction rom[MAX_ROM_LINES];     /* Program Memory (ROM)                 */
    int         rom_count;              /* Number of loaded instructions        */
    int         t_state;                /* Sub-cycle: 0=FetchAddr, 1=Read, 2=Decode, 3=Exec */
    int         halted;                 /* 1 if CPU encountered HLT             */
    char        last_narrator[160];     /* Plain-English explanation of step    */
    char        last_rtl[80];           /* Register Transfer Language trace     */
    char        control_signals[96];    /* Active Control Unit signals          */
} CPUState;

/* STREAMING_CHUNK:Implementing binary formatting and safe input helper functions... */
/**
 * Converts an 8-bit unsigned integer into an 8-character binary string
 */
void to_bin8(uint8_t val, char *out_buf) {
    int i;
    for (i = 7; i >= 0; i--) {
        out_buf[7 - i] = ((val >> i) & 1) ? '1' : '0';
    }
    out_buf[8] = '\0';
}

/**
 * Converts an 8-bit unsigned value to its signed 2's complement integer (-128 to +127)
 */
int to_signed8(uint8_t val) {
    return (val >= 128) ? ((int)val - 256) : (int)val;
}

/**
 * Safely reads an integer from standard input and clears any leftover newline
 */
int read_int_safe(int default_val) {
    char buf[64];
    int val;
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return default_val;
    }
    if (sscanf(buf, "%d", &val) == 1) {
        return val;
    }
    return default_val;
}

/**
 * Waits for the user to press ENTER before continuing
 */
void press_enter_to_continue(void) {
    char buf[16];
    printf("\nPress [ENTER] to continue...");
    fflush(stdout);
    fgets(buf, sizeof(buf), stdin);
}

/* STREAMING_CHUNK:Implementing Gate-Level 1-Bit Full Adder and 8-Bit Multi-Bit ALU... */
/**
 * KCA152 Experiment 1: 1-Bit Full Adder using basic logic gates (XOR, AND, OR)
 */
FullAdderSlice full_adder_1bit(uint8_t a, uint8_t b, uint8_t cin) {
    FullAdderSlice s;
    s.a    = a & 1;
    s.b    = b & 1;
    s.cin  = cin & 1;
    s.prop = s.a ^ s.b;                       /* Half-Adder 1 XOR (Propagate) */
    s.gen  = s.a & s.b;                       /* Half-Adder 1 AND (Generate)  */
    s.sum  = s.prop ^ s.cin;                  /* Half-Adder 2 XOR (Sum)       */
    s.cout = s.gen | (s.prop & s.cin);        /* OR Gate for Carry-Out        */
    return s;
}

/**
 * KCA152 Experiment 2, 3, 6: 8-Bit Multi-Bit ALU with 3-to-8 Decoder Operations
 * Supported ops: "ADD", "SUB", "AND", "OR", "XOR", "NOT", "SHL", "SHR", "INC", "DEC", "B2G", "G2B"
 */
ALUResult compute_8bit_alu(const char *op, uint8_t val_a, uint8_t val_b) {
    ALUResult res;
    uint8_t a = val_a & 0xFF;
    uint8_t b = val_b & 0xFF;
    uint8_t result = 0;
    uint8_t carry_out = 0;
    uint8_t overflow = 0;
    int i;

    memset(&res, 0, sizeof(ALUResult));

    if (strcmp(op, "ADD") == 0 || strcmp(op, "SUB") == 0 ||
        strcmp(op, "INC") == 0 || strcmp(op, "DEC") == 0) {
        /* Arithmetic Operations using Cascaded 8-Bit Full Adder Chain */
        uint8_t eff_b = b;
        uint8_t carry = 0;
        uint8_t c6 = 0;

        if (strcmp(op, "SUB") == 0) {
            /* 2's Complement Subtraction: A - B = A + (~B) + 1 */
            eff_b = (~b) & 0xFF;
            carry = 1;
        } else if (strcmp(op, "INC") == 0) {
            eff_b = 1;
            carry = 0;
        } else if (strcmp(op, "DEC") == 0) {
            eff_b = 0xFF; /* Adding -1 (11111111) in 2's complement */
            carry = 0;
        }

        for (i = 0; i < 8; i++) {
            FullAdderSlice st = full_adder_1bit((a >> i) & 1, (eff_b >> i) & 1, carry);
            res.slices[i] = st;
            result |= (st.sum << i);
            carry = st.cout;
            if (i == 6) {
                c6 = carry;
            }
        }
        carry_out = carry;
        overflow  = c6 ^ carry_out;
    } else {
        /* Populate adder slices for reference & perform Logic/Shift/Code-Conversion */
        uint8_t carry = 0;
        for (i = 0; i < 8; i++) {
            res.slices[i] = full_adder_1bit((a >> i) & 1, (b >> i) & 1, carry);
            carry = res.slices[i].cout;
        }

        if (strcmp(op, "AND") == 0) {
            result = a & b;
        } else if (strcmp(op, "OR") == 0) {
            result = a | b;
        } else if (strcmp(op, "XOR") == 0) {
            result = a ^ b;
        } else if (strcmp(op, "NOT") == 0) {
            result = (~a) & 0xFF;
        } else if (strcmp(op, "SHL") == 0) {
            carry_out = (a >> 7) & 1;
            result = (a << 1) & 0xFF;
        } else if (strcmp(op, "SHR") == 0) {
            carry_out = a & 1;
            result = (a >> 1) & 0xFF;
        } else if (strcmp(op, "B2G") == 0) {
            /* KCA152 Exp 2: Binary to Gray Code: G = B XOR (B >> 1) */
            result = (a ^ (a >> 1)) & 0xFF;
        } else if (strcmp(op, "G2B") == 0) {
            /* KCA152 Exp 2: Gray to Binary Code conversion */
            uint8_t bin = a;
            uint8_t mask = a >> 1;
            while (mask != 0) {
                bin ^= mask;
                mask >>= 1;
            }
            result = bin & 0xFF;
        }
    }

    res.result  = result;
    res.flags.Z = (result == 0) ? 1 : 0;
    res.flags.C = carry_out ? 1 : 0;
    res.flags.S = (result >> 7) & 1;
    res.flags.V = overflow ? 1 : 0;
    return res;
}

/* STREAMING_CHUNK:Building the CPU instruction encoder and preset programs... */
/**
 * Helper to encode and append an instruction into CPU ROM
 */
void add_rom_instruction(CPUState *cpu, uint8_t op, uint8_t rd, uint8_t rs,
                         uint8_t imm, const char *mnemonic, const char *comment) {
    if (cpu->rom_count >= MAX_ROM_LINES) return;
    Instruction *ins = &cpu->rom[cpu->rom_count];
    ins->op   = op & 0xF;
    ins->rd   = rd & 0x3;
    ins->rs   = rs & 0x3;
    ins->imm  = imm & 0xFF;
    ins->word = ((uint16_t)ins->op << 12) |
                ((uint16_t)ins->rd << 10) |
                ((uint16_t)ins->rs << 8)  |
                ((uint16_t)ins->imm);
    strncpy(ins->mnemonic, mnemonic, sizeof(ins->mnemonic) - 1);
    ins->mnemonic[sizeof(ins->mnemonic) - 1] = '\0';
    strncpy(ins->comment, comment, sizeof(ins->comment) - 1);
    ins->comment[sizeof(ins->comment) - 1] = '\0';
    cpu->rom_count++;
}

/**
 * Resets all CPU registers, flags, and control state back to initial values
 */
void reset_cpu(CPUState *cpu, int clear_ram) {
    memset(cpu->regs, 0, sizeof(cpu->regs));
    cpu->PC      = 0;
    cpu->MAR     = 0;
    cpu->MBR     = 0;
    cpu->IR      = 0;
    strcpy(cpu->ir_text, "NONE");
    memset(&cpu->flags, 0, sizeof(StatusFlags));
    if (clear_ram) {
        memset(cpu->ram, 0, sizeof(cpu->ram));
    }
    cpu->t_state = 0;
    cpu->halted  = 0;
    strcpy(cpu->last_narrator, "CPU Reset & Ready. Choose 'Next Step' to start Step 1 (Fetch Address).");
    strcpy(cpu->last_rtl, "PC = 0 (Waiting for Clock Pulse)");
    strcpy(cpu->control_signals, "None (Idle)");
}

/**
 * Loads one of 4 syllabus-aligned preset assembly programs into ROM
 */
void load_preset_program(CPUState *cpu, int preset_id) {
    cpu->rom_count = 0;
    reset_cpu(cpu, 1);

    if (preset_id == 1) {
        /* Preset 1: Add, Shift & Store Demo */
        add_rom_instruction(cpu, OP_MOVI,  0, 0, 25, "MOVI R0, 25",  "Put 25 into Register R0");
        add_rom_instruction(cpu, OP_MOVI,  1, 0, 17, "MOVI R1, 17",  "Put 17 into Register R1");
        add_rom_instruction(cpu, OP_ADD,   0, 1, 0,  "ADD  R0, R1",  "R0 = 25 + 17 = 42");
        add_rom_instruction(cpu, OP_ALU1,  0, 1, 0,  "SHL  R0",      "Shift Left: 42 << 1 = 84");
        add_rom_instruction(cpu, OP_SUB,   0, 1, 0,  "SUB  R0, R1",  "R0 = 84 - 17 = 67");
        add_rom_instruction(cpu, OP_STORE, 0, 0, 2,  "STORE R0, 2",  "Save R0 (67) into RAM[2]");
        add_rom_instruction(cpu, OP_HLT,   0, 0, 0,  "HLT",          "Stop CPU execution");
    } else if (preset_id == 2) {
        /* Preset 2: Loop Sum (5 + 4 + 3 + 2 + 1 = 15) */
        add_rom_instruction(cpu, OP_MOVI,  0, 0, 0,  "MOVI R0, 0",   "R0 holds sum (0)");
        add_rom_instruction(cpu, OP_MOVI,  1, 0, 5,  "MOVI R1, 5",   "R1 holds loop counter (5)");
        add_rom_instruction(cpu, OP_ADD,   0, 1, 0,  "ADD  R0, R1",  "Loop: Add R1 into R0");
        add_rom_instruction(cpu, OP_ALU1,  1, 4, 0,  "DEC  R1",      "Decrement counter R1");
        add_rom_instruction(cpu, OP_BR,    1, 0, 2,  "JNZ  2",       "If R1 != 0, jump to line 2");
        add_rom_instruction(cpu, OP_STORE, 0, 0, 0,  "STORE R0, 0",  "Store sum (15) in RAM[0]");
        add_rom_instruction(cpu, OP_HLT,   0, 0, 0,  "HLT",          "Stop CPU");
    } else if (preset_id == 3) {
        /* Preset 3: Binary to Gray Code using Register Transfer */
        add_rom_instruction(cpu, OP_MOVI,  0, 0, 13, "MOVI R0, 13",  "Load Binary 13 (00001101)");
        add_rom_instruction(cpu, OP_MOV,   1, 0, 0,  "MOV  R1, R0",  "Copy 13 into R1");
        add_rom_instruction(cpu, OP_ALU1,  1, 2, 0,  "SHR  R1",      "R1 = 13 >> 1 (00000110)");
        add_rom_instruction(cpu, OP_XOR,   0, 1, 0,  "XOR  R0, R1",  "Gray Code = R0 XOR R1 (11)");
        add_rom_instruction(cpu, OP_STORE, 0, 0, 4,  "STORE R0, 4",  "Save Gray code in RAM[4]");
        add_rom_instruction(cpu, OP_HLT,   0, 0, 0,  "HLT",          "Stop CPU");
    } else {
        /* Preset 4: Signed 2's Complement Overflow Test (+100 + +50 = +150 -> Overflow!) */
        add_rom_instruction(cpu, OP_MOVI,  0, 0, 100,"MOVI R0, 100", "R0 = +100");
        add_rom_instruction(cpu, OP_MOVI,  1, 0, 50, "MOVI R1, 50",  "R1 = +50");
        add_rom_instruction(cpu, OP_ADD,   0, 1, 0,  "ADD  R0, R1",  "100+50=150 (Sets Overflow V=1)");
        add_rom_instruction(cpu, OP_STORE, 0, 0, 1,  "STORE R0, 1",  "Save result into RAM[1]");
        add_rom_instruction(cpu, OP_HLT,   0, 0, 0,  "HLT",          "Stop CPU");
    }
}

/* STREAMING_CHUNK:Implementing the 4-Step Instruction Cycle & RTL Execution Engine... */
/**
 * Executes Step 4 (Execute Phase) of the currently decoded instruction in IR
 */
void execute_decoded_instruction(CPUState *cpu) {
    uint8_t op  = (cpu->IR >> 12) & 0xF;
    uint8_t rd  = (cpu->IR >> 10) & 0x3;
    uint8_t rs  = (cpu->IR >> 8)  & 0x3;
    uint8_t imm = cpu->IR & 0xFF;

    if (op == OP_MOVI) {
        cpu->regs[rd] = imm;
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (EXECUTE): Loaded immediate value %u directly into Register R%u.", imm, rd);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "R%u <- %u (0x%02X)", rd, imm, imm);
        strcpy(cpu->control_signals, "Reg_Write = 1");

    } else if (op == OP_MOV) {
        cpu->regs[rd] = cpu->regs[rs];
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (EXECUTE): Copied value %u from Register R%u into Register R%u.",
                 cpu->regs[rs], rs, rd);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "R%u <- R%u (%u)", rd, rs, cpu->regs[rd]);
        strcpy(cpu->control_signals, "Reg_Read = 1, Reg_Write = 1");

    } else if (op == OP_LOAD) {
        uint8_t addr = imm & 0x0F;
        cpu->MAR = addr;
        cpu->regs[rd] = cpu->ram[addr];
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (EXECUTE): Loaded byte %u from RAM[%u] into Register R%u.",
                 cpu->regs[rd], addr, rd);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "MAR <- %u, R%u <- RAM[%u] (%u)",
                 addr, rd, addr, cpu->regs[rd]);
        strcpy(cpu->control_signals, "MAR_In = 1, Mem_Read = 1, Reg_Write = 1");

    } else if (op == OP_STORE) {
        uint8_t addr = imm & 0x0F;
        cpu->MAR = addr;
        cpu->ram[addr] = cpu->regs[rs];
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (EXECUTE): Saved Register R%u (%u) into Data Memory RAM[%u].",
                 rs, cpu->regs[rs], addr);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "MAR <- %u, RAM[%u] <- R%u (%u)",
                 addr, addr, rs, cpu->regs[rs]);
        strcpy(cpu->control_signals, "MAR_In = 1, Mem_Write = 1");

    } else if (op >= OP_ADD && op <= OP_XOR) {
        const char *op_names[] = {"ADD", "SUB", "AND", "OR", "XOR"};
        const char *alu_op = op_names[op - OP_ADD];
        uint8_t val_a = cpu->regs[rd];
        uint8_t val_b = cpu->regs[rs];
        ALUResult res = compute_8bit_alu(alu_op, val_a, val_b);

        cpu->regs[rd] = res.result;
        cpu->flags    = res.flags;

        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (ALU EXECUTE): 8-Bit ALU computed %s on R%u (%u) and R%u (%u) = %u.",
                 alu_op, rd, val_a, rs, val_b, res.result);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "R%u <- R%u %s R%u (%u)",
                 rd, rd, alu_op, rs, res.result);
        strcpy(cpu->control_signals, "ALU_Enable = 1, Flags_In = 1, Reg_Write = 1");

    } else if (op == OP_ALU1) {
        const char *sub_ops[] = {"NOT", "SHL", "SHR", "INC", "DEC"};
        const char *alu_op = (rs <= 4) ? sub_ops[rs] : "NOT";
        uint8_t val_a = cpu->regs[rd];
        ALUResult res = compute_8bit_alu(alu_op, val_a, 0);

        cpu->regs[rd] = res.result;
        cpu->flags    = res.flags;

        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 4 (ALU EXECUTE): 8-Bit ALU computed %s on R%u (%u -> %u).",
                 alu_op, rd, val_a, res.result);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "R%u <- %s(R%u) = %u",
                 rd, alu_op, rd, res.result);
        strcpy(cpu->control_signals, "ALU_Enable = 1, Flags_In = 1, Reg_Write = 1");

    } else if (op == OP_JMP || op == OP_BR) {
        int is_cond = (op == OP_BR);
        int is_jnz  = (rd == 1);
        int take    = !is_cond || (is_jnz ? (cpu->flags.Z == 0) : (cpu->flags.Z == 1));
        if (take) {
            cpu->PC = imm;
            snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                     "STEP 4 (BRANCH TAKEN): Condition met (Z=%u), jumped PC to line %u.",
                     cpu->flags.Z, imm);
            snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "PC <- %u", imm);
            strcpy(cpu->control_signals, "PC_Load = 1");
        } else {
            snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                     "STEP 4 (BRANCH NOT TAKEN): Condition not met (Z=%u), continuing to next line.",
                     cpu->flags.Z);
            strcpy(cpu->last_rtl, "PC unchanged");
            strcpy(cpu->control_signals, "None");
        }

    } else if (op == OP_HLT) {
        cpu->halted = 1;
        strcpy(cpu->last_narrator, "STEP 4 (HALT): CPU Halted — Program execution complete!");
        strcpy(cpu->last_rtl, "CPU_HALT <- 1");
        strcpy(cpu->control_signals, "HLT_Line = 1");
    }
}

/**
 * Advances the CPU by one sub-cycle (T0 -> T1 -> T2 -> T3)
 */
void step_micro_cycle(CPUState *cpu) {
    if (cpu->halted) {
        strcpy(cpu->last_narrator, "Program has already halted! Choose 'Reset CPU' to run again.");
        return;
    }
    if (cpu->PC >= cpu->rom_count && cpu->t_state == 0) {
        cpu->halted = 1;
        return;
    }

    if (cpu->t_state == 0) {
        /* STEP 1: FETCH ADDRESS (MAR <- PC) */
        cpu->MAR = cpu->PC;
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 1 OF 4 (FETCH ADDRESS): Copied Program Counter (%u) into MAR.", cpu->PC);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "MAR <- PC (%u)", cpu->PC);
        strcpy(cpu->control_signals, "PC_Out = 1, MAR_In = 1");
        cpu->t_state = 1;

    } else if (cpu->t_state == 1) {
        /* STEP 2: READ MEMORY (MBR <- ROM[MAR], PC <- PC + 1) */
        Instruction *ins = &cpu->rom[cpu->MAR];
        cpu->MBR = ins->word;
        cpu->PC  = (cpu->PC + 1) & 0xFF;
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 2 OF 4 (READ MEMORY): Fetched '%s' into MBR and incremented PC to %u.",
                 ins->mnemonic, cpu->PC);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "MBR <- ROM[%u] (0x%04X), PC <- %u",
                 cpu->MAR, cpu->MBR, cpu->PC);
        strcpy(cpu->control_signals, "Mem_Read = 1, MBR_In = 1, PC_Inc = 1");
        cpu->t_state = 2;

    } else if (cpu->t_state == 2) {
        /* STEP 3: DECODE INSTRUCTION (IR <- MBR) */
        Instruction *ins = &cpu->rom[cpu->MAR];
        cpu->IR = cpu->MBR;
        strncpy(cpu->ir_text, ins->mnemonic, sizeof(cpu->ir_text) - 1);
        snprintf(cpu->last_narrator, sizeof(cpu->last_narrator),
                 "STEP 3 OF 4 (DECODE): Control Unit decoded instruction '%s' inside IR.",
                 cpu->ir_text);
        snprintf(cpu->last_rtl, sizeof(cpu->last_rtl), "IR <- MBR (%s)", cpu->ir_text);
        strcpy(cpu->control_signals, "IR_In = 1, CU_Decode = 1");
        cpu->t_state = 3;

    } else if (cpu->t_state == 3) {
        /* STEP 4: EXECUTE INSTRUCTION */
        execute_decoded_instruction(cpu);
        cpu->t_state = 0;
    }
}

/* STREAMING_CHUNK:Rendering the visual ASCII CPU Architecture Dashboard... */
/**
 * Prints the clean, structured CPU Architecture Dashboard to the terminal
 */
void print_cpu_dashboard(const CPUState *cpu) {
    char bin_buf[9];
    int i;
    int active_line = (cpu->t_state == 0) ? cpu->PC : cpu->MAR;

    printf("\n================================================================================\n");
    printf("   MODULE 1: 8-BIT CPU & RTL DATAPATH SIMULATOR (KCA105 & KCA152 EXP 5,7,8,9)\n");
    printf("================================================================================\n");

    /* 4-Step Progress Indicator */
    printf(" CYCLE PROGRESS : %s[1.Fetch Addr] -> %s[2.Read Mem] -> %s[3.Decode] -> %s[4.Execute]\n",
           (cpu->t_state == 1) ? ">>" : "  ",
           (cpu->t_state == 2) ? ">>" : "  ",
           (cpu->t_state == 3) ? ">>" : "  ",
           (cpu->t_state == 0 && cpu->IR != 0) ? ">>" : "  ");
    printf("--------------------------------------------------------------------------------\n");
    printf(" WHAT HAPPENED  : %s\n", cpu->last_narrator);
    printf(" RTL TRANSFER   : %-32s | CONTROL SIGNALS: %s\n", cpu->last_rtl, cpu->control_signals);
    printf("--------------------------------------------------------------------------------\n");

    /* Section A: Special Purpose Control Registers */
    printf(" [A] SPECIAL CONTROL REGISTERS:\n");
    printf("     +------------------+------------------+------------------+------------------+\n");
    printf("     | PC  : %-3u (0x%02X) | MAR : %-3u (0x%02X) | MBR : 0x%04X     | IR : %-11s |\n",
           cpu->PC, cpu->PC, cpu->MAR, cpu->MAR, cpu->MBR, cpu->ir_text);
    printf("     +------------------+------------------+------------------+------------------+\n");

    /* Section B: Four 8-Bit Internal Registers (R0 - R3) */
    printf("\n [B] FOUR 8-BIT INTERNAL REGISTERS (KCA152 Exp 5) & STATUS FLAGS (Z, C, S, V):\n");
    for (i = 0; i < NUM_REGS; i++) {
        to_bin8(cpu->regs[i], bin_buf);
        printf("     R%d : %3u (Signed: %+4d | Hex: 0x%02X | Binary: %s)\n",
               i, cpu->regs[i], to_signed8(cpu->regs[i]), cpu->regs[i], bin_buf);
    }
    printf("     STATUS FLAGS -> Zero(Z): %u | Carry(C): %u | Sign(S): %u | Overflow(V): %u\n",
           cpu->flags.Z, cpu->flags.C, cpu->flags.S, cpu->flags.V);

    /* Section C: Program Memory (ROM) & Data Memory (RAM) */
    printf("\n [C] PROGRAM MEMORY (ROM):                           [D] NON-ZERO DATA RAM:\n");
    for (i = 0; i < cpu->rom_count; i++) {
        const char *marker = (!cpu->halted && i == active_line) ? "-->" : "   ";
        printf("     %s [%2d] %-14s (0x%04X) ; %s\n",
               marker, i, cpu->rom[i].mnemonic, cpu->rom[i].word, cpu->rom[i].comment);
    }
    printf("     DATA RAM[0..7] : ");
    for (i = 0; i < 8; i++) {
        printf("[%d]=%-3u ", i, cpu->ram[i]);
    }
    printf("\n================================================================================\n");
}

/**
 * Interactive submenu for Module 1: Step-by-Step CPU Simulator
 */
void run_cpu_simulator_menu(CPUState *cpu) {
    int choice = 0;
    while (1) {
        print_cpu_dashboard(cpu);
        printf(" [1] Next Step (1 Sub-Cycle)     [2] Complete Full Instruction (4 Steps)\n");
        printf(" [3] Run Until Halt              [4] Load Different Example Program (1-4)\n");
        printf(" [5] Reset CPU                   [0] Back to Main Menu\n");
        printf(" Select action: ");

        choice = read_int_safe(-1);
        if (choice == 0) {
            break;
        } else if (choice == 1) {
            step_micro_cycle(cpu);
        } else if (choice == 2) {
            if (!cpu->halted) {
                do {
                    step_micro_cycle(cpu);
                } while (cpu->t_state != 0 && !cpu->halted);
            }
        } else if (choice == 3) {
            int safety = 0;
            while (!cpu->halted && safety < 400) {
                step_micro_cycle(cpu);
                safety++;
            }
        } else if (choice == 4) {
            printf("\n Choose Preset (1: Add/Shift/Store, 2: Loop Sum, 3: Binary->Gray, 4: Overflow): ");
            int p = read_int_safe(1);
            if (p < 1 || p > 4) p = 1;
            load_preset_program(cpu, p);
        } else if (choice == 5) {
            reset_cpu(cpu, 1);
        }
    }
}

/* STREAMING_CHUNK:Implementing Module 2: 8-Bit Multi-Bit ALU & Full Adder Explorer... */
/**
 * Interactive Module 2: Gate-Level 8-Bit ALU & Full Adder Carry Chain Explorer
 */
void run_alu_explorer_menu(void) {
    uint8_t a = 25, b = 17;
    int op_choice = 1;
    const char *ops[]  = {"ADD", "SUB", "AND", "OR", "XOR", "NOT", "B2G", "G2B"};
    const char *sels[] = {"000", "001", "010", "011", "100", "101", "110", "111"};

    while (1) {
        const char *cur_op = ops[op_choice - 1];
        ALUResult res = compute_8bit_alu(cur_op, a, b);
        char bin_a[9], bin_b[9], bin_y[9];
        int i;

        to_bin8(a, bin_a);
        to_bin8(b, bin_b);
        to_bin8(res.result, bin_y);

        printf("\n================================================================================\n");
        printf("   MODULE 2: 8-BIT MULTI-BIT ALU & FULL ADDER EXPLORER (KCA152 EXP 1, 2, 3, 6)\n");
        printf("================================================================================\n");
        printf(" 3-to-8 Decoder Select (S2 S1 S0) = %s (%s)\n", sels[op_choice - 1], cur_op);
        printf("--------------------------------------------------------------------------------\n");
        printf(" INPUT A    : %3u (Signed: %+4d | Hex: 0x%02X | Binary: %s)\n", a, to_signed8(a), a, bin_a);
        printf(" INPUT B    : %3u (Signed: %+4d | Hex: 0x%02X | Binary: %s)\n", b, to_signed8(b), b, bin_b);
        printf(" ALU OUTPUT : %3u (Signed: %+4d | Hex: 0x%02X | Binary: %s)\n",
               res.result, to_signed8(res.result), res.result, bin_y);
        printf(" ALU FLAGS  : Zero(Z)=%u | Carry(C8)=%u | Sign(S)=%u | Overflow(V)=%u\n",
               res.flags.Z, res.flags.C, res.flags.S, res.flags.V);
        printf("--------------------------------------------------------------------------------\n");
        printf(" GATE-LEVEL 8-BIT FULL ADDER CARRY CHAIN (FA7 .. FA0) [KCA152 Exp 1]:\n");
        printf(" +-------+-----+-----+-----+---------------+---------------+-------+------+\n");
        printf(" | Stage | A_i | B_i | Cin | Prop (A ^ B)  | Gen  (A & B)  | Sum_i | Cout |\n");
        printf(" +-------+-----+-----+-----+---------------+---------------+-------+------+\n");
        for (i = 7; i >= 0; i--) {
            FullAdderSlice s = res.slices[i];
            printf(" |  FA%d  |  %u  |  %u  |  %u  |     P%d = %u    |     G%d = %u    |   %u   |   %u  |\n",
                   i, s.a, s.b, s.cin, i, s.prop, i, s.gen, s.sum, s.cout);
        }
        printf(" +-------+-----+-----+-----+---------------+---------------+-------+------+\n");

        printf("\n [1] Set Input A (0-255)   [2] Set Input B (0-255)   [3] Choose ALU Operation\n");
        printf(" [0] Return to Main Menu\n");
        printf(" Select choice: ");

        int ch = read_int_safe(0);
        if (ch == 0) {
            break;
        } else if (ch == 1) {
            printf(" Enter new Input A (0-255): ");
            a = (uint8_t)(read_int_safe(a) & 0xFF);
        } else if (ch == 2) {
            printf(" Enter new Input B (0-255): ");
            b = (uint8_t)(read_int_safe(b) & 0xFF);
        } else if (ch == 3) {
            printf("\n 3-to-8 Decoder Operations:\n");
            printf("  1. S=000 ADD (A + B)          2. S=001 SUB (A - B, 2's Comp)\n");
            printf("  3. S=010 AND (A & B)          4. S=011 OR  (A | B)\n");
            printf("  5. S=100 XOR (A ^ B)          6. S=101 NOT (~A)\n");
            printf("  7. S=110 Binary -> Gray Code  8. S=111 Gray -> Binary Code\n");
            printf(" Select Operation (1-8): ");
            int op_in = read_int_safe(1);
            if (op_in >= 1 && op_in <= 8) {
                op_choice = op_in;
            }
        }
    }
}

/* STREAMING_CHUNK:Implementing Module 3: Booth's Signed Multiplication Algorithm... */
/**
 * KCA105 Unit II: Booth's Signed 2's Complement Multiplication Algorithm
 */
void run_booth_multiplier_module(void) {
    int m_in = -7, q_in = 3;
    uint8_t A = 0, Q, M, neg_M, Q_1 = 0;
    char bin_a[9], bin_q[9], bin_m[9];
    int step;

    printf("\n================================================================================\n");
    printf("   MODULE 3: BOOTH'S 2'S COMPLEMENT MULTIPLICATION SIMULATOR (KCA105 UNIT II)\n");
    printf("================================================================================\n");
    printf(" Enter Signed Multiplicand M (-64 to +63) [default -7]: ");
    m_in = read_int_safe(-7);
    printf(" Enter Signed Multiplier   Q (-64 to +63) [default  3]: ");
    q_in = read_int_safe(3);

    M     = (uint8_t)(m_in & 0xFF);
    neg_M = (uint8_t)(((~M) + 1) & 0xFF);
    Q     = (uint8_t)(q_in & 0xFF);
    to_bin8(M, bin_m);

    printf("\n Multiplicand M = %d (%s) | -M (2's Comp) = %d | Multiplier Q = %d\n",
           m_in, bin_m, to_signed8(neg_M), q_in);
    printf(" +----------+---------+-------------------------------+----------+----------+-----+\n");
    printf(" | Step     | Q0, Q-1 | RTL Micro-Operation           | Acc (A)  | Mult (Q) | Q-1 |\n");
    printf(" +----------+---------+-------------------------------+----------+----------+-----+\n");

    to_bin8(A, bin_a);
    to_bin8(Q, bin_q);
    printf(" | Init     |  %u,  %u  | Initialize A=0, Q=%-4d        | %s | %s |  %u  |\n",
           Q & 1, Q_1, q_in, bin_a, bin_q, Q_1);

    for (step = 1; step <= 8; step++) {
        uint8_t q0 = Q & 1;
        if (q0 == 1 && Q_1 == 0) {
            A = (A + neg_M) & 0xFF;
            to_bin8(A, bin_a);
            to_bin8(Q, bin_q);
            printf(" | Step %dA  |  1,  0  | Subtract M: A <- A - M        | %s | %s |  %u  |\n",
                   step, bin_a, bin_q, Q_1);
        } else if (q0 == 0 && Q_1 == 1) {
            A = (A + M) & 0xFF;
            to_bin8(A, bin_a);
            to_bin8(Q, bin_q);
            printf(" | Step %dA  |  0,  1  | Add M     : A <- A + M        | %s | %s |  %u  |\n",
                   step, bin_a, bin_q, Q_1);
        }

        /* Arithmetic Shift Right (ASR) on [A, Q, Q-1] */
        uint8_t msb_a = A & 0x80;
        uint8_t lsb_a = A & 1;
        Q_1 = Q & 1;
        Q   = ((Q >> 1) | (lsb_a << 7)) & 0xFF;
        A   = ((A >> 1) | msb_a) & 0xFF;

        to_bin8(A, bin_a);
        to_bin8(Q, bin_q);
        printf(" | Step %dS  |  %u,  %u  | Arithmetic Shift Right (ASR)  | %s | %s |  %u  |\n",
               step, q0, Q_1, bin_a, bin_q, Q_1);
    }
    printf(" +----------+---------+-------------------------------+----------+----------+-----+\n");

    int16_t product = (int16_t)(((uint16_t)A << 8) | Q);
    printf(" FINAL 16-BIT PRODUCT [A:Q] = %s %s (Decimal: %d | Hex: 0x%04X)\n",
           bin_a, bin_q, (int)product, (uint16_t)product);
    press_enter_to_continue();
}

/* STREAMING_CHUNK:Implementing Module 4: IEEE-754 Floating-Point & Flip-Flop Verifier... */
/**
 * KCA105 Unit II & KCA152 Exp 4: IEEE-754 Single Precision & Flip-Flop Verifier
 */
void run_ieee_and_flipflop_module(void) {
    union {
        float    f;
        uint32_t u;
    } ieee;
    char input_buf[64];
    int i;

    printf("\n================================================================================\n");
    printf("   MODULE 4: IEEE-754 FLOATING POINT & FLIP-FLOP EXCITATION TABLES\n");
    printf("================================================================================\n");
    printf(" Enter a floating-point number (e.g. -12.625 or 3.14159): ");
    if (fgets(input_buf, sizeof(input_buf), stdin) != NULL) {
        if (sscanf(input_buf, "%f", &ieee.f) != 1) {
            ieee.f = -12.625f;
        }
    } else {
        ieee.f = -12.625f;
    }

    uint32_t sign = (ieee.u >> 31) & 1;
    uint32_t exp  = (ieee.u >> 23) & 0xFF;
    uint32_t mant = ieee.u & 0x7FFFFF;

    printf("\n [A] IEEE-754 32-BIT SINGLE-PRECISION REPRESENTATION FOR %g:\n", ieee.f);
    printf("     Hexadecimal Encoding : 0x%08X\n", ieee.u);
    printf("     Sign Bit (1 bit)     : %u (%s)\n", sign, sign ? "Negative '-'" : "Positive '+'");
    printf("     Exponent (8 bits)    : ");
    for (i = 7; i >= 0; i--) printf("%u", (exp >> i) & 1);
    printf(" (Biased = %u, Unbiased = %d)\n", exp, (int)exp - 127);
    printf("     Mantissa (23 bits)   : ");
    for (i = 22; i >= 0; i--) printf("%u", (mant >> i) & 1);
    printf("\n");

    printf("\n [B] KCA152 EXP 4: FLIP-FLOP CHARACTERISTIC & EXCITATION TABLE REFERENCE:\n");
    printf("     +------+------+---------------+     +------+--------+-----+-----+-----+-----+\n");
    printf("     |  J   |  K   | Next Q(t+1)   |     | Q(t) | Q(t+1) |  J  |  K  |  D  |  T  |\n");
    printf("     +------+------+---------------+     +------+--------+-----+-----+-----+-----+\n");
    printf("     |  0   |  0   | Q(t)  (Hold)  |     |  0   |   0    |  0  |  X  |  0  |  0  |\n");
    printf("     |  0   |  1   | 0     (Reset) |     |  0   |   1    |  1  |  X  |  1  |  1  |\n");
    printf("     |  1   |  0   | 1     (Set)   |     |  1   |   0    |  X  |  1  |  0  |  1  |\n");
    printf("     |  1   |  1   | Q'(t) (Toggle)|     |  1   |   1    |  X  |  0  |  1  |  0  |\n");
    printf("     +------+------+---------------+     +------+--------+-----+-----+-----+-----+\n");
    press_enter_to_continue();
}

/* STREAMING_CHUNK:Implementing the main interactive application loop... */
int main(void) {
    CPUState cpu;
    int menu_choice = 0;

    /* Initialize CPU with Preset Program 1 (Add, Shift & Store) */
    load_preset_program(&cpu, 1);

    while (1) {
        printf("\n################################################################################\n");
        printf("#     MULTI-BIT ALU & CPU ARCHITECTURE SIMULATOR (MCA KCA105/KCA152/KCA151)    #\n");
        printf("################################################################################\n");
        printf("#  1. Interactive 8-Bit CPU & RTL Datapath Simulator (Step-by-Step 4 Cycles)   #\n");
        printf("#  2. 8-Bit Multi-Bit ALU & Full Adder Carry Chain Explorer (Exp 1, 2, 3, 6)   #\n");
        printf("#  3. Booth's Signed 2's Complement Multiplication Visualizer (KCA105 Unit II) #\n");
        printf("#  4. IEEE-754 Floating-Point Converter & Flip-Flop Excitation Tables (Exp 4)  #\n");
        printf("#  0. Exit Simulator                                                           #\n");
        printf("################################################################################\n");
        printf("Select Module (0-4): ");

        menu_choice = read_int_safe(0);
        if (menu_choice == 0) {
            printf("\nExiting Multi-Bit ALU & CPU Simulator. Good luck with your Viva!\n");
            break;
        } else if (menu_choice == 1) {
            run_cpu_simulator_menu(&cpu);
        } else if (menu_choice == 2) {
            run_alu_explorer_menu();
        } else if (menu_choice == 3) {
            run_booth_multiplier_module();
        } else if (menu_choice == 4) {
            run_ieee_and_flipflop_module();
        } else {
            printf("\nInvalid choice! Please enter a number between 0 and 4.\n");
        }
    }

    return 0;
}