#include "kernel_scheduler.h"

int socket_servidor;
int puerto_escucha;
t_config *config;
int socket_cpu;
int socket_io;
int socket_kernel_memory;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log *logger;
t_log_level log_level;

void ObtenerConfig()
{
    config = config_create("kernel_scheduler.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
}


int main(int argc, char *argv[])
{

    saludar("kernel_scheduler");
    ObtenerConfig();

    logger = log_create("kernel_scheduler.log", "KERNEL_SCHEDULER", true, log_level);
    socket_servidor = IniciarServidor(puerto_escucha);

    socket_io = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de io");
    
    socket_cpu = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de cpu");

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);

    while (1)
    {
        sleep(10);
    }

    return 0;
}
