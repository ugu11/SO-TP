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
#include <dirent.h>
#include "../general.h"
#include "client_handlers.h"
#include "arbitro_handlers.h"
#include "request_functions.h"
#include "cli_games_handlers.h"

/* VARIAVEIS GLOBAIS*/
int readyToStart = 0;
int gameStarted = FALSE;
int serverFd;
pid_t childPid;
Arbitro arbitro;            //Arbitro como variavel global
pthread_t waitThread, clientMessagesThread;

void *sorteioJogos(void *arg);
void* iniciaEspera(void* arg);

void despertar(int sinal){
    stopGames(&arbitro, &gameStarted);
}

void sigint_handler(int s) {
    commandArbitroExit(&arbitro);

    pthread_kill(clientMessagesThread, SIGUSR1);

    free(arbitro.clientes);
    close(serverFd);
    unlink(FIFO_SRV);
    exit(0);
}
/*
    Read environment variables GAMEDIR and MAXPLAYER
*/
void getEnvironmentVariables() {
    char *maxplayerBuff;

    if((arbitro.GAMEDIR = getenv("GAMEDIR")) == NULL) {
        printf("GAMEDIR NULL\n");
    }else {
        printf("%s\n", arbitro.GAMEDIR);
    }

    if((maxplayerBuff = getenv("MAXPLAYER")) == NULL) {
        printf("MAXPLAYERS NULL\n");
    } else {
        arbitro.MAXPLAYERS = atoi(maxplayerBuff);
        printf("%d\n", arbitro.MAXPLAYERS);
    }
}

/*
    Read cli arguments
    ./arbitro.o -d xxx -t xxx
    -d => Championship duration
    -t => Waiting time
*/
void getArgs(int argc, char *argv[]) {
    int opt;
    char *subopts, *value;

    // Make them negative for later validation
    arbitro.DURACAO_CAMPEONATO = -1;
    arbitro.TEMPO_ESPERA = -1;

    while((opt = getopt(argc, argv, "d:t:")) != -1) {  
        switch(opt) {
            case 'd':
                printf("%s\n", optarg);
                arbitro.DURACAO_CAMPEONATO = atoi(optarg);
                break;
            case 't':
                printf("%s\n", optarg);
                arbitro.TEMPO_ESPERA = atoi(optarg);
                break;
            default:
                printf("Invalid argument: %s\n", opt);
        }
    }

    // Values validation. If they're <= 0, then they're invalid and the program ends
    if(!arbitro.DURACAO_CAMPEONATO || arbitro.DURACAO_CAMPEONATO <= 0) {
        printf("[ERRO] - Duracao do campeonato em falta/invalida. Utilize o argumento '-d' para passar um valor válido\n");
        exit(1);
    }

    if(!arbitro.TEMPO_ESPERA || arbitro.TEMPO_ESPERA <= 0) {
        printf("[ERRO] - Tempo de espera em falta/invalida. Utilize o argumento '-t' para passar um valor válido\n");
        exit(1);
    }
}

int countGames(){
    DIR *directory = opendir(arbitro.GAMEDIR);
    struct dirent *entry;
    int count=0;

    while((entry = readdir(directory)) != NULL){
        if(entry->d_name[0] == 'g' && entry->d_name[1] == '_'){
            arbitro.jogos[count] = entry->d_name;
            count++;
        }
    }
    return count;
}

/*
    Get env variables, cli args and initialize arbitro.
*/
void initArbitro(int argc, char* argv[]) {
    getEnvironmentVariables(arbitro);
    getArgs(argc, argv);

    arbitro.nClientes = 0;
    arbitro.nJogos = 0;
    arbitro.clientes = NULL;
    arbitro.nJogos = countGames();
}

