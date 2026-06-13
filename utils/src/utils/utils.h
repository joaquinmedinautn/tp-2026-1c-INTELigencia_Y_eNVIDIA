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
#include <commons/bitarray.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/list.h>

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

typedef struct{
    uint32_t pid;
    uint32_t id_segmento;
    uint32_t base;
    uint32_t tamanio;
    bool presente;
    t_list* bloques_swap;
} t_segmento;

typedef struct{
    uint32_t base;
    uint32_t tamanio;
} t_hueco_libre;

typedef struct{
    int socket;
    uint32_t limite;
    uint32_t tamanio; 
    uint32_t base_global;
} t_memory_stick;


typedef enum {
    // CONEXION
    KERNEL_SCHEDULER,
    CPU_A_KERNEL_MEMORY,
    CPU_A_KERNEL_SCHEDULER,
    IO,
    SWAP,
    CPU_A_MEMORY_STICK,

    // Gestión de Procesos
    CREAR_PROCESO,
    PROCESO_CARGADO,
    SOLICITUD_INSTRUCCION,
    FINALIZAR_PROCESO,
    SUSPENDER_PROCESO,
    
    // Memoria (
    LECTURA_MEMORIA,
    ESCRITURA_MEMORIA,
    ASIGNAR_MEMORIA,
    SOLICITUD_ESPACIO_LIBRE,
    ESPACIO_INSUFICIENTE,
    MEMORIA_CORRUPTA,
    LEER,
    ESCRIBIR,
    DESUSPENDER_PROCESO,
    COMPACTAR_MEMORIA,
    MEMORIA_INSUFICIENTE,
    SOLICITUD_CONTEXTO,
    

    // CPU y Planificacion
    CPU_SOLICITA_PID,
    CPU_SYSCALL,
    CPU_PID_DEVUELTO,
    INTERRUPCION_QUANTUM,
    INTERRUPCION_DESALOJO_COLA,
    PROCESO,
    CARGAR_PROC,
    KERNEL_SCHEDULER_EVENTOS,
    NECESITA_COMPACTACION,
    CONSULTAR_ESPACIO,
    CREAR_SEGMENTO,
    ELIMINAR_SEGMENTO,
    NUEVO_MEMORY_STICK,
    FIN_COMPACTACION,
    SOLICITUD_COMPACTACION,    // KM → KS: desalojen las CPUs
    CPUS_DESALOJADAS,          // KS → KM: ya están desalojadas, compactá
    INTERRUPCION_COMPACTACION,  // KS → CPU: devolvé el proceso

    // Io
    IO_STDIN_READ,
    IO_STDOUT_WRITE,
    IO_SLEEP,
    IO_FINALIZADO,

    // otros
    MENSAJE,
    DESCONEXION,
    SEGMENTATION_FAULT,
    //swap
    ESCRITURA_BLOQUE,
    LECTURA_BLOQUE
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
