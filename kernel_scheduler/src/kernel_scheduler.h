#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <utils/utils.h>

typedef enum {
    NEW,
    READY,
    EXEC,
    BLOCK,
    SUSP_BLOCK,
    SUSP_READY,
    EXIT
} t_estado;

typedef struct
{
    int pid;
    int tamanio;
    char* codigo;
    t_estado estado;
    int prioridad;
    int dir_logica_io;
    int tamanio_io;
} proceso;

typedef struct
{
    int socket;
    int id_cpu;
    bool ocupado;  // Si está ejecutando un proceso
    int pid_actual;
} t_cpu_conectada;

typedef struct
{
    int socket;
    char tipo_io[20];  // "STDIN", "STDOUT", "SLEEP"
} t_io_conectado;

typedef struct {
    t_cpu_conectada* cpu;
    int pid;
} t_args_quantum;

typedef struct {
    char nombre[50];
    int pid_dueño;      // -1 si está libre
    t_queue* cola_espera;  // procesos esperando
    pthread_mutex_t mutex_syscall;
} t_mutex_proceso;

void IniciarSemaforos();
void ObtenerConfig();
void IniciarServidorMultihilo();
void *AceptarConexiones(void* args);
void *ManejarCliente(void* args);
void RegistrarCPU(int socket_cliente);
void RegistrarIO(int socket_cliente);
void *AtenderCPU(void* args);
void *AtenderIO(void* args);
void *PlanificadorLargoPlazo(void* args);
void *PlanificadorCortoPlazo(void* args);
void EnviarPIDaCPU(t_cpu_conectada* cpu);
void ProcesarSyscall(t_cpu_conectada* cpu);
void EnviarAIO(char* tipo_buscado, int operacion, int pid, void* params);
void IniciarProceso(int tamanio, char *codigo);
char* NombreEstado(t_estado estado);
void CambiarEstado(proceso* p, t_estado nuevo_estado);
proceso* BuscarProceso(int pid);
void EjecutarBSOD();
//void *EscucharKernelMemory(void* args);
void *HiloQuantum(void *args);
t_mutex_proceso *BuscarMutex(char *nombre);
void Cleanup();

#endif