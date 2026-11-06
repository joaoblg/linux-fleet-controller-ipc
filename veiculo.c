#include "common.h" // Usar o common.h
#include <time.h>    // Para a telemetria

// Handler para o sinal de cancelamento
void trata_sigusr1(int s) {
    printf("\n[Veiculo] Recebi ordem de cancelamento (SIGUSR1). A terminar...\n");
    // Opcional: avisar o cliente que a viagem foi cancelada
    exit(0); // Termina o processo
}

int main(int argc, char *argv[]) {
    // Garante que todos os printf() são enviados IMEDIATAMENTE 
    // para o pipe do controlador. (Conforme Ficha 3, Ex 3)
    setbuf(stdout, NULL);
    
    char nome_fifo_veiculo[100];
    char nome_fifo_cliente[100];
    int fd_veiculo, fd_cliente;
    MensagemSistema msg_cheguei;
    MensagemVeiculo cmd_recebido;
    signal(SIGUSR1, trata_sigusr1);

    // 1. Validar Argumentos
    if (argc != 5) {
        // ATENÇÃO: Este printf() agora vai para o PIPE, não para o ecrã!
        printf("[Veiculo ERRO] Fui lançado com o número errado de argumentos (%d).\n", argc);
        return 1;
    }
    
    // O printf() força a saída dos dados para o pipe
    // (O stdout é 'line buffered' por defeito quando ligado a um pipe)
    printf("[Veiculo PID: %d] Fui lançado!\n", getpid());

    // 2. Extrair dados
    int distancia = atoi(argv[3]);
    pid_t pid_cliente = atoi(argv[4]);

    // 3. Criar o MEU FIFO (para o cliente me responder)
    sprintf(nome_fifo_veiculo, VEICULO_FIFO_TEMPLATE, getpid());
    if (mkfifo(nome_fifo_veiculo, 0666) == -1) {
        printf("[Veiculo ERRO] mkfifo() do veiculo falhou.\n");
        return 2;
    }

    // 4. Abrir o FIFO DO CLIENTE (para eu lhe escrever)
    sprintf(nome_fifo_cliente, CLIENT_FIFO_TEMPLATE, pid_cliente);
    fd_cliente = open(nome_fifo_cliente, O_WRONLY);
    if (fd_cliente == -1) {
        printf("[Veiculo ERRO] Não consegui abrir o FIFO do cliente %s.\n", nome_fifo_cliente);
        unlink(nome_fifo_veiculo); // Limpar o meu FIFO
        return 3;
    }

    // 5. Enviar mensagem "Cheguei!" ao cliente
    msg_cheguei.tipo_msg = MSG_VEICULO_CHEGOU;
    msg_cheguei.pid_veiculo = getpid(); // Envio o meu PID
    write(fd_cliente, &msg_cheguei, sizeof(MensagemSistema));
    close(fd_cliente); // Já não preciso de escrever mais ao cliente

    // 6. Abrir o MEU FIFO para LER (e esperar pelo comando 'entrar')
    fd_veiculo = open(nome_fifo_veiculo, O_RDONLY);
    if (fd_veiculo == -1) {
        printf("[Veiculo ERRO] Não consegui abrir o meu FIFO para ler.\n");
        unlink(nome_fifo_veiculo);
        return 4;
    }

    printf("[Veiculo] À espera do comando 'entrar' do cliente...\n");
    
    int bytes_lidos = read(fd_veiculo, &cmd_recebido, sizeof(MensagemVeiculo));
    
    if (bytes_lidos == sizeof(MensagemVeiculo) && cmd_recebido.tipo_cmd == CMD_ENTRAR) {
        printf("[Veiculo] Cliente entrou! Destino: %s. A iniciar telemetria.\n", argv[2]);

        // --- 7. SIMULAR VIAGEM (COM A DISTÂNCIA CORRETA) ---
        
        int ultimo_reporte = 0;
        printf("[Telemetria] Viagem 0%% completa.\n");

        // O loop agora corre 'distancia' vezes (1 segundo por km)
        for (int t = 1; t <= distancia; t++) {
            sleep(1); // Simula 1km = 1 unidade de tempo
            
            // Calcular a percentagem atual
            int percentagem = (int)(((float)t / distancia) * 100);

            // Reportar a cada 10% (mas não reportar o mesmo valor duas vezes)
            if (percentagem >= ultimo_reporte + 10 && percentagem < 100) {
                // Arredonda para baixo para o múltiplo de 10 mais próximo
                int reporte_actual = (percentagem / 10) * 10;
                printf("[Telemetria] Viagem %d%% completa.\n", reporte_actual);
                ultimo_reporte = reporte_actual;
            }
        }
        
        // Garantir que os 100% são sempre reportados no fim
        printf("[Telemetria] Viagem 100%% completa.\n");
    } else {
        printf("[Veiculo ERRO] Cliente não enviou comando 'entrar' válido.\n");
    }

    // 8. Fim da Viagem
    close(fd_veiculo);
    unlink(nome_fifo_veiculo); // Limpar o meu FIFO
    printf("[Veiculo PID: %d] Viagem terminada. A desligar.\n", getpid());
    
    return 0;
}