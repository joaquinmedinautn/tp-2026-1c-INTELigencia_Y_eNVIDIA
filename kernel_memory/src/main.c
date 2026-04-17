#include "kernel_memory.h"

t_config *config;
t_log_level log_level;
t_log *logger;
int socket_servidor;
int puerto_escucha;
int socket_kernel_scheduler;
int socket_memory_stick;
int socket_cpu;
int socket_swap;

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);
    socket_servidor = IniciarServidor(puerto_escucha);

    socket_kernel_scheduler = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de kernel scheduler");

    socket_memory_stick = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de memory stick");

    socket_cpu = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de cpu");

    socket_swap = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de swap");

    while (1)
    {
        sleep(10);
    }

    return 0;
}

void ObtenerConfig()
{
    config = config_create("kernel_memory.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
}