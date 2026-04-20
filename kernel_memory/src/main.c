##include "kernel_memory.h"
#include <pthread.h>

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
int instruction_delay;




// ejecuto cada hilo 
void atender_cliente(void* void_socket) {
    int socket_cliente = *(int*)void_socket;
    free(void_socket);


    int cod_op = recibir_operacion(socket_cliente);
    
    switch (cod_op) {
        case KERNEL_SCHEDULER:
            log_info(logger, "## Kernel Scheduler Conectado");
            break;
        case CPU:
            log_info(logger, "## CPU Conectada");
            break;
//..... 
        default:
            log_error(logger, "Código de operación desconocido al iniciar");
            close(socket_cliente);
            return;
    }

    // bucle de mensajes 
    while (1) {
        int cod_op_mensaje = recibir_operacion(socket_cliente);
        
        if (cod_op_mensaje == -1) {
            log_error(logger, "Cliente desconectado");
            break;
        }

        switch (cod_op_mensaje) {
            case CREAR_PROCESO: 

                log_info(logger, "## PID: <PID> Proceso Creado"); 
                break;

            case SOLICITUD_INSTRUCCION: 
                usleep(instruction_delay * 1000);
                log_info(logger, "## PID: <PID> <PC> Instrucción: <TEXTO>"); 
                break;

            default:
                log_warning(logger, "Operación desconocida");
                break;
        }
    }
    close(socket_cliente);
}

int main(int argc, char *argv[]) {
    saludar("kernel_memory");
    ObtenerConfig();
    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);

    socket_servidor = IniciarServidor(puerto_escucha);
    log_info(logger, "## Kernel Memory escuchando en puerto %d", puerto_escucha);

    while (1) {
        int socket_cliente = EsperarConexion(socket_servidor);

        pthread_t hilo;
        int* p_socket = malloc(sizeof(int));
        *p_socket = socket_cliente;

        if (pthread_create(&hilo, NULL, (void*)atender_cliente, p_socket) != 0) {
            log_error(logger, "Error al crear el hilo para el cliente");
            free(p_socket);
            close(socket_cliente);
        } else {
            pthread_detach(hilo);
        }
    }
    return 0;
}