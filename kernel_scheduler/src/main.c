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
int op_code;
t_list* lista;
t_paquete* paquete;

void ObtenerConfig()
{
    config = config_create("kernel_scheduler.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    
    config_destroy(config);
}

t_paquete* Paquete(int socket_conexion)
{
    t_paquete* paquete = CrearPaquete();
    EnviarPaquete(paquete, socket_conexion);
}

void IdentificarCliente()
{
    socket_servidor = IniciarServidor(puerto_escucha);
    for (int i = 0; i < ; i++)
    {
        socket_misterioso = EsperarConexion(socket_servidor);
        int op_code = RecibirOperacion(socket_misterioso);
        switch (op_code)
        {
        case CPU_A_KERNEL_SCHEDULER:
            socket_cpu = socket_misterioso;
            break;

        case IO:
            socket_io = socket_misterioso;
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

    saludar("kernel_scheduler");
    ObtenerConfig();

    logger = log_create("kernel_scheduler.log", "KERNEL_SCHEDULER", true, log_level);

    IdentificarCliente();

    socket_servidor = IniciarServidor(puerto_escucha);

    socket_io = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de io");
    
    socket_cpu = EsperarConexion(socket_servidor);
    log_info(logger, "Se conecto el modulo de cpu");

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    
    EnviarOpCode(socket_kernel_memory, KERNEL_SCHEDULER);

    //paquete = CrearPaquete(KERNEL_SCHEDULER);

    //EnviarPaquete(paquete, socket_kernel_memory);



    while (1)
    {
        sleep(10);
    }

    return 0;
}
