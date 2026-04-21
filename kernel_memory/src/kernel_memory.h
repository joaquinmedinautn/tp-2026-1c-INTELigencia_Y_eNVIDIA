#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#include <utils/utils.h>

void ObtenerConfig();


t_dictionary* procesos;
pthread_mutex_t mx_procesos; 
#endif