void finishGame() {
    for(int i = 0; i < arbitro.nClientes; i++) {
        pthread_join(arbitro.clientes[i].jogo.gameThread, NULL);

        if(arbitro.winner == NULL ||
        arbitro.clientes[i].jogador.pontuacao > arbitro.winner->jogador.pontuacao) {
            arbitro.winner = &arbitro.clientes[i];
        }
    }

    putchar('\n');

    displayFinalScores(&arbitro);
    clearClientes(&arbitro);
    gameStarted = FALSE;
    pthread_create(&waitThread, NULL, &iniciaEspera, &arbitro);

    return;
}

/*
    Handle connection request: #_connect_ command
*/
void handleConnectRequest(PEDIDO p, char *fifo, int n) {
    if(gameStarted == TRUE) {
        printf("\n[WARNING] Coneccao do jogador %s recusada. O campeonato ja comecou.\n", p.nome);
        sendResponse(p, "_connection_failed_", "_game_started_", getClienteByName(&arbitro, p.nome)->fifo, n);
        return;
    }

    switch(add_cliente(&arbitro, &p)) {
        case TRUE:
            sendResponse(p, "_connection_accept_", "", getClienteByName(&arbitro, p.nome)->fifo, n);
            break;
        case MAX_PLAYER_ERR:
            sendResponse(p, "_connection_failed_", "_max_players_", getClienteByName(&arbitro, p.nome)->fifo, n);
            break;
        case FALSE:
        default:
            sendResponse(p, "_connection_failed_", "", getClienteByName(&arbitro, p.nome)->fifo, n);
    }
    printClientes(&arbitro);
}


/*
    Handle command for the arbitro internal usage
*/
void handleClientCommandsForArbitro(PEDIDO p, int n) {
    // Check for #_connect_ command and if client isnt already connected
    // When a client make a connection request
    // sendResponse(p, "_test_command_", "", fifo, sizeof(p));
    if(strcmp(p.comando, "#_connect_") == TRUE && validate_client_connected(&arbitro,p.pid) == TRUE) { 
        handleConnectRequest(p, getClienteByName(&arbitro, p.nome)->fifo, n);
    } else if(strcmp(p.comando, "#quit") == TRUE) {
        commandClientQuit(&arbitro, &p);
        if(arbitro.nClientes < 2)
            stopGames(&arbitro, &gameStarted);
    } else if(strcmp(p.comando, "#mygame") == TRUE)
        commandClientMyGame(&arbitro, &p, p.nome, n);
    else sendResponse(p, "_error_", "_invalid_command_", getClienteByName(&arbitro, p.nome)->fifo, n);
}

int handleArbitroCommands() {
    PEDIDO p;
    char adminCommand[40];
    scanf("%s", adminCommand);

    if(strcmp(adminCommand, "players") == TRUE){
        commandArbitroPlayers(&arbitro);
    }else if(strcmp(adminCommand, "games") == TRUE){
        commandArbitroGames(&arbitro);
    } else if(strcmp(adminCommand, "end") == TRUE){
        stopGames(&arbitro, &gameStarted);
    } else if(adminCommand[0] == 'k'){
        commandArbitroK(&arbitro, adminCommand);
    } else if(adminCommand[0] == 's'){
        commandArbitroConSuspensa(&arbitro, adminCommand, &p, TRUE);
    } else if(adminCommand[0] == 'r'){
        commandArbitroConSuspensa(&arbitro, adminCommand, &p, FALSE);
    } else if(strcmp(adminCommand, "exit") == TRUE){
        commandArbitroExit(&arbitro);
        return 1;
    }

    return 0;
}

void handleClientsMessages(int fd) {
    PEDIDO p;
    Cliente *client;
    int n;
    n = read(fd, &p, sizeof(PEDIDO));
    if(p.comando[0] == '#') { // Command for arbitro
        handleClientCommandsForArbitro(p, n);
        printf("\n[ADMIN]: ");
    } else { // Command for game...
        client = getClienteByName(&arbitro, p.nome);

        if(client->isConnectionSuspended == TRUE) {
            printf("\n[INFO] Comunicação de %s suspensa\n", client->jogador.nome);
            sendResponse(p, "_con_suspensa_", "[WARNING] Comunicacao jogador-jogo foi suspensa.", client->fifo, n);
        } else {
            if(strcmp(client->jogo.nome, "") != TRUE) {
                strcpy(client->jogo.gameCommand, p.comando);
            }
        }
    }
}

