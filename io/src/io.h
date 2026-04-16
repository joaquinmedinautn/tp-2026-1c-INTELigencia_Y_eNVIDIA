#ifndef IO_H
#define IO_H 

#include <utils/utils.h>

extern t_log* logger;

void ObtenerConfig(char* path, char** ip_scheduler, int* puerto_scheduler);

#endif