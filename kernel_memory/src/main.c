#include <stdlib.h>
#include <stdio.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/utils.h> 



int socket_servidor;
int puerto_escucha;
t_config* config;
t_log* logger;

int main(int argc, char* argv[]) {
    saludar("kernel_memory");
    
   
    config = config_create("kernel_memory.config");
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, LOG_LEVEL_INFO);

   
    socket_servidor = IniciarServidor(puerto_escucha);
    log_info(logger, "Servidor Memoria escuchando en puerto %d", puerto_escucha);
    
    // Espero a la CPU primero 
    int socket_cpu = EsperarConexion(socket_servidor);
    log_info(logger, "## CPU Conectada");

    // Espero al Scheduler 
    int socket_scheduler = EsperarConexion(socket_servidor);
    log_info(logger, "## Kernel Scheduler Conectado %d", socket_scheduler); 

    // Espero el mmory Stick
    int socket_stick = EsperarConexion(socket_servidor);
    log_info(logger, "## Memory Stick Conectada"); 

    while(1) { sleep(10); }

    return 0;
}