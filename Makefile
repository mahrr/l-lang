CC = gcc
CFLAGS = -std=c11 -g -Wall -Wextra -pedantic -o
PFLAGS = -std=c11 -g -Wall -Wextra -pedantic -pg -O3 -o 
OFLAGS = -std=c11 -march=native -g -Wall -Wextra -pedantic -O3 -o
SRCDIR = ./src
OUTDIR = ./bin
TARGET = raven
CLIBS = -lm

# debug
default:
	@$(CC) $(CFLAGS) $(OUTDIR)/$(TARGET) $(SRCDIR)/*.c $(CLIBS)

# profiling
profile:
	@$(CC) $(PFLAGS) $(OUTDIR)/$(TARGET) $(SRCDIR)/*.c $(CLIBS)

# release
opt:
	@$(CC) $(OFLAGS) $(OUTDIR)/$(TARGET) $(SRCDIR)/*.c $(CLIBS)


run:
	@$(OUTDIR)/$(TARGET)

clean:
	$(RM) $(OUTDIR)/*


# lexer tests
LEX_TEST = tests/test_lexer.c
LEX_TEST_TARGET = lex_test
LEX_TEST_RELATED = src/lexer.c  \
				   src/error.c  \
				   src/list.c   \
				   src/debug.c  \
				   src/strutil.c

LEX_INCLUDE = -I./src
LEX_PRINT = -DPRINT_TOKENS

# compile the lexer test and run it
lextest:
	@$(CC) $(CFLAGS) $(OUTDIR)/$(LEX_TEST_TARGET) \
	 $(LEX_TEST_RELATED) $(LEX_TEST) $(LEX_INCLUDE) $(CLIBS)
	@$(OUTDIR)/$(LEX_TEST_TARGET)

# compile the lexer test with PRINT_TOKENS macro defined and run it
plextest:
	@$(CC) $(CFLAGS) $(OUTDIR)/$(LEX_TEST_TARGET) \
	 $(LEX_PRINT) $(LEX_TEST_RELATED) $(LEX_TEST) \
	 $(LEX_INCLUDE) $(CLIBS)
	@$(OUTDIR)/$(LEX_TEST_TARGET)

# compile the lexer test only
clextest:
	@$(CC) $(CFLAGS) $(OUTDIR)/$(LEX_TEST_TARGET) \
	 $(LEX_TEST_RELATED) $(LEX_TEST) $(LEX_INCLUDE) $(CLIBS)
