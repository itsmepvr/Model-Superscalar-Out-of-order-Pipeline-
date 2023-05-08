
/*
 * Authors: Venkata Ramana Pulugari <vpuluga1@binghamton.edu>
            Mastanvali Shaik <mshaik1@binghamton.edu>
 * Description: This file contains the implementation of a program that performs
 *              a 11-stage pipeline with an instruction decoder and branch prediction
 * Date: 3/6/23
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cpu.h"
#include <regex.h>
#include <stdint.h>

// flags
int camel_flag = FALSE;
int flush_flag = FALSE;
int simulation_count = 0;

// BTB and PT arrays
BTBEntry btb[BTB_SIZE];
PTEntry pt[PT_SIZE];
ReorderBuffer rob;
ReservationStation rs;

// maping from opcode to string
char *instructions[] = {"mul", "add", "sub", "div", "ld", "st", "mull", "addl", "subl", "divl", "ldl", "stl", "set", "bez", "bgez", "blez", "bgtz", "bltz", "ret"};

// regex to check the opcode
char *instruction_id_regex = "(mul)|(add)|(sub)|(div)|(ld)|(st)|(mull)|(addl)|(subl)|(divl)|(ldl)|(stl)|(set)|(bez)|(bgez)|(blez)|(bgtz)|(bltz)|(ret)";

regex_t instruction_id_regex_compiled;

// specific regex to parse the instruction
char *instruction_regex[] = {
    "^[0-9]+ mul R([0-9]+) R(-?[0-9]+) #(-?[0-9]+)",
    "^[0-9]+ add R([0-9]+) R(-?[0-9]+) #(-?[0-9]+)",
    "^[0-9]+ sub R([0-9]+) R(-?[0-9]+) #(-?[0-9]+)",
    "^[0-9]+ div R([0-9]+) R(-?[0-9]+) #(-?[0-9]+)",
    "^[0-9]+ ld R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ st R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ mul R([0-9]+) R(-?[0-9]+) R(-?[0-9]+)",
    "^[0-9]+ add R([0-9]+) R(-?[0-9]+) R(-?[0-9]+)",
    "^[0-9]+ sub R([0-9]+) R(-?[0-9]+) R(-?[0-9]+)",
    "^[0-9]+ div R([0-9]+) R(-?[0-9]+) R(-?[0-9]+)",
    "^[0-9]+ ld R([0-9]+) R(-?[0-9]+)",
    "^[0-9]+ st R([0-9]+) R(-?[0-9]+)",
    "^[0-9]+ set R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ bez R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ bgez R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ blez R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ bgtz R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ bltz R([0-9]+) #(-?[0-9]+)",
    "^[0-9]+ ret"};

regex_t instruction_regex_compiled[ARRLEN(instruction_regex)];

// initialise regex parser for compiled instructions
void initilize_parser()
{
    // compile the regex for instruction IDs
    if (regcomp(&instruction_id_regex_compiled, instruction_id_regex, REG_EXTENDED))
    {
        printf("Could not compile regular expression.\n");
        exit(EXIT_FAILURE);
    };

    // loop through all instructions and compile their regexes
    for (int i = 0; i < ARRLEN(instruction_regex); i++)
    {
        // compile the regex for the current instruction
        if (regcomp(&instruction_regex_compiled[i], instruction_regex[i], REG_EXTENDED))
        {
            printf("Could not compile regular expression.\n");
            exit(EXIT_FAILURE);
        };
    }
}

// Load instructions from the specified file
Instruction *load_instructions(char *filename, int *size)
{
    FILE *fp;
    ssize_t nread;
    size_t len = 0;
    char *line = NULL;
    int mem_size = 0;
    int curr_instr = 0;
    Instruction *code_memory;

    if (!filename)
        exit(EXIT_FAILURE);

    fp = fopen(filename, "r");
    if (!fp)
        exit(EXIT_FAILURE);

    while ((nread = getline(&line, &len, fp)) != -1)
    {
        mem_size++;
    }

    *size = mem_size;
    if (!mem_size)
    {
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    code_memory = calloc(mem_size, sizeof(Instruction));
    if (!code_memory)
    {
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    rewind(fp);

    while ((nread = getline(&line, &len, fp)) != -1)
    {
        if (line[nread - 1] == '\n')
        {
            line[nread - 1] = '\0';
        }
        parse_instructions(&code_memory[curr_instr], line, curr_instr);
        curr_instr++;
    }

    free(line);
    fclose(fp);
    return code_memory;
}

// parse the given instructions
void parse_instructions(Instruction *instr, char *line, int no)
{
    strcpy(instr->instruction, line);
    instr->instruction[strlen(line)] = '\0';
    instr->instruction_no = no;

    char *cursor = line;
    regmatch_t match[1];
    int reg_compile;

    // compile the line with regular expressions
    reg_compile = regexec(&instruction_id_regex_compiled, cursor, 1, match, 0);

    if (reg_compile == REG_NOMATCH)
    {
        printf("Could not parse instruction %d: %s\n", no, line);
        char error_message[100];
        regerror(reg_compile, &instruction_id_regex_compiled, error_message, sizeof(error_message));
        printf("regexec failed: %s at position %d\n", error_message, (int)match[0].rm_so);
        exit(EXIT_FAILURE);
    }

    char cursorCopy[strlen(cursor) + 1];
    strcpy(cursorCopy, cursor);
    cursorCopy[match[0].rm_eo] = 0;

    // remove opcode from the string
    memmove(cursorCopy, cursorCopy + 5, strlen(cursorCopy) - 5 + 1);
    int has_register = has_two_R_letters(cursor, cursorCopy);

    // get opcode index
    instr->opcode = getIndex(instructions, ARRLEN(instructions), cursorCopy, has_register);

    int group;
    int maxGroups = 4;
    int operands[3] = {0};
    regmatch_t tokens[maxGroups];

    if (regexec(&instruction_regex_compiled[instr->opcode], cursor, 4, tokens, 0))
    {
        printf("Could not parse instruction [%d: %s] of type [%d%s]\n", no, line, instr->opcode, cursorCopy + match[0].rm_so);
        exit(EXIT_FAILURE);
    }

    for (group = 1; group < 4; group++)
    {
        if (tokens[group].rm_so == (size_t)-1)
            break; // No more groups

        char cursorCopy[strlen(cursor) + 1];
        strcpy(cursorCopy, cursor);
        cursorCopy[tokens[group].rm_eo] = 0;
        operands[group - 1] = atoi(cursorCopy + tokens[group].rm_so);
        // printf("Group %u: [%2u-%2u]: %s\n", group, tokens[group].rm_so, tokens[group].rm_eo, cursorCopy + tokens[group].rm_so);
    }

    switch (instr->opcode)
    {
        case MUL:
        case ADD:
        case SUB:
        case DIV:
            instr->rd = operands[0];
            instr->rs1 = operands[1];
            instr->op1 = operands[2];
            break;
        case MULL:
        case ADDL:
        case SUBL:
        case DIVL:
            instr->rd = operands[0];
            instr->rs1 = operands[1];
            instr->rs2 = operands[2];
            break;
        case SET:
        case LD:
        case ST:
        case BEZ:
        case BGEZ:
        case BLEZ:
        case BGTZ:
        case BLTZ:
            instr->rd = operands[0];
            instr->op1 = operands[1];
            break;
        case LDL:
        case STL:
            instr->rd = operands[0];
            instr->rs1 = operands[1];
            break;
    }
}

// check if the instruction has two R's
int has_two_R_letters(char *str, char *code)
{
    int count = 0;
    // Iterate through the string and count the number of 'R' letters.
    // If two 'R' letters are found, return TRUE.
    if (strcmp(code, "st") == 0 || strcmp(code, "ld") == 0)
    {
        for (int i = 0; str[i] != '\0'; i++)
        {
            if (str[i] == 'R')
            {
                count++;
                if (count == 2)
                {
                    return TRUE;
                }
            }
        }
    }
    else
    {
        for (int i = 0; str[i] != '\0'; i++)
        {
            if (str[i] == 'R')
            {
                count++;
                if (count == 3)
                {
                    return TRUE;
                }
            }
        }
    }
    // If the string does not contain two 'R' letters, return FALSE.
    return FALSE;
}

int getIndex(char **arr, int len, char *inst, int has_register)
{
    // If the instruction requires a register, append 'l' to the instruction string.
    // Note that the 'l' character is only appended to the instruction string if the instruction
    // is not "set" or "ret", as these two instructions do not require a register.
    if (has_register)
    {
        if (strcmp(inst, "set") != 0 && strcmp(inst, "ret") != 0)
        {
            strcat(inst, "l");
        }
    }

    // printf("%s\n", inst);

    for (int i = 0; i < len; i++)
    {
        // printf("%s, %d\n", inst, strcmp(inst, arr[i]));
        if (strcmp(arr[i], inst) == 0)
        {
            return i;
        }
    }

    return -1;
}

CPU *CPU_init()
{
    CPU *cpu = malloc(sizeof(*cpu));
    if (!cpu)
    {
        return NULL;
    }

    /* Create register files */
    cpu->regs = create_registers(REG_COUNT);

    cpu->regs_copy = create_registers(REG_COUNT);

    return cpu;
}

