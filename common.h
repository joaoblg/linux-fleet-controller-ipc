#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

// --- Configurações Gerais ---
#define FIFO_PRINCIPAL "/tmp/taxi_fifo"
#define CLIENT_FIFO_TEMPLATE "/tmp/cli_%d_fifo"
#define VEICULO_FIFO_TEMPLATE "/tmp/vei_%d_fifo"
#define MAX_NAME 50
#define MAX_LOCAL 100
#define MAX_CLIENTES 30
#define MAX_VIAGENS 100
#define MAX_FROTA_GLOBAL 30

// --- Tipos de Mensagens ---
#define PEDIDO_LOGIN 1
#define PEDIDO_AGENDAR 2
#define PEDIDO_SAIR 3
#define PEDIDO_CANCELAR 4
#define PEDIDO_CONSULTAR 5

// --- Códigos de Resposta ---
#define LOGIN_OK 1
#define LOGIN_USER_TAKEN 2
#define LOGIN_FULL 3
#define AGENDAMENTO_OK 4
#define MSG_VEICULO_CHEGOU 5
#define MSG_CONSULTA 6

// --- Tipos de Mensagens (Veículo) ---
#define CMD_ENTRAR 1

// --- 1. DEFINIR AS STRUCTS PEQUENAS PRIMEIRO ---

typedef struct { 
    char username[MAX_NAME]; 
} PedidoLogin;

typedef struct { 
    int hora; 
    int distancia; 
    char local[MAX_LOCAL]; 
} PedidoAgendar;

typedef struct { 
    int id_viagem; 
} PedidoCancelar;

// --- 2. DEFINIR AS STRUCTS GRANDES DEPOIS ---
// (Agora o compilador já conhece 'PedidoCancelar')

typedef struct {
    int tipo_pedido; 
    pid_t pid_cliente; 
    union {
        PedidoLogin login;
        PedidoAgendar agendamento;
        PedidoCancelar cancelamento;
    } dados;
} MensagemCliente;

typedef struct {
    int tipo_msg;
    pid_t pid_veiculo; 
    char payload[512]; // Texto da resposta
} MensagemSistema;

typedef struct {
    int tipo_cmd;
    char destino[MAX_LOCAL];
} MensagemVeiculo;

// --- 3. ESTRUTURAS INTERNAS DO CONTROLADOR ---

typedef struct { 
    pid_t pid; 
    char username[MAX_NAME]; 
    int em_viagem; 
} Cliente;

typedef struct {
    int id; 
    int hora_inicio;
    int distancia;
    char local[MAX_LOCAL];
    pid_t pid_cliente; 
    int notificacao_atraso_enviada;
} Viagem;

typedef struct {
    pid_t pid_veiculo;
    pid_t pid_cliente;
    int fd_pipe_telemetria; 
    int hora_fim;
    int distancia;
    int id;
    int kms_percorridos;
} VeiculoEmCurso;

#endif