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
#include <pthread.h>
#include <commons/collections/queue.h>
#include <semaphore.h>
#include <stdint.h>

typedef enum
{
    CPU_A_MEMORY_STICK,
    LEER,
    ESCRIBIR
} utils_op_code_memory_stick;

typedef enum
{
    CPU_A_KERNEL_SCHEDULER,
    IO,
    PROCESO_CARGADO,
    ESPACIO_INSUFICIENTE,
    CARGAR_PROC,
    CPU_SOLICITA_PID,
    CPU_SYSCALL,
    CPU_PID_DEVUELTO,
    KERNEL_ENVIA_PID,
    KERNEL_NO_HAY_PROCESOS,
    DESCONEXION,
    IO_STDIN_READ,
    IO_STDOUT_WRITE,
    IO_SLEEP,
    IO_FINALIZADO,
    MEMORIA_CORRUPTA,
    INTERRUPCION_QUANTUM
} utils_op_code_kernel_scheduler;

typedef enum
{
    KERNEL_SCHEDULER,
    MEMORY_STICK,
    CPU_A_KERNEL_MEMORY,
    SWAP,
    PROCESO
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

typedef enum{

    CREAR_PROCESO, //preparacion
    SOLICITUD_INSTRUCCION, //fetch
    FINALIZAR_PROCESO, //libero memoria
    MENSAJE,
    LECTURA_MEMORIA,
    ESCRITURA_MEMORIA,
    SOLICITUD_ESPACIO_LIBRE,
} op_code;

typedef struct {
    uint32_t PC;
    uint32_t EAX;
    uint32_t EBX;
    uint32_t ECX;
    uint32_t EDX;
    uint32_t SI;
    uint32_t DI;
    uint8_t AX;
    uint8_t BX;
    uint8_t CX;
    uint8_t DX;
    t_list* instrucciones;
} t_contexto;


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
void EliminarPaquete(t_paquete* paquete);
void AgregarAPaquete(t_paquete* paquete, void* valor, int tamanio);
void* RecibirBuffer(int* size, int socket_cliente);
t_list* RecibirPaquete(int socket_cliente);


#endif
