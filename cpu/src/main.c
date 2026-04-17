#include "cpu.h"

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

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);

    socket_memory_stick = CrearConexion(ip_memory_stick, puerto_memory_stick);


    while (1)
    {
        sleep(10);
    }

    return 0;
}

void ObtenerConfig()
{
    config = config_create("cpu.config");
    
    ip_kernel_scheduler = config_get_string_value(config, "IP_KERNEL_SCHEDULER");
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    ip_memory_stick = config_get_string_value(config, "IP_MEMORY_STICK");
    puerto_memory_stick = config_get_int_value(config, "PUERTO_MEMORY_STICK");

    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
}