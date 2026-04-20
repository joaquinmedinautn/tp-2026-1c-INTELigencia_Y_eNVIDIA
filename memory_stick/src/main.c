#include "memory_stick.h"

int socket_servidor;
int puerto_escucha;
t_config *config;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log *logger;
t_log_level log_level;
int socket_cpu;
int socket_kernel_memory;

void ObtenerConfig()
{
    config = config_create("memory_stick.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    config_destroy(config);
}

int main(int argc, char* argv[]) {
    
    saludar("memory_stick");

    ObtenerConfig();

    logger = log_create("memory_stick.log", "MEMORY_STICK", true, log_level);

    socket_servidor = IniciarServidor(puerto_escucha);

    socket_cpu = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de cpu");

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);

    while (1)
    {
        sleep(10);
    }

    return 0;
}
