# Simulador de memoria virtual con paginacion de dos niveles.
# Targets exigidos por la rubrica: all, clean, run.
# Adicionales: test, valgrind, stress, barrido, diferencial.

CC     = gcc
# -Wall -Werror -std=c99 son obligatorios; -g solo para poder depurar con gdb/valgrind.
CFLAGS = -Wall -Werror -std=c99 -g

# Tamano de pagina = 2^PAGE_BITS bytes (10 = 1 KB ... 16 = 64 KB; 12 = 4 KB por
# defecto). PT1 y PT2 se derivan en src/config.h para que la VA siga sumando 32 bits.
#   make PAGE_BITS=13        -> paginas de 8 KB
PAGE_BITS ?= 12
CFLAGS    += -DBITS_OFFSET=$(PAGE_BITS)

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
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(OBJDIR)/page_bits.stamp | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Registra el PAGE_BITS de la ultima compilacion. Solo se reescribe cuando cambia,
# y como todos los .o dependen de el, cambiar PAGE_BITS recompila todo sin clean.
$(OBJDIR)/page_bits.stamp: FORCE | $(OBJDIR)
	@echo '$(PAGE_BITS)' | cmp -s - $@ || echo '$(PAGE_BITS)' > $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: $(BIN)
	./$(BIN) $(ENTRADA) $(ARGS)

# Verificaciones automaticas: valores leidos, swap, politicas e invariantes.
test: $(BIN)
	bash tests/verificar.sh

# valgrind sobre todas las pruebas con las dos politicas; falla si hay fugas o errores.
valgrind: $(BIN)
	@for t in tests/t*.txt; do for p in lru fifo; do \
	  valgrind --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1 -q \
	    ./$(BIN) $$t -p $$p > /dev/null || exit 1; \
	  echo "valgrind OK: $$t -p $$p"; \
	done; done

# Prueba de estres: 100000 accesos 80/20 sobre 256 paginas, con las dos politicas.
stress: $(BIN)
	bash tests/generar_pruebas.sh estres
	@for p in lru fifo; do echo "== -p $$p"; ./$(BIN) tests/estres_grande.txt -p $$p; echo; done

# Prueba diferencial: 200 trazas aleatorias contra un modelo de referencia en
# Python (fallos, reemplazos y el valor de cada read). Requiere python3.
diferencial: $(BIN)
	python3 tests/diferencial.py 200 1

# Tabla y grafico de fallos contra tamano de memoria: make barrido TRAZA=tests/t3_estres.txt
TRAZA ?= tests/t4_localidad_8020.txt
barrido: $(BIN)
	bash tests/barrido.sh $(TRAZA)

clean:
	rm -rf $(OBJDIR) $(BIN) tests/estres_grande.txt

-include $(wildcard $(OBJDIR)/*.d)

.PHONY: all run clean test valgrind stress barrido diferencial FORCE
FORCE:
