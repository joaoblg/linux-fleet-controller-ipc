#include "common.h"
#include <sys/wait.h>
#include <pthread.h> 

// --- Variáveis Globais ---
int fd_fifo_principal; 
int tempo_simulado = 0; 
int limite_frota_configurado = 10; 
long total_kms_percorridos = 0; 

// Listas de dados
Cliente lista_clientes[MAX_CLIENTES];
int num_clientes = 0; 
Viagem lista_viagens[MAX_VIAGENS];
int num_viagens = 0; 
VeiculoEmCurso frota[MAX_FROTA_GLOBAL]; 
int num_veiculos_em_curso = 0;

// Este mutex protege o acesso às listas partilhadas entre threads
pthread_mutex_t trinco = PTHREAD_MUTEX_INITIALIZER;

// Handlers de Sinais 
void trata_sigint(int s) {
    printf("\nControlador a terminar...\n");
    close(fd_fifo_principal);   
    unlink(FIFO_PRINCIPAL);     
    exit(0);                    
}
void trata_sigchld(int s) {
    // Limpa processos zombie sem bloquear
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        ; 
    }
}

// Funções auxiliares
int encontrar_cliente_por_nome(char* username) {
    for (int i = 0; i < num_clientes; i++) {
        if (strcmp(lista_clientes[i].username, username) == 0) return 1; 
    }
    return 0; 
}

const char* encontrar_nome_por_pid(pid_t pid) {
    for (int i = 0; i < num_clientes; i++) {
        if (lista_clientes[i].pid == pid) {
            return lista_clientes[i].username;
        }
    }
    return "Desconhecido";
}

void adicionar_cliente(pid_t pid, char* username) {
    lista_clientes[num_clientes].pid = pid;
    strcpy(lista_clientes[num_clientes].username, username);
    lista_clientes[num_clientes].em_viagem = 0;
    num_clientes++;
}

void enviar_msg_cliente(pid_t pid_cliente, int msg_tipo) {
    char nome_fifo_cliente[100];
    int fd_fifo_cliente;
    MensagemSistema msg_envio; 

    sprintf(nome_fifo_cliente, CLIENT_FIFO_TEMPLATE, pid_cliente);
    fd_fifo_cliente = open(nome_fifo_cliente, O_WRONLY);

    if (fd_fifo_cliente == -1) {
        perror("  [AVISO] Não consegui abrir o FIFO do cliente para responder");
    } else {
        msg_envio.tipo_msg = msg_tipo;
        msg_envio.pid_veiculo = 0; 
        write(fd_fifo_cliente, &msg_envio, sizeof(MensagemSistema));
        close(fd_fifo_cliente);
    }
}

// THREAD 1: Gestão de Pedidos dos Clientes (Lê do FIFO Principal)
void* tarefa_clientes(void* arg) {
    MensagemCliente msg_recebida;

    while (1) {
        int bytes_lidos = read(fd_fifo_principal, &msg_recebida, sizeof(MensagemCliente));
        
        if (bytes_lidos == sizeof(MensagemCliente)) {
            
            // Variáveis locais para decidir o que fazer FORA do lock
            int status_login = 0;
            int enviar_resposta = 0;
            int tipo_resposta = 0;

            switch (msg_recebida.tipo_pedido) {
                
                case PEDIDO_LOGIN:
                    printf("\n[Login] Novo pedido: %s (PID: %d)\n", msg_recebida.dados.login.username, msg_recebida.pid_cliente);
                    
                    // 🔒 LOCK (Apenas para verificar e adicionar)
                    pthread_mutex_lock(&trinco);
                    if (encontrar_cliente_por_nome(msg_recebida.dados.login.username)) {
                        status_login = LOGIN_USER_TAKEN;
                    } else if (num_clientes >= MAX_CLIENTES) {
                        status_login = LOGIN_FULL;
                    } else {
                        status_login = LOGIN_OK;
                        adicionar_cliente(msg_recebida.pid_cliente, msg_recebida.dados.login.username);
                    }
                    pthread_mutex_unlock(&trinco); // 🔓 UNLOCK
                    
                    // Enviar resposta (Lento - fazemos fora do lock)
                    enviar_msg_cliente(msg_recebida.pid_cliente, status_login);
                    break;

                case PEDIDO_AGENDAR:
                    printf("\n[Agenda] Pedido recebido (PID: %d)\n", msg_recebida.pid_cliente);
                    
                    // 🔒 LOCK (Apenas para adicionar à lista)
                    pthread_mutex_lock(&trinco);
                    if (num_viagens < MAX_VIAGENS) {
                        lista_viagens[num_viagens].hora_inicio = msg_recebida.dados.agendamento.hora;
                        lista_viagens[num_viagens].distancia = msg_recebida.dados.agendamento.distancia;
                        strcpy(lista_viagens[num_viagens].local, msg_recebida.dados.agendamento.local);
                        lista_viagens[num_viagens].pid_cliente = msg_recebida.pid_cliente;
                        lista_viagens[num_viagens].notificacao_atraso_enviada = 0; 
                        lista_viagens[num_viagens].id = num_viagens + 1; 
                        num_viagens++;
                        enviar_resposta = 1;
                        tipo_resposta = AGENDAMENTO_OK;
                    } else {
                        printf("  [AVISO] Lista de viagens cheia!\n");
                    }
                    pthread_mutex_unlock(&trinco); // 🔓 UNLOCK
                    
                    if (enviar_resposta) enviar_msg_cliente(msg_recebida.pid_cliente, tipo_resposta);
                    break;
                
                case PEDIDO_CANCELAR:
                    // 🔒 LOCK
                    pthread_mutex_lock(&trinco);
                    printf("\n[Controlador] Cancelamento (PID: %d)\n", msg_recebida.pid_cliente);
                    int id_alvo = msg_recebida.dados.cancelamento.id_viagem;
                    int cancelados = 0;
                    
                    // Cancelar PENDENTES
                    for (int v = 0; v < num_viagens; v++) {
                        if (lista_viagens[v].pid_cliente == msg_recebida.pid_cliente) {
                            if (id_alvo == 0 || lista_viagens[v].id == id_alvo) {
                                printf("  > Viagem Pendente ID %d cancelada.\n", lista_viagens[v].id);
                                lista_viagens[v] = lista_viagens[num_viagens - 1];
                                num_viagens--; v--; cancelados++;
                            }
                        }
                    }
                    // Cancelar EM CURSO
                    for (int f = 0; f < num_veiculos_em_curso; f++) {
                         if (frota[f].pid_cliente == msg_recebida.pid_cliente) {
                             if (id_alvo == 0 || frota[f].id == id_alvo) {
                                 printf("  > A cancelar Veículo %d...\n", frota[f].pid_veiculo);
                                 kill(frota[f].pid_veiculo, SIGUSR1);
                                 cancelados++;
                             }
                         }
                    }
                    if (cancelados == 0) printf("  > Nada a cancelar.\n");
                    pthread_mutex_unlock(&trinco); // 🔓 UNLOCK
                    break;

                case PEDIDO_CONSULTAR:
                    printf("\n[Controlador] Consulta (PID: %d)\n", msg_recebida.pid_cliente);
                    char buffer_lista[512] = "";
                    int encontrou = 0;
                    
                    // 🔒 LOCK (Leitura rápida da memória)
                    pthread_mutex_lock(&trinco);
                    for (int v = 0; v < num_viagens; v++) {
                        if (lista_viagens[v].pid_cliente == msg_recebida.pid_cliente) {
                            char linha[200];
                            sprintf(linha, "> ID: %d | Destino: %s | Hora: %d\n", 
                                    lista_viagens[v].id, lista_viagens[v].local, lista_viagens[v].hora_inicio);
                            strcat(buffer_lista, linha);
                            encontrou = 1;
                        }
                    }
                    pthread_mutex_unlock(&trinco); // 🔓 UNLOCK
                    
                    if (!encontrou) strcpy(buffer_lista, "Nao tem viagens pendentes.\n");

                    // Envio (Lento - fora do lock)
                    char nome_dest[100];
                    sprintf(nome_dest, CLIENT_FIFO_TEMPLATE, msg_recebida.pid_cliente);
                    int fd_d = open(nome_dest, O_WRONLY);
                    if (fd_d != -1) {
                        MensagemSistema msg_r;
                        msg_r.tipo_msg = MSG_CONSULTA;
                        msg_r.pid_veiculo = 0;
                        strcpy(msg_r.payload, buffer_lista);
                        write(fd_d, &msg_r, sizeof(MensagemSistema));
                        close(fd_d);
                    }
                    break;

               case PEDIDO_SAIR:
                    pthread_mutex_lock(&trinco);
                    printf("\n[Controlador] Logout (PID: %d)\n", msg_recebida.pid_cliente);
                    
                    // 1. Verificar se este cliente tem um veículo na estrada
                    for (int f = 0; f < num_veiculos_em_curso; f++) {
                        if (frota[f].pid_cliente == msg_recebida.pid_cliente) {
                            printf("  > O cliente saiu a meio da viagem! A parar veículo %d...\n", frota[f].pid_veiculo);
                            kill(frota[f].pid_veiculo, SIGUSR1); // Manda o táxi parar
                            // Nota: Não removemos da frota aqui. O veículo morre, 
                            // fecha o pipe e a limpeza é feita no 'read==0'.
                        }
                    }

                    // 2. Remover da lista de clientes (Logout)
                    int removido = 0;
                    for (int i = 0; i < num_clientes; i++) {
                        if (lista_clientes[i].pid == msg_recebida.pid_cliente) {
                            lista_clientes[i] = lista_clientes[num_clientes-1];
                            num_clientes--;
                            removido = 1;
                            break;
                        }
                    }
                    if (removido) printf("  > Cliente removido da lista.\n");
                    
                    pthread_mutex_unlock(&trinco);
                    break;
            }
        }
    }
    return NULL;
}

// THREAD 2: Comandos do Administrador (Lê do Teclado)
void* tarefa_admin(void* arg) {
    char buffer_admin[100];

    while (1) {
        // Leitura BLOQUEANTE do teclado (muito mais eficiente que O_NONBLOCK)
        int bytes_admin = read(0, buffer_admin, sizeof(buffer_admin) - 1);
        
        if (bytes_admin > 0) {
            buffer_admin[bytes_admin] = '\0';
            if (buffer_admin[bytes_admin - 1] == '\n') buffer_admin[bytes_admin - 1] = '\0';
            
            // 🔒 Bloquear acesso
            pthread_mutex_lock(&trinco);// tentar nao ter unlock e lock tao afastados
            //usar o mutex para proteger\bloquear acesso ás variaveis partilhadas entre os diverços processos (threads)

            if (strcmp(buffer_admin, "utiliz") == 0) {
                printf("\n--- Clientes (%d) ---\n", num_clientes);
                for (int c = 0; c < num_clientes; c++) printf(" > %s (PID %d)\n", lista_clientes[c].username, lista_clientes[c].pid);
            }
            else if (strcmp(buffer_admin, "listar") == 0) {
                printf("\n--- Viagens Pendentes (%d) ---\n", num_viagens);
                for (int v = 0; v < num_viagens; v++) 
                    printf(" > ID %d | %s | T:%d\n", lista_viagens[v].id, lista_viagens[v].local, lista_viagens[v].hora_inicio);
            }
            else if (strcmp(buffer_admin, "frota") == 0) {
                printf("\n--- Frota (%d/%d) ---\n", num_veiculos_em_curso, limite_frota_configurado);
                for (int f = 0; f < num_veiculos_em_curso; f++) {
                    if (frota[f].hora_fim > 0) 
                        printf(" > ID:%d | Veic:%d | Cli:%d | Fim: T-%d\n", frota[f].id, frota[f].pid_veiculo, frota[f].pid_cliente, frota[f].hora_fim);
                    else 
                        printf(" > ID:%d | Veic:%d | Cli:%d | A aguardar\n", frota[f].id, frota[f].pid_veiculo, frota[f].pid_cliente);
                }
            }
            else if (strcmp(buffer_admin, "km") == 0) {
                printf("\n[Estatistica] Total KMs: %ld\n", total_kms_percorridos);
            }
            else if (strcmp(buffer_admin, "hora") == 0) {
                printf("\nTempo Atual: %d\n", tempo_simulado);
            }
            else if (strcmp(buffer_admin, "terminar") == 0) {
                for (int k = 0; k < num_veiculos_em_curso; k++) kill(frota[k].pid_veiculo, SIGUSR1);
                exit(0); // Termina todo o processo e threads
            }
            else if (strncmp(buffer_admin, "cancelar", 8) == 0 || strncmp(buffer_admin, "c ", 2) == 0) {
                int id_alvo;
                if (sscanf(buffer_admin, "%*s %d", &id_alvo) == 1) {
                    int cancelados = 0;
                    // Cancelar Pendentes
                    for (int v = 0; v < num_viagens; v++) {
                        if (id_alvo == 0 || lista_viagens[v].id == id_alvo) {
                            printf("  > Viagem Pendente ID %d cancelada.\n", lista_viagens[v].id);
                            lista_viagens[v] = lista_viagens[num_viagens - 1];
                            num_viagens--;
                            v--; 
                            cancelados++;
                        }
                    }
                    // Cancelar Em Curso
                    for (int f = 0; f < num_veiculos_em_curso; f++) {
                        if (id_alvo == 0 || frota[f].id == id_alvo) {
                            printf("  > A cancelar Veículo %d (Viagem ID %d)...\n", frota[f].pid_veiculo, frota[f].id);
                            kill(frota[f].pid_veiculo, SIGUSR1);
                            cancelados++;
                        }
                    }
                    if (cancelados == 0) printf("  > ID %d não encontrado.\n", id_alvo);
                } else {
                    printf("  > Uso: c <ID> (ou 0 para tudo)\n");
                }
            }
            else {
                printf("Comando desconhecido.\n");
            }

            // 🔓 Desbloquear acesso
            pthread_mutex_unlock(&trinco);
            
            // Forçar o prompt ou output
            fflush(stdout);
        }
    }
    return NULL;
}

