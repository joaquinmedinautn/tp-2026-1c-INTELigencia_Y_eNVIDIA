#ifndef CICLO_H
#define CICLO_H

#include <utils/utils.h>

typedef struct {
    char* nombre;
    char* params[4];
    int cant_params;
} t_instruccion;

// Contexto del proceso que está ejecutando la CPU ahora mismo
extern t_contexto contexto;
extern int pid_actual;
extern int socket_kernel_memory;
extern int socket_kernel_scheduler;
extern t_log* logger;
extern int hay_interrupcion;
extern pthread_mutex_t mutex_interrupcion;

uint32_t obtener_valor_registro(char* nombre);
void asignar_valor_registro(char* nombre, uint32_t valor);

// Etapas del ciclo
char* fetch();
t_instruccion* decode(char* instruccion);
int execute(t_instruccion* inst);
int check_interrupt();

// El loop principal
void iniciar_ciclo(int pid);

#endif