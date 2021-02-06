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
#include "client_handlers.h"
#include "arbitro_handlers.h"
#include "request_functions.h"

pid_t childPid;
int readyToStart = 0;

typedef struct THREAD_CLI_MSG {
    Arbitro *arbitro;
    int fd;
    char fifo[40];
    int stop;
} THREAD_CLI_MSG;


void sorteioJogos(Arbitro *arbitro);

void despertar(int sinal){
	kill(childPid, SIGUSR1);
}

/*
    Read environment variables GAMEDIR and MAXPLAYER
*/
void getEnvironmentVariables(Arbitro *arbitro) {
    char *maxplayerBuff;

    if((arbitro->GAMEDIR = getenv("GAMEDIR")) == NULL) {
        printf("GAMEDIR NULLL\n");
    }else {
        printf("%s\n", arbitro->GAMEDIR);
    }

    if((maxplayerBuff = getenv("MAXPLAYER")) == NULL) {
        printf("MAXPLAYERS NULLL\n");
    } else {
        arbitro->MAXPLAYERS = atoi(maxplayerBuff);
        printf("%d\n", arbitro->MAXPLAYERS);
    }
}

/*
    Read cli arguments
    ./arbitro.o -d xxx -t xxx
    -d => Championship duration
    -t => Waiting time
*/
void getArgs(Arbitro *arbitro, int argc, char *argv[]) {
    int opt;
    char *subopts, *value;

    // Make them negative for later validation
    arbitro->DURACAO_CAMPEONATO = -1;
    arbitro->TEMPO_ESPERA = -1;

    while((opt = getopt(argc, argv, "d:t:")) != -1) {  
        switch(opt) {
            case 'd':
                printf("%s\n", optarg);
                arbitro->DURACAO_CAMPEONATO = atoi(optarg);
                break;
            case 't':
                printf("%s\n", optarg);
                arbitro->TEMPO_ESPERA = atoi(optarg);
                break;
            default:
                printf("Invalid argument: %s\n", opt);
        }
    }

    // Values validation. If they're <= 0, then they're invalid and the program ends
    if(!arbitro->DURACAO_CAMPEONATO || arbitro->DURACAO_CAMPEONATO <= 0) {
        printf("[ERRO] - Duracao do campeonato em falta/invalida. Utilize o argumento '-d' para passar um valor válido\n");
        exit(1);
    }

    if(!arbitro->TEMPO_ESPERA || arbitro->TEMPO_ESPERA <= 0) {
        printf("[ERRO] - Tempo de espera em falta/invalida. Utilize o argumento '-t' para passar um valor válido\n");
        exit(1);
    }
}

/*
    Get env variables, cli args and initialize arbitro.
*/
void initArbitro(Arbitro *arbitro, int argc, char* argv[]) {
    getEnvironmentVariables(arbitro);
    getArgs(arbitro, argc, argv);

    arbitro->nClientes = 0;
    arbitro->nJogos = 0;
    arbitro->clientes = NULL;
}
/*
    Handle connection request: #_connect_ command
*/
void handleConnectRequest(Arbitro *arbitro, PEDIDO p, char *fifo, int n) {
    switch(add_cliente(arbitro, &p)) {
        case TRUE:
            sorteioJogos(arbitro);
            sendResponse(p, "_connection_accept_", "", fifo, n);
            break;
        case MAX_PLAYER_ERR:
            sendResponse(p, "_connection_failed_", "_max_players_", fifo, n);
            break;
        case FALSE:
        default:
            sendResponse(p, "_connection_failed_", "", fifo, n);
    }
    printClientes(arbitro);
}