void terminaThread(int sig){
    pthread_exit(NULL);
}

void *runClientMessagesThread(void *arg) {
    signal(SIGUSR1, terminaThread);
    THREAD_CLI_MSG *tcm = (THREAD_CLI_MSG *)arg;
    int fd = tcm->fd;
    while(1) {
        handleClientsMessages(fd);
    }
    pthread_exit(NULL);
}

void* gameThread(void* arg){
    Cliente *cliente = (Cliente *) arg;
    initJogo(cliente, &gameStarted, &arbitro);
    pthread_exit(NULL);
}

void *sorteioJogos(void *arg) {
    signal(SIGALRM, despertar);
    PEDIDO p;
    if(gameStarted == FALSE && arbitro.nClientes >= 2) {
        printf("\n[ SORTEANDO JOGOS ] -- %d\n", arbitro.nClientes);
        gameStarted = TRUE;
        int num;
        
        for(int i = 0; i < arbitro.nClientes; i++) {
            printf("\nStarting game for jogador %s\n", arbitro.clientes[i].jogador.nome);
            num = intUniformRnd(1,arbitro.nJogos);
            strcpy(arbitro.clientes[i].jogo.nome , arbitro.jogos[num-1]) ;
            sendResponse(p,"_game_sorted_",arbitro.clientes[i].jogo.nome,arbitro.clientes[i].fifo, sizeof(p));
            pthread_create(&arbitro.clientes[i].jogo.gameThread, NULL, &gameThread ,&arbitro.clientes[i]);
        }

        alarm(arbitro.DURACAO_CAMPEONATO);
        finishGame();

        pthread_exit(NULL);      
    }
}

void espera(int sinal){
    setbuf(stdout, NULL);
    pthread_t sorteioJogosThread;
    printf("\nVai se dar inicio aos jogos!");
    pthread_create(&sorteioJogosThread,  NULL, &sorteioJogos, NULL);
}

void* iniciaEspera(void* arg){
    Arbitro *arbtr = (Arbitro *)arg;
    signal(SIGALRM, espera);
    printf("\nAguarde por outros jogadores...");
    while(1){
        if(arbtr->nClientes >= 2){
            alarm(arbtr->TEMPO_ESPERA);
            sleep(arbtr->TEMPO_ESPERA+1);
            break;
        }
    }  
    pthread_exit(NULL);      
}


int main(int argc, char *argv[]){
    PEDIDO p;
    RESPONSE resp;
    int n, res;
    fd_set fds;
    THREAD_CLI_MSG thread_cli_msg;
    initRandom();
    initArbitro(argc, argv);
    signal(SIGUSR1, threadSignalHandler);
    signal(SIGINT, sigint_handler);

    if(access(FIFO_SRV, F_OK)) {
        mkfifo(FIFO_SRV, 0600);
        printf("FIFO Criado...\n");
    }

    serverFd = open(FIFO_SRV, O_RDWR);
    printf("FIFO aberto: '%s'\n", FIFO_SRV);
    printClientes(&arbitro);

    thread_cli_msg.fd = serverFd;
    thread_cli_msg.stop = 0;

    pthread_create(&clientMessagesThread, NULL, &runClientMessagesThread, &thread_cli_msg);
    pthread_create(&waitThread, NULL, &iniciaEspera, &arbitro);
    
    do {
        printf("\n[ADMIN]: ");
        fflush(stdout);
        
        if(handleArbitroCommands() == 1) break;
    } while(1);

    thread_cli_msg.stop = 1;

    pthread_kill(clientMessagesThread, SIGUSR1);

    free(arbitro.clientes);
    close(serverFd);
    unlink(FIFO_SRV);
    exit(0);
}
