#include "common.h"
#include <pthread.h>

// --- Variáveis Globais ---
int fd_servidor;        
int fd_cliente;         
char nome_fifo_cliente[100]; 

// Variáveis partilhadas e sincronização
pid_t pid_veiculo_ativo = 0; 
pthread_mutex_t trinco = PTHREAD_MUTEX_INITIALIZER; 
int continuar = 1; 


// --- Handler de Saída (Ctrl+C ou comando 'terminar') ---
void trata_sigint_cliente(int s) {
    if (s != 0) printf("\nCliente a terminar (Ctrl+C)...\n");
    
    continuar = 0; // Mandar as threads pararem

    // Avisar servidor
    MensagemCliente msg_sair;
    msg_sair.tipo_pedido = PEDIDO_SAIR;
    msg_sair.pid_cliente = getpid();
    write(fd_servidor, &msg_sair, sizeof(MensagemCliente));
    
    close(fd_servidor);
    close(fd_cliente);
    unlink(nome_fifo_cliente);
    exit(0);
}

// ============================================================================
// THREAD SECUNDÁRIA: Escutar o Servidor/Veículo
// ============================================================================
void* tarefa_recebe_respostas(void* arg) {
    MensagemSistema msg_recebida;

    while (continuar) {
        // Leitura BLOQUEANTE (a thread dorme aqui até chegar algo)
        int bytes_lidos = read(fd_cliente, &msg_recebida, sizeof(MensagemSistema));

        if (bytes_lidos == sizeof(MensagemSistema)) {
            
            if (msg_recebida.tipo_msg == AGENDAMENTO_OK) {
                printf("\n[FIFO] Viagem agendada com sucesso!\n");
            } 
            else if (msg_recebida.tipo_msg == MSG_VEICULO_CHEGOU) {
                printf("\n[FIFO] O SEU VEÍCULO (PID: %d) CHEGOU!\n", msg_recebida.pid_veiculo);
                printf("  > Use o comando: entrar\n");
                
                // Proteger a escrita na variável partilhada
                pthread_mutex_lock(&trinco);
                pid_veiculo_ativo = msg_recebida.pid_veiculo;
                pthread_mutex_unlock(&trinco);
            }
            else if (msg_recebida.tipo_msg == MSG_CONSULTA) {
                printf("\n[FIFO] As suas viagens:\n%s", msg_recebida.payload);
            }
            
            // Re-imprimir prompt para não ficar confuso
            printf("Comando > "); 
            fflush(stdout);

        } else if (bytes_lidos == 0) {
            // EOF (Pipe fechado) - em princípio não acontece com o servidor a correr
            continue; 
        } else {
            if (continuar) perror("[ERRO] Leitura do FIFO");
            break;
        }
    }
    return NULL;
}

