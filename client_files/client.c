#include <stdio.h>
#include <sys/stat.h> 
#include <sys/wait.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include "../utils.h"
#include "../general.h"

// Global vars to be accessible to the sigint_handler()
char fifo[40];
char playerName[40];
int isConnectionSuspended = FALSE;
int fd;

void quitGame() {
    int n;
    PEDIDO p;

    // Send disconnect message to arbitro
    if(access(fifo, F_OK) == 0) {
        // fdSrv = open(FIFO_SRV, O_WRONLY);
        strcpy(p.nome, playerName); 
        strcpy(p.comando, "#quit"); 
        n = write(fd, &p, sizeof(PEDIDO));
    }

    putchar('\n');
}

void sigint_handler(int s) {
    quitGame();
    unlink(fifo);
    close(fd);
    exit(0);
}

void sigusr2_handler(int s) {
	printf("\n%s saiu do campeonato!\n", playerName);
	sigint_handler(s);
}

void sigusr_handler(int s) {
	printf("\n%s saiu do campeonato!\n", playerName);
	sigint_handler(s);
}

int processResponse(RESPONSE resp, char *fifo) {
    if(strcmp(resp.code, "_connection_failed_") == TRUE) {
        if(strcmp(resp.desc, "_max_players_") == TRUE)
            printf("[ERRO] O numero maximo de jogadores foi atingido!\n");
        if(strcmp(resp.desc, "_game_started_") == TRUE)
            printf("[ARBITRO] Coneccao recusada. O campeonato ja comecou.\n");
        else
            printf("[ERRO] Erro ao conectar ao arbitro!\n");

        putchar('\n');
        unlink(fifo);
        exit(0);
    } else if(strcmp(resp.code, "_quit_") == TRUE) {
	    printf("\n%s saiu do campeonato!\n", playerName);
        putchar('\n');
        unlink(fifo);
        exit(0);
    } else if(strcmp(resp.code, "_success_arbitro_") == TRUE)
        printf("[ARBITRO]: %s\n", resp.desc);
    else if(strcmp(resp.code, "_success_game_") == TRUE)
        printf("[GAME]: %s\n", resp.desc);
    else if(strcmp(resp.code, "_con_suspensa_") == TRUE)
        printf("[ARBITRO] Comunicacao jogador-jogo foi suspensa!\n");
    else if(strcmp(resp.code, "_con_retomada_") == TRUE)
        printf("[ARBITRO] Comunicacao jogador-jogo foi retomada!\n");
    else if(strcmp(resp.code, "_announce_winner_") == TRUE) {
        printf(resp.desc);
        return 1;
    } else if(strcmp(resp.code, "_game_output_") == TRUE)
        printf(resp.desc);
    else if(strcmp(resp.code, "_final_score_") == TRUE)
        printf("\nO campeonato chegou ao fim!\nPontuacao final: %s\n\n", resp.desc);
    else if(strcmp(resp.code, "_error_") == TRUE)
        if(strcmp(resp.desc, "_no_game_assigned_") == TRUE)
            printf("[ERROR/ARBITRO]: Nao existe nenhum jogo associado a este cliente\n");
        else if(strcmp(resp.desc, "_invalid_command_") == TRUE)
            printf("[ERROR/ARBITRO]: Comando invalido\n");
        else
            printf("[ERROR/ARBITRO]: Ocorreu um erro\n");

    return 0;
}

/*
    Get response from arbitro
*/
int getResponse(char *fifo) {
    RESPONSE resp;
    int fdr = open(fifo, O_RDONLY);
    int n = read(fdr, &resp, sizeof(RESPONSE));
    close(fdr);
    return processResponse(resp, fifo);
}

/*
    Make connection request to arbitro and process it.
*/
void connect_to_arbitro(PEDIDO p, int *fd) {
    int n;
    
    mkfifo(fifo, 0600);
    *fd = open(FIFO_SRV, O_WRONLY);
    strcpy(p.comando, "#_connect_");
    p.pid = getpid();
    n = write(*fd, &p, sizeof(PEDIDO));
    getResponse(fifo); 
}


void *readFromStdin(void *arg) {
    int *cancel = (int *)arg;
    int n;
    PEDIDO p;
    strcpy(p.nome, playerName);

    while(*cancel == 0) {
        printf("Comando => ");
        fflush(stdout);
        
        scanf("%s", p.comando);
        p.pid = getpid();
        n = write(fd, &p, sizeof(PEDIDO));
    }
}

int askForReconect() {
    char r;

    do {
        printf("\nPretende voltar a jogar? (s/n): ");
        scanf("%c", &r);
    } while(r != 's' && r != 'n');

    return r == 's';
}

void runCliente(){
    pthread_t readPipeThread, readStdinThread;
    int n, quit = 0;
    PEDIDO p;
    RESPONSE resp;
    printf("\n\nPID <%d>\n\n", getpid());

    signal(SIGINT, sigint_handler); // Ignore ^C
    signal(SIGUSR2, sigusr_handler);
    signal(SIGUSR2, sigusr2_handler);
    setbuf(stdout, NULL);

    if(access(FIFO_SRV, F_OK) == -1) {
        fprintf(stderr, "[ERR] O servidor não está a correr\n");
        exit(1);
    }

    while(1) {
        printf("Nome de jogador: ");
        scanf("%s", playerName);

        sprintf(fifo, FIFO_CLI, playerName);

        if(access(fifo, F_OK) == 0) {
            fprintf(stderr, "[ERR] Nome de jogador já existe\n");
        } else break;
    }

    strcpy(p.nome, playerName);
    connect_to_arbitro(p, &fd);


    pthread_create(&readStdinThread, NULL, &readFromStdin, &quit);

    do {
        if(getResponse(fifo) == 1) {
            pthread_cancel(readStdinThread);
            break;
        }
        printf("\nComando => ");
    } while(1);

    quitGame();
    quit = 1;
    
    pthread_join(readStdinThread, NULL);
    unlink(fifo);
    close(fd);
}

void main() {
    do {
        runCliente();
    } while(askForReconect() == 1);

    exit(0);
}