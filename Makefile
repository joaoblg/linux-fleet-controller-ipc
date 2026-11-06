# Definições do compilador
CC = gcc
CFLAGS = -Wall -pthread

# Targets Principais
all: controlador cliente veiculo

# Como compilar o Controlador
controlador: controlador.c common.h
	$(CC) -o controlador controlador.c $(CFLAGS)

# Como compilar o Cliente
cliente: cliente.c common.h
	$(CC) -o cliente cliente.c $(CFLAGS)

# Como compilar o Veículo
veiculo: veiculo.c common.h
	$(CC) -o veiculo veiculo.c $(CFLAGS)

# Limpeza (Apagar executáveis e FIFOs antigos)
clean:
	rm -f controlador cliente veiculo
	rm -f /tmp/taxi_fifo
	rm -f /tmp/cli_*_fifo
	rm -f /tmp/vei_*_fifos