// ============================================================================
// THREAD PRINCIPAL (MAIN): Ler do Teclado
// ============================================================================
int main(int argc, char *argv[]) {
    setbuf(stdout, NULL); // Disable buffering

    if (argc != 2) {
        printf("Uso: ./cliente <username>\n");
        exit(1);
    }
    char *username = argv[1];

    signal(SIGINT, trata_sigint_cliente);

    // 1. Preparar FIFOs
    sprintf(nome_fifo_cliente, CLIENT_FIFO_TEMPLATE, getpid());
    if (mkfifo(nome_fifo_cliente, 0666) == -1) {
        perror("[ERRO] mkfifo falhou");
        exit(2);
    }

    fd_servidor = open(FIFO_PRINCIPAL, O_WRONLY);
    if (fd_servidor == -1) {
        printf("O controlador não está a correr!\n");
        unlink(nome_fifo_cliente);
        exit(3);
    }

    // Abrir em modo RW para não bloquear e para manter o pipe aberto
    fd_cliente = open(nome_fifo_cliente, O_RDWR); 
    if (fd_cliente == -1) {
        perror("Erro open FIFO cliente");
        unlink(nome_fifo_cliente);
        exit(4);
    }

    // 2. Login (Síncrono - fazemos antes de lançar a thread)
    MensagemCliente msg_envio;
    MensagemSistema msg_resposta;
    
    msg_envio.tipo_pedido = PEDIDO_LOGIN;
    msg_envio.pid_cliente = getpid();
    strncpy(msg_envio.dados.login.username, username, MAX_NAME); 

    printf("A enviar login como '%s'...\n", username);
    write(fd_servidor, &msg_envio, sizeof(MensagemCliente));

    // Esperar resposta (bloqueante)
    read(fd_cliente, &msg_resposta, sizeof(MensagemSistema)); 

    if (msg_resposta.tipo_msg == LOGIN_OK) {
        printf("[Cliente] Login aceite!\n");
    } else {
        printf("[Cliente] ERRO: Login recusado (%d)!\n", msg_resposta.tipo_msg);
        trata_sigint_cliente(0);
    }

    // 3. Lançar Thread de Escuta
    pthread_t tid;
    if (pthread_create(&tid, NULL, tarefa_recebe_respostas, NULL) != 0) {
        perror("Erro pthread_create");
        trata_sigint_cliente(0);
    }

    printf("\nComandos: agendar <hora> <loc> <dist>, consultar, cancelar <id>, entrar, sair\n");
    printf("Comando > ");

    // 4. Loop de Leitura do Teclado
    char buffer_teclado[200];
    while (continuar && fgets(buffer_teclado, sizeof(buffer_teclado), stdin)) {
        
        // Remover \n
        buffer_teclado[strcspn(buffer_teclado, "\n")] = 0;

        if (strlen(buffer_teclado) == 0) {
             printf("Comando > "); 
             continue;
        }

        if (strcmp(buffer_teclado, "sair") == 0 || strcmp(buffer_teclado, "terminar") == 0) {
            trata_sigint_cliente(0);
        }
        else if (strncmp(buffer_teclado, "agendar", 7) == 0) {
            msg_envio.tipo_pedido = PEDIDO_AGENDAR;
            msg_envio.pid_cliente = getpid();
            
            // %99[^0-9] lê o local (com espaços) até encontrar o último número
            int res = sscanf(buffer_teclado, "agendar %d %99[^0-9] %d", 
                   &msg_envio.dados.agendamento.hora, 
                   msg_envio.dados.agendamento.local, 
                   &msg_envio.dados.agendamento.distancia);
            
            // Limpar espaços finais no local
            int len = strlen(msg_envio.dados.agendamento.local);
            if (len > 0 && msg_envio.dados.agendamento.local[len - 1] == ' ') 
                msg_envio.dados.agendamento.local[len - 1] = '\0';

            if (res == 3) {
                write(fd_servidor, &msg_envio, sizeof(MensagemCliente));
            } else {
                printf("[Uso]: agendar <hora> <local> <distancia>\n");
            }
        }
        else if (strncmp(buffer_teclado, "consultar", 9) == 0) {
            msg_envio.tipo_pedido = PEDIDO_CONSULTAR;
            msg_envio.pid_cliente = getpid();
            write(fd_servidor, &msg_envio, sizeof(MensagemCliente));
        }
        else if (strncmp(buffer_teclado, "cancelar", 8) == 0) {
            msg_envio.tipo_pedido = PEDIDO_CANCELAR;
            msg_envio.pid_cliente = getpid();
            int id_c = 0;
            sscanf(buffer_teclado, "cancelar %d", &id_c);
            msg_envio.dados.cancelamento.id_viagem = id_c;
            write(fd_servidor, &msg_envio, sizeof(MensagemCliente));
        }
        else if (strncmp(buffer_teclado, "entrar", 6) == 0) {
            // Ler variável partilhada com segurança
            pthread_mutex_lock(&trinco);
            int veiculo = pid_veiculo_ativo;
            pthread_mutex_unlock(&trinco);

            if (veiculo == 0) {
                printf("[Erro] Nenhum veículo à sua espera.\n");
            } else {
                // Enviar ordem ao veículo
                char nome_fifo_v[100];
                sprintf(nome_fifo_v, VEICULO_FIFO_TEMPLATE, veiculo);
                int fd_v = open(nome_fifo_v, O_WRONLY);
                
                if (fd_v != -1) {
                    MensagemVeiculo mv;
                    mv.tipo_cmd = CMD_ENTRAR;
                    strcpy(mv.destino, ""); 
                    write(fd_v, &mv, sizeof(MensagemVeiculo));
                    close(fd_v);
                    printf(" > Entrou no veículo %d.\n", veiculo);
                    
                    // Reset
                    pthread_mutex_lock(&trinco);
                    pid_veiculo_ativo = 0;
                    pthread_mutex_unlock(&trinco);
                } else {
                    printf("[Erro] Falha ao contactar veículo %d.\n", veiculo);
                }
            }
        }
        else {
            printf("Comando desconhecido.\n");
        }

        printf("Comando > "); 
    }
    
    return 0;
}