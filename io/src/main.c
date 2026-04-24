#include "io.h"

t_config *config;
char *ip_kernel_scheduler;
int puerto_kernel_scheduler;
t_log_level log_level;
t_log *logger;
int socket_kernel_scheduler;

void ObtenerConfig() {
    config = config_create("io.config");
    ip_kernel_scheduler     = strdup(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    config_destroy(config);
}

int main(int argc, char *argv[])
{
    saludar("I/O");
    ObtenerConfig();

    char* tipo = argv[1]; // STDIN, STDOUT o SLEEP

    logger = log_create("io.log", "IO", true, log_level);
    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);

    //La parte del Handshake
    utils_op_code_kernel_scheduler mi_tipo = IO;
    send(socket_kernel_scheduler, &mi_tipo, sizeof(mi_tipo), 0);

    
    while (1)
    {
        sleep(10);
    }

    return 0;
}
