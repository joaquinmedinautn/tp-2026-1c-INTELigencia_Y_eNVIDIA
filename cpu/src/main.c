#include "cpu.h"
#include <commons/string.h>
#include <string.h>

t_config *config;
char *ip_kernel_scheduler;
int puerto_kernel_scheduler;
char *ip_kernel_memory;
int puerto_kernel_memory;
char *ip_memory_stick;
int puerto_memory_stick;
t_log_level log_level;
t_log *logger;
int socket_kernel_scheduler;
int socket_kernel_memory;
int socket_memory_stick;

int main(int argc, char *argv[])
{

    saludar("cpu");
    ObtenerConfig();

    logger = log_create("cpu.log", "CPU", true, log_level);
    
    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);
    EnviarOpCode(socket_kernel_scheduler, CPU_A_KERNEL_SCHEDULER);

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_kernel_memory, CPU_A_KERNEL_MEMORY);

    //socket_memory_stick = CrearConexion(ip_memory_stick, puerto_memory_stick);
   //EnviarOpCode(socket_memory_stick, CPU_A_MEMORY_STICK);


   while (1) {
    // Esperamos un PID del Scheduler
    int size;
    void* buffer = RecibirBuffer(&size, socket_kernel_scheduler);
    int pid = *(int*)buffer;
    free(buffer);
    
    // Arrancamos el ciclo con ese PID
    iniciar_ciclo(pid);
}

    return 0;
}

void ObtenerConfig()
{
    config = config_create("cpu.config");
    
    ip_kernel_scheduler = string_duplicate(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");

    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    //ip_memory_stick = config_get_string_value(config, "IP_MEMORY_STICK");
    //puerto_memory_stick = config_get_int_value(config, "PUERTO_MEMORY_STICK");


    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    
    config_destroy(config);
}