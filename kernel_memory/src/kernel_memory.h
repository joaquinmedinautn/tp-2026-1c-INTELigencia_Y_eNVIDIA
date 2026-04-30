#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <utils/utils.h>

void ObtenerConfig();

typedef struct{
    t_list* intrucciones;

    uint32_t PC;
    uint8_t AX, BX, CX, DX;
    uint32_t EAX, EBX, ECX, EDX;
    uint32_t SI, DI;
} t_contexto;

void atender_cliente(void* void_socket); 
void cargar_instrucciones(uint32_t pid, char* path_relativo);
char* obtener_instruccion(uint32_t pid, uint32_t pc);
void eliminar_contexto(void* contexto_void);


extern t_log* logger; 
extern t_dictionary* procesos;
extern pthread_mutex_t mx_procesos;
extern int instruction_delay; 
extern char* scripts_path; 

#endif