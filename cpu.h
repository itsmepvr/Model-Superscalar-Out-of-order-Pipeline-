/*
 * Authors: Venkata Ramana Pulugari <vpuluga1@binghamton.edu>
            Mastanvali Shaik <mshaik1@binghamton.edu>
 * Description: This file contains the implementation of a program that performs 
 *              a 11-stage pipeline with an instruction decoder
 * Date: 3/6/23
 */


#ifndef _CPU_H_
#define _CPU_H_
#include <stdbool.h>
#include <assert.h>

#define TRUE 1
#define FALSE 0

#define REG_COUNT 16

#define MEMORY_SIZE 64000

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

// Constants for BTB and PT sizes
#define BTB_SIZE 16
#define PT_SIZE 16

#define PC_TAG 0xFFFFFFC0

// BTB entry structure
typedef struct BTBEntry{
    int tag;
    int target_address;
} BTBEntry;

// PT entry structure
typedef struct PTEntry{
    int counter;
} PTEntry;

/* Define opcodes as constants */
#define MUL     0
#define ADD     1
#define SUB     2
#define DIV     3
#define LD      4
#define ST      5
#define MULL    6
#define ADDL    7
#define SUBL    8
#define DIVL    9
#define LDL     10
#define STL     11
#define SET     12
#define BEZ     13
#define BGEZ    14
#define BLEZ    15
#define BGTZ    16
#define BLTZ    17
#define RET     18

#define ROB_SIZE 8

typedef struct Instruction{
    char instruction[32];
    int instruction_no;
    int opcode;
    int rd;
    int rs1;
    int rs2;
    int op1;
} Instruction;
typedef struct Stage
{
    int pc;
    Instruction* inst;
    int opcode;
    int dest_value;
    int src1_value;
    int src2_value;
    int result;
    int addr;
    int occupied;
    bool valid;
    bool src1_ready;
    bool src2_ready;
} Stage;

typedef struct ROBEntry {
    int ROBid;
    int destinationReg;
    int result;
    bool exception;
    int completed;
} ROBEntry;

typedef struct ReorderBuffer {
    ROBEntry entries[ROB_SIZE];
    int head;
    int tail;
} ReorderBuffer;

#define RS_SIZE 4

typedef struct ReservationStation {
    Stage entries[RS_SIZE];
    int head;
    int tail;
} ReservationStation;

typedef struct Register
{
    int status;
    int tag;
    int value;          // contains register value
    bool is_writing;    // indicate that the register is current being written
	                    // True: register is not ready
						// False: register is ready
} Register;

typedef struct Halt
{
    int halt;
    int reg;
    int end_halt;
} Halt;

// Bubbel struct
typedef struct Bubble
{
    int valid;
    int reg;
    int val;
} Bubble;

/* Model of CPU */
typedef struct CPU
{
    int pc;
    int clockCycle;
    Instruction *code_mem;
    int code_size;
    int stalled_cycles;
    int data_mem[MEMORY_SIZE];
	Register *regs;
    Register *regs_copy;
    int memory_size;
    int flush;
    Halt halt_flag;
    Bubble add_bubble;
    Bubble mul_bubble;
    Bubble div_bubble;
    Bubble memory_bubble;
	Stage fetch;
    Stage decode;
    Stage analyze;
    Stage read_registers;
    Stage issue;
    Stage add;
    Stage mul;
    Stage div;
    Stage branch;
    Stage mem1;
    Stage mem2;
    Stage writeback_1;
    Stage writeback_2;
    Stage writeback_3;
    Stage writeback_4;
    Stage retire_1;
    Stage retire_2;
} CPU;

CPU*
CPU_init();

Register*
create_registers(int size);

int
CPU_run(CPU* cpu, char* filename);

void
CPU_stop(CPU* cpu);


// =================================================================

int has_two_R_letters(char* str, char* code);

int getIndex(char** arr, int len, char *inst, int has_register);

void parse_instructions(Instruction *instr, char *line, int no);

void print_inst(Instruction *inst);

void retire_stage(CPU *cpu);

static int writeback_stage(CPU* cpu);

void memory1_stage(CPU* cpu);

void memory2_stage(CPU* cpu);

void branch_stage(CPU* cpu);

void div_stage(CPU* cpu);

void mul_stage(CPU* cpu);

void add_stage(CPU* cpu);

void read_registers_stage(CPU* cpu);

void analyze_stage(CPU* cpu);

void decode_stage(CPU* cpu);

void fetch_stage(CPU* cpu);

void end_of_clock_cycle(CPU* cpu);

void print_instruction_info(CPU* cpu, int cycle);

void print_instruction(char* stage, Stage s);

int load_memory_map(char* filename, CPU* cpu);

int bubble_fetch(CPU *cpu, int register, int *value);

void flushStages(CPU *cpu);

int predictBranchOutcome(int pc);

void updateBranchPredictor(CPU *cpu, int addr, int actual_outcome);

void initBranchPredictor();

void ROB_Init();

bool ROB_IsFull();

bool ROB_IsEmpty();

int ROB_Enqueue(CPU *cpu, int destReg);

void ROB_Update(int ROBid, int result);

void ROB_Commit(int ROBid);

bool ROB_IsReady(int ROBid);

void RS_Init();

void Rename_Registers(CPU *cpu, int *dst, int *src1, int *src2);

int RS_Enqueue(CPU *cpu, int opcode, int operand1, int operand2, int destReg);

void get_RS(CPU *cpu);

#endif