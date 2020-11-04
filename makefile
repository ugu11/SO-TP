CC=gcc

all:
	make set_env
	make jogos
	make client
	make arbitro

jogos:
	${CC} -c jogos_files/g_*.c

client:
	${CC} client_files/client.c -o client.o

arbitro:
	${CC} arbitro_files/arbitro.c -o arbitro.o

# TODO: ADD TEMP FILE CLEAN
clean:
	rm *.o

set_env:
	bash ./set_env.sh
