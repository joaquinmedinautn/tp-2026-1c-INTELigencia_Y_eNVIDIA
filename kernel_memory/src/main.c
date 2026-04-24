#include "kernel_memory.h"
#include <pthread.h>

t_config *config;
t_log_level log_level;
t_log *logger;
t_dictionary *procesos;
pthread_mutex_t mutex_procesos;

int socket_servidor;
int puerto_escucha;
int socket_kernel_scheduler;
int socket_memory_stick;
int socket_cpu;
int socket_swap;
int socket_misterioso;
int instruction_delay;



void ObtenerConfig() {
    config = config_create("kernel_memory.config");
    if (config == NULL){
        perror("No se pudo crear el config");
        exit(EXIT_FAILURE);
    }
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    instruction_delay = config_get_int_value(config, "INTRUCTION_DELAY");
    scripts_path = string_duplicate(config_get_string_value(config, "SCRIPTS_BASEPATH"));

    char* log_level_str = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);

    config_destroy(config);
    
}

// ejecuto cada hilo 
void atender_cliente(void* void_socket) {
    int socket_cliente = *(int*)void_socket;
    free(void_socket);


    int cod_op = recibir_operacion(socket_cliente);
    
    switch (cod_op) {
        case KERNEL_SCHEDULER:
            log_info(logger, "## Kernel Scheduler Conectado");
            break;
        case CPU_A_KERNEL_MEMORY:
            log_info(logger, "## CPU Conectada");
            break;
        case CPU_A_KERNEL_MEMORY:
            socket_cpu = socket_misterioso;
            break;

        case SWAP:
            socket_swap = socket_misterioso;
            break;
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
            case CREAR_PROCESO: {

                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                char* path = list_get(valores, 1);

                cargar_instrucciones(pid, path); 
                log_info(logger, "## PID: %d  Proceso Creado", pid); 

                EnviarOpCode(socket_cliente, MENSAJE); 
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case SOLICITUD_INSTRUCCION: {
               t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t pc = *(uint32_t*)list_get(valores, 1);

                char* instruccion = obtener_instruccion(pid, pc); 
                usleep(instruction_delay * 1000); // latencia obligatoria
                log_info(logger, "## PID: %d %d Instrucción: %s", pid, pc, instruccion); 

                // envio el string a la CPU
                t_paquete* res = CrearPaquete(SOLICITUD_INSTRUCCION);
                AgregarAPaquete(res, instruccion, strlen(instruccion) + 1);
                EnviarPaquete(res, socket_cliente);
                EliminarPaquete(res);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            default:
                log_warning(logger, "Operación desconocida");
                break;
        }
    }
    close(socket_cliente);
}

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    procesos = dictionary_create();
    pthread_mutex_init(&mutex_procesos, NULL);

    
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

    dictionary_destroy(procesos);
    pthread_mutex_destroy(&mx_procesos);
    free(scripts_path);
    log_destroy(logger);

    return 0;
}