int *create_memory(int size)
{
    int *memory = malloc(sizeof(int) * size);
    if (!memory)
    {
        return NULL;
    }
    memset(memory, 0, sizeof(int) * size);
    return memory;
}

// =================== STAGES ======================================

void retire_stage(CPU *cpu)
{
    if (cpu->retire_1.occupied){
        cpu->regs[rob.entries[cpu->retire_1.dest_value].destinationReg].value = rob.entries[cpu->retire_1.dest_value].result;
        cpu->regs[rob.entries[cpu->retire_1.dest_value].destinationReg].tag = -1;
        cpu->regs[rob.entries[cpu->retire_1.dest_value].destinationReg].status = TRUE; 
        rob.entries[cpu->retire_1.dest_value].destinationReg = -1;
        rob.entries[cpu->retire_1.dest_value].result = -1;
        rob.entries[cpu->retire_1.dest_value].completed = FALSE;
    }
}

// Writeback Stage
static int writeback_stage(CPU *cpu)
{
    if (cpu->writeback_1.occupied)
    {
        Instruction *inst = cpu->writeback_1.inst;
        simulation_count += 1;
        switch (inst->opcode)
        {
            case MUL:
            case ADD:
            case SUB:
            case DIV:
            case MULL:
            case ADDL:
            case SUBL:
            case DIVL:
            case SET:
            case LD:
            case LDL:
                rob.entries[cpu->writeback_1.dest_value].result = cpu->writeback_1.result;
                rob.entries[cpu->writeback_1.dest_value].completed = TRUE;
                break;
            case RET:
                return TRUE;
                break;
        }
        cpu->retire_1 = cpu->writeback_1;
        cpu->writeback_1.occupied = FALSE;
    }
    return 0;
}

