#include <stdio.h>

#include "chunk.h"
#include "debug.h"

static int basic_instruction(const char *tag, int offset) {
    printf("%s\n", tag);
    return offset + 1;
}

static int const_instruction(const char *tag, Chunk *chunk, int offset) {
    uint8_t constant_index = chunk->opcodes[offset + 1];
    printf("%-16s %4d '", tag, constant_index);
    print_value(chunk->constants.values[constant_index]);
    printf("'\n");
    return offset + 2;
}

int disassemble_instruction(Chunk *chunk, int offset) {
    printf("%04d", offset);

    int line = decode_line(chunk, offset);
    int instruction = chunk->opcodes[offset];
        
    if (offset > 0 && line == decode_line(chunk, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", line);
    }
    
    switch (instruction) {
    case OP_LOAD_TRUE:
        return basic_instruction("LOAD_TRUE", offset);
        
    case OP_LOAD_FALSE:
        return basic_instruction("LOAD_FALSE", offset);
        
    case OP_LOAD_NIL:
        return basic_instruction("LOAD_NIL", offset);
        
    case OP_LOAD_CONST:
        return const_instruction("LOAD_CONST", chunk, offset);
        
    case OP_ADD:
        return basic_instruction("ADD", offset);
        
    case OP_SUB:
        return basic_instruction("SUB", offset);
        
    case OP_MUL:
        return basic_instruction("MUL", offset);
        
    case OP_DIV:
        return basic_instruction("DIV", offset);
        
    case OP_MOD:
        return basic_instruction("MOD", offset);

    case OP_NEG:
        return basic_instruction("NEG", offset);
        
    case OP_RETURN:
        return basic_instruction("RETURN", offset);
    }

    assert(0);
}

void disassemble_chunk(Chunk *chunk, const char *name) {
    printf("## %s ##\n", name);

    for (int offset = 0; offset < chunk->count; ) {
        offset = disassemble_instruction(chunk, offset);
    }
}
