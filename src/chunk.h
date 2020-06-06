#ifndef raven_chunk_h
#define raven_chunk_h

#include "common.h"
#include "value.h"

typedef enum {         // Immediate Arguments
    // Loading
    OP_LOAD_TRUE,
    OP_LOAD_FALSE,
    OP_LOAD_NIL,
    OP_LOAD_CONST,     // 1-byte constant index

    // TODO: optimize the STORE, LOAD, STORE pattern.
    // Load from, store into X register
    OP_LOAD,
    OP_STORE,
    
    // Arithmetics
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,

    // Comparison
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTQ,
    OP_GT,
    OP_GTQ,

    // Variables
    OP_DEF_GLOBAL,     // 1-byte name index
    OP_SET_GLOBAL,     // 1-byte name index
    OP_GET_GLOBAL,     // 1-byte name index
    OP_SET_LOCAL,      // 1-byte slot index
    OP_GET_LOCAL,      // 1-byte slot index

    // Branching
    OP_CALL,           // 1-byte arguments count
    OP_JMP,            // 2-bytes offset
    OP_JMP_BACK,       // 2-bytes offset
    OP_JMP_FALSE,      // 2-bytes offset
    OP_JMP_POP_FALSE,  // 2-bytes offset
    
    OP_POP,
    OP_POPN,           // 1-byte count
    OP_NOT,

    OP_RETURN,
    OP_EXIT
} Opcode;

// Line encoding
typedef struct {
    int line;
    int offset;
} Line;

typedef struct {
    // Dynamic array of the opcodes.
    int count;
    int capacity;
    uint8_t *opcodes;

    // Dynamic array of the lines corresponding to opcodes.
    // This array is not in sync with the opcodes array, the
    // lines are encoded in some sort of Run-length format.
    int lines_count;
    int lines_capacity;
    Line *lines;

    ValueArray constants;
} Chunk;

// Initialize the chunk state.
void init_chunk(Chunk *chunk);

// Free the chunk memory.
void free_chunk(Chunk *chunk);

// Add a byte to the chuck, and register it with the provided line.
void write_byte(Chunk *chunk, uint8_t byte, int line);

// Add a constant to the constants table, and return its index.
int write_constant(Chunk *chunk, Value value);

// Decode a line corresponing to a given instruction offset
int decode_line(Chunk *chunk, int offset);

#endif