// Memory 2 Stage
void memory2_stage(CPU *cpu)
{
    if (cpu->mem2.occupied)
    {
        Instruction *inst = cpu->mem2.inst;
        Stage *s = &cpu->mem2;
        switch (inst->opcode)
        {
        case LD:
        case LDL:
            // changed memeory address index
            cpu->mem2.result = cpu->data_mem[(cpu->mem2.addr)/4];
            break;
        case ST:
        case STL:
            cpu->data_mem[s->dest_value/4] = s->src1_value;
            break;
        }
        switch (inst->opcode)
        {
        case LD:
        case LDL:
            cpu->memory_bubble.reg = inst->rd;
            cpu->memory_bubble.val = cpu->mem2.result;
            cpu->memory_bubble.valid = TRUE;
            break;
        }
    }
}

// Memory 1 Stage
void memory1_stage(CPU *cpu)
{
    Instruction *inst = cpu->mem1.inst;
    Stage *s = &cpu->mem1;
    if (cpu->mem1.occupied)
    {
        switch (inst->opcode)
        {
        case LD:
        case LDL:
        case ST:
        case STL:
            s->addr = s->src1_value;
            break;
        }
    }
}

// Branch Stage
void branch_stage(CPU *cpu) {
    Instruction *inst = cpu->branch.inst;
    Stage *s = &cpu->branch;
    int actual_outcome = 0;
    int addr;

    if(cpu->branch.occupied)
    {
        switch (inst->opcode)
        {
        case BGEZ:
            if(s->src2_value >= 0){
                actual_outcome = 1;
            }
            updateBranchPredictor(cpu, s->src1_value, actual_outcome);
            break;
        case BLEZ:
            if(s->src2_value <= 0){
                actual_outcome = 1;
            }
            updateBranchPredictor(cpu, s->src1_value, actual_outcome);
            break;
        case BGTZ:
            if(s->src2_value > 0){
                actual_outcome = 1;
            }
            updateBranchPredictor(cpu, s->src1_value, actual_outcome);
            break;
        case BLTZ:
            if(s->src2_value < 0){
                actual_outcome = 1;
            }
            updateBranchPredictor(cpu, s->src1_value, actual_outcome);
            break;
        case BEZ:
            if(s->src2_value == 0){
                actual_outcome = 1;
            }
            updateBranchPredictor(cpu, s->src1_value, actual_outcome);
            break;
        }
    }
}

