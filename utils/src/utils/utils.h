#ifndef UTILS_HELLO_H_
#define UTILS_HELLO_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <commons/log.h>
#include <netdb.h>
#include <unistd.h>
#include <commons/config.h>

typedef enum
{
    CPU_A_KERNEL_SCHEDULER,
    IO
} utils_op_code_kernel_scheduler;

typedef enum
{
    KERNEL_SCHEDULER,
    MEMORY_STICK,
    CPU_A_KERNEL_MEMORY,
    SWAP
} utils_op_code_kernel_memory;

typedef struct
{
	int size;
	void* stream;
} t_buffer;

typedef struct
{
    int codigo_operacion;
	t_buffer* buffer;
} t_paquete;


void saludar(char* quien);
int IniciarServidor(int puerto);
int EsperarConexion(int socket_servidor);
int CrearConexion(char *ip, int puerto);
void CrearBuffer(t_paquete* paquete);
t_paquete* CrearPaquete(int OP);
void EnviarPaquete(t_paquete *paquete, int socket_conexion);
void *SerializarPaquete(t_paquete *paquete, int bytes);
void EnviarOpCode(int socket_conexion, int OP);
int RecibirOperacion(int socket_cliente);


#endif
