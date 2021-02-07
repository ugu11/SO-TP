CC=gcc

all: client jogos arbitro

jogos: utils.o
	@${CC} jogos_files/g_1.c -o GameDir/g_1.o
	@${CC} jogos_files/g_2.c utils.o -o GameDir/g_2.o
	@echo Ficheiros dos jogos Compilados!

client:
	@${CC} client_files/client.c -o client.o -lpthread
	@echo Ficheiros dos Clientes Compilados!

arbitro:
	@${CC} arbitro_files/*.c utils.o -o arbitro.o -lpthread
	@echo Ficheiros do arbitro Compilados!

utils.o: utils.c
	@${CC} utils.c -c

clean:
	@rm *.o
	@rm GameDir/*.o
	@find . -type p -delete
	@echo Ficheiros limpos! 