// flush or squash all wrong fetched instructions
void flushStages(CPU *cpu){
    cpu->div.occupied = FALSE;
    cpu->mul.occupied = FALSE;
    cpu->add.occupied = FALSE;
    cpu->read_registers.occupied = FALSE;
    cpu->analyze.occupied = FALSE;
    cpu->decode.occupied = FALSE;
    cpu->fetch.occupied = FALSE;
    cpu->halt_flag.halt = FALSE;
    cpu->halt_flag.end_halt = FALSE;
    for (int i = 0; i < REG_COUNT; i++)
    {
        cpu->regs[i].is_writing = FALSE;
    }
}

// Div Stage
void div_stage(CPU *cpu)
{
    Instruction *inst = cpu->div.inst;
    Stage *s = &cpu->div;
    if (cpu->div.occupied)
    {
        switch (inst->opcode)
        {
        case DIV:
        case DIVL:
            if(s->src2_value == 0){
                printf("\n\nFloating point exception occured..\n");
                exit(1);
            }else{
                s->result = s->src1_value / s->src2_value;
            }
            cpu->div_bubble.reg = inst->rd;
            cpu->div_bubble.val = s->result;
            cpu->div_bubble.valid = TRUE;
            break;
        }
    }
}

// Mul Stage
void mul_stage(CPU *cpu)
{
    Instruction *inst = cpu->mul.inst;
    Stage *s = &cpu->mul;
    if (cpu->mul.occupied)
    {
        switch (inst->opcode)
        {
        case MUL:
        case MULL:
            s->result = s->src1_value * s->src2_value;
            s->result = s->src1_value * s->src2_value;
            cpu->mul_bubble.reg = inst->rd;
            cpu->mul_bubble.val = s->result;
            cpu->mul_bubble.valid = TRUE;
            break;
        }
    }
}

// Add Stage
void add_stage(CPU *cpu)
{
    Stage *s = &cpu->add;
    if (cpu->add.occupied)
    {
        switch (s->opcode)
        {
        case ADD:
        case ADDL:
            s->result = s->src1_value + s->src2_value;
            break;
        case SUB:
        case SUBL:
            s->result = s->src1_value - s->src2_value;
            break;
        case SET:
            s->result = s->src1_value;
            break;
        }
    }
}


void issue_stage(CPU *cpu, ReorderBuffer *rbo)
{
    Instruction *inst = cpu->read_registers.inst;
    Stage *s = &cpu->read_registers;
}

