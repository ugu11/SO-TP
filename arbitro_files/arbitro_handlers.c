#include "arbitro_handlers.h"

void commandArbitroPlayers(Arbitro *arbitro) {
    printf("\nLista de Jogadores:\n");
    for(int j=0; j<arbitro->nClientes; j++){
        printf("\t-> %s\n", arbitro->clientes[j].jogador.nome);
    }
}

void commandArbitroGames(Arbitro *arbitro) {
    printf("\nLista de Jogos disponiveis:\n");
    for(int j=0; j<arbitro->nJogos; j++){
        printf("\t-> %s\n", arbitro->jogos[j]);
    }
}

void commandArbitroK(Arbitro *arbitro, char* adminCommand) {
    PEDIDO ptmp;
    int wordSize = strlen(adminCommand);
    char *clientName = malloc(sizeof(char) * (wordSize-1));
    int doesNameExist = 0;
    for(int j=0; j<wordSize-1; j++){
        clientName[j] = adminCommand[j+1];
    }
    
    for(int i=0; i<arbitro->nClientes; i++){
        if(strcmp(arbitro->clientes[i].jogador.nome, clientName) == TRUE){
            strcpy(ptmp.nome,arbitro->clientes[i].jogador.nome);
            ptmp.pid = arbitro->clientes[i].pid;
            commandClientQuit(arbitro, &ptmp);
            doesNameExist = 1;
            break;
        }
    }
    //Caso o nome nao exista
    if(!doesNameExist){
        printf("\nErro no comando! Digite um nome existente (knome)\n");
    }

    free(clientName);
}

void commandArbitroExit(Arbitro *arbitro) {
    PEDIDO ptmp;
    displayFinalScores(arbitro);
    for(int i = 0; i < arbitro->nClientes; i){
        strcpy(ptmp.nome,arbitro->clientes[i].jogador.nome);
        ptmp.pid = arbitro->clientes[i].pid;
        commandClientQuit(arbitro, &ptmp);
    }
}

void commandArbitroConSuspensa(Arbitro *arbitro, char* adminCommand, PEDIDO *p, int isConSuspended) {
    PEDIDO ptmp;
    int wordSize = strlen(adminCommand);
    char *clientName = malloc(sizeof(char) * (wordSize-1));
    int doesNameExist = 0;
    for(int j=0; j<wordSize-1; j++){
        clientName[j] = adminCommand[j+1];
    }

    for(int i=0; i<arbitro->nClientes; i++){
        if(strcmp(arbitro->clientes[i].jogador.nome, clientName) == TRUE){
            arbitro->clientes[i].isConnectionSuspended = isConSuspended;
            doesNameExist = 1;
            p->pid = arbitro->clientes[i].pid;
            strcpy(p->nome, arbitro->clientes[i].jogador.nome);
            if(isConSuspended == TRUE){
                printf("Comunicacao suspensa para %s\n", arbitro->clientes[i].jogador.nome);
                sendResponse(*p, "_con_suspensa_", "", arbitro->clientes[i].fifo, sizeof(*p));
            }else{
                printf("Comunicacao retomana para %s\n", arbitro->clientes[i].jogador.nome);
                sendResponse(*p, "_con_retomada_", "", arbitro->clientes[i].fifo, sizeof(*p));
            }
            
            break;
        }
    }
    //Caso o nome nao exista
    if(!doesNameExist){
        printf("\nErro no comando! Digite um nome existente (snome)\n");
    }

    free(clientName);
}

void displayFinalScores(Arbitro *arbitro) {
    PEDIDO p;
    char winnerDesc[70];

printf("\n\n###############################\n");
    printf("        Pontuação final        \n");
    printf("###############################\n");
    for(int i = 0; i < arbitro->nClientes; i++)
        printf(" [ %d ] - %s - %dpts\n", i+1, arbitro->clientes[i].jogador.nome, arbitro->clientes[i].jogador.pontuacao);
    printf("###############################\n");


    for(int i = 0; i < arbitro->nClientes; i++) {
        strcpy(p.nome, arbitro->clientes[i].jogador.nome);
        if(arbitro->winner == &arbitro->clientes[i]) {
            sprintf(winnerDesc, "\nPontuacao final: %d\n\nParabens! Venceu o campeonato com %dpts!",
                arbitro->clientes[i].jogador.pontuacao, arbitro->winner->jogador.pontuacao);
        } else {
            sprintf(winnerDesc, "\nPontuacao final: %d\n\nO vencedor do campeonato foi %s com %dpts!",
                arbitro->clientes[i].jogador.pontuacao, arbitro->winner->jogador.nome, arbitro->winner->jogador.pontuacao);
        }
        sendResponse(p, "_announce_winner_", winnerDesc, arbitro->clientes[i].fifo, sizeof(p));
    }
}

void stopGames(Arbitro *arbitro, int *gameStarted) {
    *gameStarted = FALSE;

    for(int i = 0; i < arbitro->nClientes; i++) {
        kill(arbitro->clientes[i].jogo.gamePID, SIGUSR1);

        pthread_kill(arbitro->clientes[i].jogo.readThread, SIGUSR1);
        pthread_kill(arbitro->clientes[i].jogo.writeThread, SIGUSR1);
    }
}

void clearClientes(Arbitro *arbitro) {
    PEDIDO p;

    for(int i = 0; i < arbitro->nClientes; i++) {
        strcpy(p.nome, arbitro->clientes[i].jogador.nome);
        sendResponse(p, "_quit_", "", arbitro->clientes[i].fifo, sizeof(p));
    }

    free(arbitro->clientes);
    arbitro->clientes = NULL;
    arbitro->nClientes = 0;
}