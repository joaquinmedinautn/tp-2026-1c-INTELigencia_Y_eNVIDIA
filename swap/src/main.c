#include "swap.h"

t_config *config;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log_level log_level;
t_log *logger;
int socket_kernel_memory;

void ObtenerConfig(char* path) {
    config = config_create(path);
    ip_kernel_memory     = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    config_destroy(config);
}

int main(int argc, char *argv[])
{

    saludar("swap");
    ObtenerConfig(argv[1]);

    logger = log_create("swap.log", "SWAP", true, log_level);
    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    log_info(logger,"## Conectado a Kernel Memory");

    //Parte del Handshake
    utils_op_code_kernel_memory mi_tipo = SWAP;
    send(socket_kernel_memory, &mi_tipo, sizeof(mi_tipo), 0);

    while (1)
    {
        sleep(10);
    }

    return 0;
}