#include <stdlib.h>
#include <stdio.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/utils.h>

int main(int argc, char* argv[]) {
    // chequeo que me pasen el config y el id, sino rompe
    if (argc < 3) {
        printf("Faltan parametros. Uso: ./cpu <path_config> <id_cpu>\n");
        return EXIT_FAILURE;
    }

    char* config_path = argv[1];
    char* cpu_id = argv[2];

    // arranco el logger
    t_log* logger = log_create("cpu.log", "CPU", 1, LOG_LEVEL_INFO);
    log_info(logger, "Arrancando CPU %s", cpu_id);

    // levanto el archivo de config
    t_config* config = config_create(config_path);
    if (config == NULL) {
        log_error(logger, "No se encontro el config en %s", config_path);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // saco ips y puertos
    char* ip_scheduler = config_get_string_value(config, "IP_KERNEL_SCHEDULER");
    int puerto_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");
    
    char* ip_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    int puerto_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    // me conecto al Kernel Scheduler
    log_info(logger, "Conectando al Kernel Scheduler en %s:%d", ip_scheduler, puerto_scheduler);
    int fd_scheduler = CrearConexion(ip_scheduler, puerto_scheduler);
    if (fd_scheduler > 0) {
        log_info(logger, "Conectado al Kernel Scheduler");
    } else {
        log_error(logger, "Fallo conexion con Kernel Scheduler");
    }

    // me conecto a la Kernel Memory
    log_info(logger, "Conectando al Kernel Memory en %s:%d", ip_memory, puerto_memory);
    int fd_memory = CrearConexion(ip_memory, puerto_memory);
    if (fd_memory > 0) {
        log_info(logger, "Conectado al Kernel Memory");
    } else {
        log_error(logger, "Fallo conexion con Kernel Memory");
    }

    // TODO: borrar el while para el checkpoint 2 cuando hagamos el ciclo de instruccion
    // por ahora lo dejo loopeando para que no muera el proceso y el ayudante vea la conexion
    while (1) {
        sleep(10); 
    }

    // no va a llegar aca nunca por el while, pero lo dejo para no perder memoria
    config_destroy(config);
    log_destroy(logger);
    
    return 0;
}