// Read Register Stage
void read_registers_stage(CPU *cpu)
{
    Instruction *inst = cpu->read_registers.inst;
    Stage *s = &cpu->read_registers;
    if (cpu->read_registers.occupied)
    {
        switch (inst->opcode)
        {
        case ADDL:
        case SUBL:
        case MULL:
        case DIVL:
            s->dest_value = ROB_Enqueue(cpu, s->dest_value);
            // cpu->decode.dest_value = inst->rd;
            // cpu->decode.src1_value = inst->rs1;
            // cpu->decode.src2_value = inst->rs2;
            break;
        case ADD:
        case SUB:
        case MUL:
        case DIV:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->rs1;
            cpu->decode.src2_value = inst->op1;
            break;
        case LD:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->op1;
            break;
        case LDL:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->rs1;
            break;
        case ST:
            cpu->decode.dest_value = inst->op1;
            cpu->decode.src1_value = inst->rd;
            break;
        case STL:
            cpu->decode.dest_value = inst->rs1;
            cpu->decode.src1_value = inst->rd;
            break;
        case SET:
            s->dest_value = ROB_Enqueue(cpu, s->dest_value);
            RS_Enqueue(cpu, s->inst->opcode, s->src1_value, s->src2_value, s->dest_value);
            break;
        case BEZ:
        case BGEZ:
        case BLEZ:
        case BGTZ:
        case BLTZ:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->op1;
            break;
        }
    }
}

// Analyze Stage (Empty)
void analyze_stage(CPU *cpu) {}

// Decode Stage
void decode_stage(CPU *cpu)
{
    if (cpu->decode.occupied)
    {
        Instruction *inst = cpu->decode.inst;
        switch (inst->opcode)
        {
        case ADDL:
        case SUBL:
        case MULL:
        case DIVL:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->rs1;
            cpu->decode.src2_value = inst->rs2;
            break;
        case ADD:
        case SUB:
        case MUL:
        case DIV:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->rs1;
            cpu->decode.src2_value = inst->op1;
            break;
        case LD:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->op1;
            break;
        case LDL:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->rs1;
            break;
        case ST:
            cpu->decode.dest_value = inst->op1;
            cpu->decode.src1_value = inst->rd;
            break;
        case STL:
            cpu->decode.dest_value = inst->rs1;
            cpu->decode.src1_value = inst->rd;
            break;
        case SET:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->op1;
            break;
        case BEZ:
        case BGEZ:
        case BLEZ:
        case BGTZ:
        case BLTZ:
            cpu->decode.dest_value = inst->rd;
            cpu->decode.src1_value = inst->op1;
            break;
        }
    }
}

// Fetch Stage
void fetch_stage(CPU *cpu)
{
    if (!cpu->fetch.occupied)
    {
        if (cpu->pc >= cpu->code_size)
        {
            // reached end of the code nothing to fetch
            return;
        }

        if(cpu->flush){
            return;
        }

        cpu->fetch.pc = cpu->pc;
        cpu->fetch.inst = &cpu->code_mem[cpu->pc];

        if(predictBranchOutcome(cpu->pc) && cpu->fetch.inst->opcode >= 13 && cpu->fetch.inst->opcode <= 17){
            cpu->pc = cpu->fetch.inst->op1/4;
        }else{
            cpu->pc += 1;
        }
        cpu->fetch.occupied = TRUE;
    }
}


void end_of_clock_cycle(CPU *cpu)
{
    switch(cpu->issue.opcode){
        case ADD:
        case ADDL:
        case SUB:
        case SUBL:
        case SET:
            if(!cpu->add.occupied){
                cpu->add = cpu->issue;
                cpu->issue.occupied = FALSE;
            }
    }

    /* Read Registers stage */
    if (!cpu->issue.occupied)
    {
        get_RS(cpu);
        cpu->read_registers.occupied = FALSE;
    }

    /* Analyze stage */
    if (!cpu->read_registers.occupied)
    {
        cpu->read_registers = cpu->analyze;
        cpu->analyze.occupied = FALSE;
    }

    /* Decode stage */
    if (!cpu->analyze.occupied)
    {
        cpu->analyze = cpu->decode;
        cpu->decode.occupied = FALSE;
    }

    /* Fetch stage */
    if (cpu->fetch.occupied && !cpu->decode.occupied)
    {
        cpu->decode = cpu->fetch;
        cpu->fetch.occupied = FALSE;
    }
}
// ============================ OUTPUT =============================

void print_output(char *filename, char *output)
{
    FILE *fp = fopen(filename, "w");
    if (fp == NULL)
    {
        printf("Error opening file %s\n", filename);
        return;
    }
    fprintf(fp, "%s", output);
    fclose(fp);
}

