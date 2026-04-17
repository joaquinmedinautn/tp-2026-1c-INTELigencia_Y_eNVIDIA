#include "swap.h"

t_config *config;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log_level log_level;
t_log *logger;
int socket_kernel_memory;

void ObtenerConfig()
{
    config = config_create("swap.config");
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
}

int main(int argc, char *argv[])
{

    saludar("swap");
    ObtenerConfig();

    logger = log_create("swap.log", "SWAP", true, log_level);
    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);

    while (1)
    {
        sleep(10);
    }

    return 0;
}