// MAIN: Thread do Relógio e Gestão da Frota
int main() {
    setbuf(stdout, NULL); // Remove buffering para printfs imediatos
    
    // --- LER AMBIENTE ---
    char* var_nveiculos = getenv("NVEICULOS");
    if (var_nveiculos != NULL) limite_frota_configurado = atoi(var_nveiculos);
    if (limite_frota_configurado <= 0) limite_frota_configurado = 10;
    printf("[Controlador] Limite da frota: %d veículos.\n", limite_frota_configurado);
    
    // --- SETUP ---
    signal(SIGINT, trata_sigint);
    signal(SIGCHLD, trata_sigchld); 

    if (mkfifo(FIFO_PRINCIPAL, 0666) == -1) {
        perror("[ERRO] mkfifo falhou (controlador já existe?)");
        exit(1);
    }

    // Abre FIFO em modo bloqueante normal (as threads tratam disto)
    fd_fifo_principal = open(FIFO_PRINCIPAL, O_RDWR); 
    if (fd_fifo_principal == -1) { perror("Erro open FIFO"); unlink(FIFO_PRINCIPAL); exit(2); }

    // --- LANÇAR THREADS DE INPUT ---
    pthread_t t_cli, t_adm;
    pthread_create(&t_cli, NULL, tarefa_clientes, NULL);
    pthread_create(&t_adm, NULL, tarefa_admin, NULL);
    
    printf("Controlador iniciado. Modo THREADS.\n");

    // --- CICLO PRINCIPAL (RELÓGIO) ---
    while (1) {
        sleep(1);
        
        // 🔒 ZONA CRÍTICA
        pthread_mutex_lock(&trinco);
        
        tempo_simulado++;

        // 1. VERIFICAR VIAGENS AGENDADAS
        for (int i = 0; i < num_viagens; i++) {
            if (lista_viagens[i].hora_inicio <= tempo_simulado) {
                
                // Verifica Frota
                if (num_veiculos_em_curso >= limite_frota_configurado) {
                    if (lista_viagens[i].notificacao_atraso_enviada == 0) {
                        // Calcula próxima vaga
                        int proxima_vaga = -1;
                        for (int k = 0; k < num_veiculos_em_curso; k++) {
                            if (frota[k].hora_fim > 0) { 
                                if (proxima_vaga == -1 || frota[k].hora_fim < proxima_vaga) proxima_vaga = frota[k].hora_fim;
                            }
                        }
                        if (proxima_vaga <= tempo_simulado) proxima_vaga = tempo_simulado + 1;

                        const char* nome = encontrar_nome_por_pid(lista_viagens[i].pid_cliente);
                        printf("\n[Controlador] Viagem de %s adiada (Frota Cheia). Re-agendada: T-%d\n", nome, proxima_vaga); 
                        
                        lista_viagens[i].hora_inicio = proxima_vaga; 
                        lista_viagens[i].notificacao_atraso_enviada = 1; 
                    } else {
                         lista_viagens[i].hora_inicio = tempo_simulado + 1;
                    }
                    continue; 
                }
                
                // Lançar Veículo
                printf("\n[Controlador] A lançar viagem para %s (ID: %d)!\n", lista_viagens[i].local, lista_viagens[i].id);
                
                int fd_pipe[2];
                if (pipe(fd_pipe) == -1) { perror("Erro pipe"); continue; }

                pid_t pid_filho = fork();
                
                if (pid_filho == 0) { // FILHO
                    close(fd_pipe[0]); close(1); dup(fd_pipe[1]); close(fd_pipe[1]);    
                    char str_hora[20], str_dist[20], str_pid_cli[20];
                    sprintf(str_hora, "%d", lista_viagens[i].hora_inicio);
                    sprintf(str_dist, "%d", lista_viagens[i].distancia);
                    sprintf(str_pid_cli, "%d", lista_viagens[i].pid_cliente);
                    execlp("./veiculo", "veiculo", str_hora, lista_viagens[i].local, str_dist, str_pid_cli, NULL);
                    exit(100); 
                } 
                else if (pid_filho > 0) { // PAI
                    close(fd_pipe[1]); 
                    
                    if (num_veiculos_em_curso < MAX_FROTA_GLOBAL) {
                        frota[num_veiculos_em_curso].pid_veiculo = pid_filho;
                        frota[num_veiculos_em_curso].pid_cliente = lista_viagens[i].pid_cliente;
                        frota[num_veiculos_em_curso].fd_pipe_telemetria = fd_pipe[0];
                        frota[num_veiculos_em_curso].distancia = lista_viagens[i].distancia; 
                        frota[num_veiculos_em_curso].hora_fim = 0;
                        frota[num_veiculos_em_curso].kms_percorridos = 0;
                        frota[num_veiculos_em_curso].id = lista_viagens[i].id; // Copiar ID
                        num_veiculos_em_curso++;
                    }
                    // Tornar pipe da telemetria não bloqueante (para não travar o relógio)
                    int flags = fcntl(fd_pipe[0], F_GETFL);
                    fcntl(fd_pipe[0], F_SETFL, flags | O_NONBLOCK);
                    
                    lista_viagens[i] = lista_viagens[num_viagens - 1];
                    num_viagens--;
                    i--; 
                } else {
                    close(fd_pipe[0]); close(fd_pipe[1]);
                }
            } 
        } 
        
        // 2. LER TELEMETRIA ---
        char buffer_telemetria[256];
        for (int i = 0; i < num_veiculos_em_curso; i++) {
            int bytes = read(frota[i].fd_pipe_telemetria, buffer_telemetria, sizeof(buffer_telemetria) - 1);
            
            if (bytes > 0) {
                buffer_telemetria[bytes] = '\0'; 
                printf("\n[Telemetria PID %d]: %s", frota[i].pid_veiculo, buffer_telemetria);

                // 1. Detetar Início da Viagem
                if (frota[i].hora_fim == 0 && strstr(buffer_telemetria, "Cliente entrou!") != NULL) {
                    frota[i].hora_fim = tempo_simulado + frota[i].distancia;
                    printf("\n[Controlador] Viagem INICIADA. Fim: T-%d\n", frota[i].hora_fim);
                }

                // 2. ATUALIZAR KMS (NOVO!)
                int percentagem = 0;
                // Procura por "Viagem X% completa"
                char* ptr_percent = strstr(buffer_telemetria, "Viagem ");
                if (ptr_percent != NULL) {
                    if (sscanf(ptr_percent, "Viagem %d%%", &percentagem) == 1) {
                        // Calcula kms reais: (percentagem * distancia_total) / 100
                        frota[i].kms_percorridos = (percentagem * frota[i].distancia) / 100;
                    }
                }
                fflush(stdout);
            } 
            else if (bytes == 0) { 
                // --- O VEÍCULO TERMINOU ---
                printf("\n[Controlador] Veículo %d terminou.\n", frota[i].pid_veiculo);
                
                // MUDANÇA AQUI: Somar apenas o que foi realmente percorrido!
                total_kms_percorridos += frota[i].kms_percorridos; 
                printf("[Estatistica] +%d KMs adicionados ao total.\n", frota[i].kms_percorridos);

                close(frota[i].fd_pipe_telemetria);
                frota[i] = frota[num_veiculos_em_curso - 1];
                num_veiculos_em_curso--;
                i--; 
                printf("[Controlador] Vaga libertada.\n");
                fflush(stdout);
            }
        }

        // 🔓 ZONA CRÍTICA FIM
        pthread_mutex_unlock(&trinco);
    }
    return 0; 
}