void print_instruction(char *stage, Stage s)
{
    if (s.occupied)
    {
        printf("%s", stage);
        printf("%*c", 10, ' ');
        printf(": ");
        // printf("%s R%d #%d #%d", instructions[s.inst->opcode], s.inst->rd, s.src1_value, s.src2_value);
        printf("%s\n", s.inst);
        // printf("\n");
    }
}

// stage output
void print_instruction_info(CPU *cpu, int cycle)
{

    printf("======================================================\n");
    printf("Clock Cycle #: %d\n", cycle + 1);
    printf("-------------------------------------------------------\n");
    print_instruction("RS  ", cpu->retire_1);
    print_instruction("WB  ", cpu->writeback_1);
    print_instruction("MEM2", cpu->mem2);
    print_instruction("MEM1", cpu->mem1);
    print_instruction("BR  ", cpu->branch);
    print_instruction("DIV ", cpu->div);
    print_instruction("MUL ", cpu->mul);
    print_instruction("ADD ", cpu->add);
    print_instruction("IS  ", cpu->issue);
    print_instruction("RR  ", cpu->read_registers);
    print_instruction("IA  ", cpu->analyze);
    print_instruction("ID  ", cpu->decode);
    print_instruction("IF  ", cpu->fetch);
}

// =================================================================

/*
 * This function de-allocates CPU cpu.
 */
void CPU_stop(CPU *cpu)
{
    free(cpu->regs);
    free(cpu);
}

/*
 * This function prints the content of the registers.
 */
void print_registers(CPU *cpu)
{
    printf("================================\n\n");

    printf("=============== STATE OF ARCHITECTURAL REGISTER FILE ==========\n\n");

    printf("--------------------------------\n");
    for (int reg = 0; reg < REG_COUNT; reg++)
    {
        printf("REG[%2d]   |   Value=%d  \n", reg, cpu->regs[reg].value);
        printf("--------------------------------\n");
    }
    printf("================================\n\n");
}

void print_display(CPU *cpu, int cycle)
{
    printf("================================\n");
    printf("Clock Cycle #: %d\n", cycle);
    printf("--------------------------------\n");

    for (int reg = 0; reg < REG_COUNT; reg++)
    {
        printf("REG[%2d]   |   Value=%d  \n", reg, cpu->regs[reg].value);
        printf("--------------------------------\n");
    }
    printf("================================\n");
    printf("\n");
}

// load memeory map and read into array
int load_memory_map(char *filename, CPU *cpu)
{
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        printf("Error opening memory map file: %s\n", filename);
        exit(1);
    }

    int address, value;
    int num_values = 0;
    char buff[512];

    while (fscanf(fp, "%d", &value) == 1)
    {
        if (num_values >= MEMORY_SIZE)
        {
            printf("Error: Address %x exceeds maximum memory size of %d\n", num_values, MEMORY_SIZE);
            exit(1);
        }
        cpu->data_mem[num_values] = value;
        num_values++;
    }

    fclose(fp);

    return num_values;
}

/*
 *  CPU simulation loop
 */