/*
    Handle command for the arbitro internal usage
*/
void handleClientCommandsForArbitro(Arbitro *arbitro, PEDIDO p, char *fifo, int n) {
    // Check for #_connect_ command and if client isnt already connected
    // When a client make a connection request
    // sendResponse(p, "_test_command_", "", fifo, sizeof(p));
    if(strcmp(p.comando, "#_connect_") == TRUE && validate_client_connected(arbitro, p.pid) == TRUE) { 
        handleConnectRequest(arbitro, p, fifo, n);
    } else if(strcmp(p.comando, "#quit") == TRUE)
        commandClientQuit(arbitro, &p);
    else if(strcmp(p.comando, "#mygame") == TRUE)
        commandClientMyGame(arbitro, &p, fifo, n);
    else sendResponse(p, "_error_", "_invalid_command_", fifo, n);
}

int handleArbitroCommands(Arbitro *arbitro, char *fifo) {
    PEDIDO p;
    char adminCommand[40];
    scanf("%s", adminCommand);
    // printf("=> %s\n", adminCommand);
    if(strcmp(adminCommand, "players") == TRUE){
        commandArbitroPlayers(arbitro);
    }else if(strcmp(adminCommand, "games") == TRUE){
        commandArbitroGames(arbitro);
    }else if(adminCommand[0] == 'k'){
        commandArbitroK(arbitro, adminCommand);
    }else if(adminCommand[0] == 's'){
        commandArbitroConSuspensa(arbitro, adminCommand, &p, TRUE);
        sendResponse(p, "_con_suspensa_", "", fifo, sizeof(p));
    }else if(adminCommand[0] == 'r'){
        commandArbitroConSuspensa(arbitro, adminCommand, &p, FALSE);
        sendResponse(p, "_con_retomada_", "", fifo, sizeof(p));
    } else if(strcmp(adminCommand, "exit") == TRUE){
        commandArbitroExit(arbitro);
        return 1;
    }

    return 0;
}

void *handleClientsMessages(Arbitro *arbitro, int fd, char *fifo) {
    PEDIDO p;
    Cliente *client;
    int n;
    n = read(fd, &p, sizeof(PEDIDO));

    if(p.comando[0] == '#') // Command for arbitro
        handleClientCommandsForArbitro(arbitro, p, fifo, n);
    else { // Command for game...
        client = getClienteByName(arbitro, p.nome);

        if(client->isConnectionSuspended == TRUE) {
            printf("Comunicação de %s suspensa\n", client->jogador.nome);
            sendResponse(p, "_con_suspensa_", "[WARNING] Comunicacao jogador-jogo foi suspensa.", fifo, n);
        } else {
            printf("To be processed by the game\n");
            sendResponse(p, "_success_game_", "output do jogo...", fifo, n);
        }
    }
}


void *runClientMessagesThread(void *arg) {
    THREAD_CLI_MSG *tcm = (THREAD_CLI_MSG *)arg;
    Arbitro *arbitro = tcm->arbitro;
    char *fifo = tcm->fifo;
    int fd = tcm->fd;

    while(tcm->stop == 0) {
        handleClientsMessages(arbitro, fd, fifo);
        printf("\n[ADMIN]: ");
    }
}


