# Simulador de memoria virtual con paginacion de dos niveles.
# Targets exigidos por la rubrica: all, clean, run.

CC     = gcc
# -Wall -Werror -std=c99 son obligatorios; -g solo para poder depurar con gdb/valgrind.
CFLAGS = -Wall -Werror -std=c99 -g

BIN    = simulador
SRCDIR = src
OBJDIR = build

FUENTES = $(wildcard $(SRCDIR)/*.c)
OBJETOS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(FUENTES))

# Archivo que usa 'make run'. Se puede cambiar sin editar el Makefile:
#   make run ENTRADA=tests/t3_estres.txt
ENTRADA ?= tests/t1_basico.txt
ARGS    ?=

all: $(BIN)

$(BIN): $(OBJETOS)
	$(CC) $(CFLAGS) -o $@ $^

# -MMD -MP genera los .d con las dependencias de cada .h, para que al tocar un
# header se recompile lo que corresponde y no quede codigo viejo enlazado.
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: $(BIN)
	./$(BIN) $(ENTRADA) $(ARGS)

clean:
	rm -rf $(OBJDIR) $(BIN)

-include $(wildcard $(OBJDIR)/*.d)

.PHONY: all run clean