int CPU_run(CPU *cpu, char *filename)
{
    // Initialize program counter (PC)
    int pc = 0;
    // Initialize cycle counter
    int cycle_count = 0;
    // Initialize stall counter
    int stall_count = 0;
    // Initialize instruction counter
    int instruction_count = 0;

    RS_Init();
    ROB_Init();

    // initialize parser
    initilize_parser();

    // Initialize branch predictor
    initBranchPredictor();

    // allocate memory for data_mem (memory_map)
    memset(cpu->data_mem, 0, sizeof(int) * MEMORY_SIZE);

    // load memory map file to data_mem array
    int num_values = load_memory_map("memory_map.txt", cpu);

    // program counter
    cpu->pc = 0;

    // flush
    cpu->flush = 0;

    // code memory with instructions
    cpu->code_mem = load_instructions(filename, &instruction_count);

    // code size (instructions count)
    cpu->code_size = instruction_count;

    int PAUSE = FALSE;
    cpu->halt_flag.halt = FALSE;

    while(!PAUSE)
    {
        retire_stage(cpu);
        writeback_stage(cpu);
        add_stage(cpu);
        read_registers_stage(cpu);
        analyze_stage(cpu);
        decode_stage(cpu);
        fetch_stage(cpu);
        print_instruction_info(cpu, cpu->clockCycle);
        end_of_clock_cycle(cpu);

        printf("\n Register Values \n");
        for(int i=0;i<REG_COUNT;i++){
            printf("R%d: [%d, %d, %d]\n", i, cpu->regs[i].status, cpu->regs[i].tag, cpu->regs[i].value);
        }
        printf("\n Reorder Buffer \n");
        for(int i=0;i<ARRLEN(rob.entries);i++){
            printf("R0B%d: [dest: %d, result: %d, e: %d, completed: %d]\n", i, rob.entries[i].destinationReg, rob.entries[i].result, rob.entries[i].exception, rob.entries[i].completed);
        }
        printf("=================\n\n");
        cpu->clockCycle++;

    }

    // loop through stages
    // while (!PAUSE)
    // {
    //     if (writeback_stage(cpu))
    //     {
    //         PAUSE = TRUE;
    //     }
    //     memory2_stage(cpu);
    //     memory1_stage(cpu);
    //     branch_stage(cpu);
    //     div_stage(cpu);
    //     mul_stage(cpu);
    //     add_stage(cpu);
    //     read_registers_stage(cpu);
    //     analyze_stage(cpu);
    //     decode_stage(cpu);
    //     fetch_stage(cpu);
    //     // print_instruction_info(cpu, cpu->clockCycle);
    //     end_of_clock_cycle(cpu);
    //     // incrementclock cycle
    //     cpu->clockCycle++;
    //     print_display(cpu,cpu->clockCycle);
    // }

    // write memeory map to text file
    // input: filename.txt output: filename_output.txt
    // output filename
    // char* output_filename = (char*) malloc(strlen(filename) + 11);
    // char *fle = strrchr(filename, '/');
    // if(fle != NULL){
    //     fle++;
    //     strcpy(filename, fle);
    // }
    // sprintf(output_filename, "mmap_%s", filename);
    // FILE *fpp = fopen(output_filename, "w+");
    // if(fpp == NULL){
    //     printf("Error: could not open file \n");
    //     return 1;
    // }
    // for (int i = 0; i < num_values; i++) {
    //     fprintf(fpp, "%d ", cpu->data_mem[i]); // write each element of the array to the file
    // }
    // fclose(fpp);
    // free(output_filename);

    // simulation output
    print_registers(cpu);
    printf("Stalled cycles due to data hazard: %d\n", cpu->stalled_cycles);
    printf("Total execution cycles: %d\n", cpu->clockCycle);
    printf("Total instruction simulated: %d\n", simulation_count);
    printf("IPC: %f\n", (float)simulation_count / cpu->clockCycle);

    return 0;
}

// create registers
Register *
create_registers(int size)
{
    Register *regs = malloc(sizeof(*regs) * size);
    if (!regs)
    {
        return NULL;
    }
    for (int i = 0; i < size; i++)
    {
        regs[i].status = 0;
        regs[i].tag = 0;
        regs[i].value = 0;
    }
    return regs;
}

// ROB initialization
void ROB_Init() {
    rob.head = rob.tail = 0;
    for (int i = 0; i < ROB_SIZE; i++) {
        rob.entries[i].completed = TRUE;
        rob.entries[i].exception = FALSE;
        rob.entries[i].result = -1;
        rob.entries[i].destinationReg = -1;
        rob.entries[i].ROBid = i;
    }
}

// check if rob is full
bool ROB_IsFull() {
    return (rob.tail + 1) % ROB_SIZE == rob.head;
}

// check if rob is empty
bool ROB_IsEmpty() {
    return rob.head == rob.tail;
}

