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
int socket_misterioso;

void ObtenerConfig()
{
    config = config_create("kernel_memory.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    config_destroy(config);
}

void IdentificarCliente()
{
    socket_servidor = IniciarServidor(puerto_escucha);
    for (int i = 0; i < 4; i++)
    {
        socket_misterioso = EsperarConexion(socket_servidor);
        int op_code = RecibirOperacion(socket_misterioso);
        switch (op_code)
        {
        case KERNEL_SCHEDULER:
            socket_kernel_scheduler = socket_misterioso;
            break;

        case MEMORY_STICK:
            socket_memory_stick = socket_misterioso;
            break;

        case CPU:
            socket_cpu = socket_misterioso;
            break;

        case SWAP:
            socket_swap = socket_misterioso;
            break;

        default:
            perror("Error: codigo de operación desconocido");
            exit(EXIT_FAILURE);
            break;
        }
    }
}

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);

    IdentificarCliente();

    //socket_kernel_scheduler = EsperarConexion(socket_servidor);
    //log_info(logger, "Se conecto el modulo de kernel scheduler");

    //socket_memory_stick = EsperarConexion(socket_servidor);
    //log_info(logger, "Se conecto el modulo de memory stick");

    //socket_cpu = EsperarConexion(socket_servidor);
    //log_info(logger, "Se conecto el modulo de cpu");

    //socket_swap = EsperarConexion(socket_servidor);
    //log_info(logger, "Se conecto el modulo de swap");


    while (1)
    {
        sleep(10);
    }

    return 0;
}
