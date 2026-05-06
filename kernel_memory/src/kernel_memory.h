#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <utils/utils.h>
#include <commons/string.h>


void ObtenerConfig();


void atender_cliente(void* void_socket); 
void cargar_instrucciones(uint32_t pid, char* path_relativo);
char* obtener_instruccion(uint32_t pid, uint32_t pc);
void eliminar_contexto(void* contexto_void);


extern t_log* logger; 
extern t_dictionary* procesos;
extern pthread_mutex_t mutex_procesos;
extern int instruction_delay; 
extern char* scripts_path; 

#endif