void initJogo(Cliente* cliente){
    int readPipe[2], writePipe[2];         //Guarda os file descriptors
    pid_t pid;      //id do nosso processo
    pthread_t thread;
    fd_set fds;
    int res;
    PEDIDO p;
    

    signal(SIGALRM, despertar);

    if (pipe(readPipe)==-1) 
    { 
        fprintf(stderr, "Pipe Failed" ); 
        return; 
    } 
    if (pipe(writePipe)==-1) 
    { 
        fprintf(stderr, "Pipe Failed" ); 
        return; 
    } 

    pid = fork();
    if(pid < 0){                //Erro no fork
        fprintf(stderr, "Fork failed!");
        return;
    }
    else if(pid == 0){           //Child process
        childPid = getpid();

        close(readPipe[0]);
        close(writePipe[1]);
        dup2(readPipe[1], 1);          //Passar o (1 -> stdout) para o pipe de escrita
        dup2(writePipe[0], 0);         //Passar o (stdin -> 0) para o pipe 
        // dup2(0, writePipe[0]);         //Passar o (stdin -> 0) para o pipe 

        execlp("./g_2.o", "./g_2.o", NULL);

    }else{          //Processo Pai
        close(readPipe[1]);
        printf("\nProcesso Pai!!\n");
    
        alarm(10);
        char readBuffer[3000];
        char writeBuffer[20];
        
        int n = 0;
        int a;
        char buffer;
        do {
            n = 0;
            strcpy(readBuffer, "");
            while(read(readPipe[0], &buffer, 1) > 0 && buffer != 36){
                // printf("%c", buffer);
                // fflush(stdout);
                readBuffer[n] = buffer;
                n++;
            }
            

            printf("\n\n%d \n", n);
            strcpy(p.nome, cliente->jogador.nome);
            sendResponse(p, "_game_output_", readBuffer, cliente->fifo, sizeof(PEDIDO));
            // } else if(res > 0 && FD_ISSET(0, &fds)) { // SEND THIS STDIN TO GAME
            //     scanf("%s", writeBuffer);
            //     puts(writeBuffer);
            //     write(writePipe[1], writeBuffer, sizeof(writeBuffer));
            // }


        } while(1);

        close(readPipe[0]);
        //close(writePipe[0]);
        
        
        int status;

        waitpid(pid, &status, 0); 		//processo pai espera que o filho termine

        if ( WIFEXITED(status) )
        {
            int exit_status = WEXITSTATUS(status);
            printf("Exit status of the child was %d\n",
                                        exit_status); 
        }
    }

    return;
}

void* gameThread(void* arg){
    Cliente *cliente = (Cliente *) arg;
    printf("====> %s\n\n", cliente->jogador.nome);
    initJogo(cliente);
}

void sorteioJogos(Arbitro *arbitro) {
    for(int i = 0; i < arbitro->nClientes; i++) {
        strcpy(arbitro->clientes[i].jogo.nome, "g_2.o");

        pthread_create(&arbitro->clientes[i].jogo.gameThread, NULL, &gameThread ,&arbitro->clientes[i]);
    }

    for(int i = 0; i < arbitro->nClientes; i++) {
        pthread_join(arbitro->clientes[i].jogo.gameThread, NULL);
    }

}

void start(){};


void iniciaEspera(Arbitro *arbitro){
    signal(SIGALRM, start);
    while(readyToStart != 1){
        
    }
    printf("Vai se dar inicio os jogos!");
    //Mandar para a outra thread qu einicia os jogos
}


int main(int argc, char *argv[]){
    pthread_t clientMessagesThread, arbitroCommandsThread, gameT;
    Arbitro arbitro;
    PEDIDO p;
    RESPONSE resp;
    int fd, n, res;
    char fifo[40];
    fd_set fds;
    THREAD_CLI_MSG thread_cli_msg;

    initArbitro(&arbitro, argc, argv);
    printf("ARBITRO\n");

    if(access(FIFO_SRV, F_OK)) {
        mkfifo(FIFO_SRV, 0600);
        printf("FIFO Criado...\n");
    }

    fd = open(FIFO_SRV, O_RDWR);
    printf("FIFO aberto: '%s'\n", FIFO_SRV);
    printClientes(&arbitro);

    thread_cli_msg.arbitro = &arbitro;
    thread_cli_msg.fd = fd;
    thread_cli_msg.stop = 0;
    strcpy(thread_cli_msg.fifo, fifo);

    printf("==> %s\n", fifo);

    pthread_create(&clientMessagesThread, NULL, &runClientMessagesThread, &thread_cli_msg);
    // pthread_create(&gameT, NULL, &gameThread, NULL);

    do {
        // printf("\n[ADMIN]: ");
        // fflush(stdout);
        
        // if(handleArbitroCommands(&arbitro, fifo) == 1) break;
    } while(1);

    thread_cli_msg.stop = 1;

    pthread_join(clientMessagesThread, NULL);
    // pthread_join(gameT, NULL);

    close(fd);
    unlink(FIFO_SRV);
    exit(0);
}