// add entry to rob
int ROB_Enqueue(CPU *cpu, int destReg) {
    if (ROB_IsFull(rob)) {
        return -1;  // ROB is full
    }
    int ROBid = rob.tail;
    cpu->regs[destReg].tag = ROBid;
    rob.tail = (rob.tail + 1) % ROB_SIZE;
    rob.entries[ROBid].ROBid = ROBid;
    rob.entries[ROBid].destinationReg = destReg;
    rob.entries[ROBid].completed = FALSE;
    return ROBid;
}

// update rob result
void ROB_Update(int ROBid, int result) {
    rob.entries[ROBid].result = result;
}

// commit rob
void ROB_Commit(int ROBid) {
    rob.entries[ROBid].completed = false;
}

// check if rob is ready
bool ROB_IsReady(int ROBid) {
    return rob.entries[ROBid].completed && !rob.entries[ROBid].exception;
}

void RS_Init() {
    rs.head = rs.tail = 0;
    for (int i = 0; i < RS_SIZE; i++) {
        rs.entries[i].valid = false;
        rs.entries[i].src1_ready = rs.entries[i].src2_ready = false;
    }
}

bool RS_IsFull() {
    return (rs.tail + 1) % RS_SIZE == rs.head;
}

bool RS_IsEmpty() {
    return rs.head == rs.tail;
}

int RS_Enqueue(CPU *cpu, int opcode, int operand1, int operand2, int destReg) {
    if (RS_IsFull()) {
        return -1;  // RS is full
    }
    int RSEntryId = rs.tail;
    rs.tail = (rs.tail + 1) % RS_SIZE;
    rs.entries[RSEntryId] = cpu->read_registers;
    rs.entries[RSEntryId].opcode = cpu->read_registers.inst->opcode;
    rs.entries[RSEntryId].valid = true;
    rs.entries[RSEntryId].src1_ready = rs.entries[RSEntryId].src2_ready = false;
    return RSEntryId;
}

bool RS_IsReady(int RSEntryId) {
    return rs.entries[RSEntryId].valid && rs.entries[RSEntryId].src1_ready && rs.entries[RSEntryId].src2_ready;
}

void RS_Clear(int RSEntryId) {
    rs.entries[RSEntryId].valid = false;
}

void get_RS(CPU *cpu){
    int RSEntryId = rs.tail-1;
    cpu->issue = rs.entries[RSEntryId];
}

// Initialize BTB and PT
void initBranchPredictor() {
    for (int i = 0; i < BTB_SIZE; i++) {
        btb[i].tag = -1;
        btb[i].target_address = -1;
    }
    for (int i = 0; i < PT_SIZE; i++) {
        pt[i].counter = 3;
    }
}

// Function to update BTB and PT with actual branch outcome
void updateBranchPredictor(CPU *cpu, int addr, int actual_outcome) {
    Instruction *inst = cpu->branch.inst;
    Stage *s = &cpu->branch;
    
    int pc = inst->instruction_no * 4;

    // Extract BTB index and tag from PC
    int btb_index = (pc >> 2) & 0xF;
    int tag = (pc & PC_TAG) >> 6;

    // Update PT with actual branch outcome
    int pt_index = (pc >> 2) & 0xF;

    if(actual_outcome){
        if(btb[btb_index].tag < 0 || pt[pt_index].counter < 4){
            cpu->flush = TRUE;
            flushStages(cpu);
            cpu->pc = addr/4;
        }
    }else{
        if(btb[btb_index].tag >= 0 && pt[pt_index].counter >= 4){
            cpu->flush = TRUE;
            flushStages(cpu);
            cpu->pc = inst->instruction_no + 1;
        }
    }
    btb[btb_index].tag = tag;
    btb[btb_index].target_address = addr;

    if (actual_outcome) {
        if (pt[pt_index].counter < 7) {
            pt[pt_index].counter++;
        }
    } else {
        if (pt[pt_index].counter > 0) {
            pt[pt_index].counter--;
        }
    }
}

// Function to predict branch outcome
int predictBranchOutcome(int pc) {

    int pt_index;

    pt_index = ((pc*4) >> 2) & 0xF;

    // Predict branch outcome based on PT counter value
    if (pt[pt_index].counter >= 4) {
        return 1;   // Predict taken
    } else {
        return 0;   // Predict not-taken
    }
}

