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
int memory_delay;

void* espacio_almacenamiento;
int tam_memoria;

char* ip_propia;

void ObtenerConfig(char* path)
{
    config = config_create(path);
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    ip_propia = strdup(config_get_string_value(config, "IP_PROPIA"));
    memory_delay = config_get_int_value(config, "MEMORY_DELAY");
}

void atender_peticiones(void* arg){
    int socket_cliente = *(int*)arg;
    free(arg);
    
    while(1){
        int cod_op = RecibirOperacion(socket_cliente);
        if  (cod_op == -1){
            log_error(logger, "Cliente Desconectado del Memory Stick");
            break;
        }
    switch(cod_op){
        case CPU_A_MEMORY_STICK:{
            log_info(logger, "## CPU conectada al Memory Stick");
            break;
            }
        case LEER:{
            usleep(memory_delay * 1000);
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t offset_local = *(uint32_t*)list_get(valores, 0);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 1);

                log_info(logger, "## Lectura de %d bytes", tamanio);
                
                void* buffer_leido =  malloc(tamanio);
                memcpy(buffer_leido, (char*)espacio_almacenamiento + offset_local, tamanio);

                t_paquete* res = CrearPaquete(LEER);
                AgregarAPaquete(res, buffer_leido, tamanio);
                EnviarPaquete(res, socket_cliente);
                
                EliminarPaquete(res);
                free(buffer_leido);
                
                list_destroy_and_destroy_elements(valores, free);
                break;
        }
        case ESCRIBIR: { 
            usleep(memory_delay * 1000);
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t offset_local = *(uint32_t*)list_get(valores, 0);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 1);
                void* datos = list_get(valores, 2);

                log_info(logger, "## Escritura de %d bytes", tamanio); 
                
                memcpy((char*)espacio_almacenamiento + offset_local, datos, tamanio);
                
                EnviarOpCode(socket_cliente, MENSAJE); 
                
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
        default:
            log_warning(logger, "Operacion desconocida en Memory Stick: %d", cod_op);
            break;
        }
    }
    close(socket_cliente);
}


int main(int argc, char* argv[]) {
    
    if (argc < 3) {
        printf("Uso: %s [Archivo Config] [Tamanio]\n", argv[0]);
        return EXIT_FAILURE;
    }

    tam_memoria = atoi(argv[2]);

    if (tam_memoria <= 0) {
        printf("Error: tamaño inválido '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }

    espacio_almacenamiento = malloc(tam_memoria);
    memset(espacio_almacenamiento, 0, tam_memoria);

    saludar("memory_stick");

    ObtenerConfig(argv[1]);

    logger = log_create("memory_stick.log", "MEMORY_STICK", true, log_level);

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    if(socket_kernel_memory > 0){
        t_paquete* paq_registro = CrearPaquete(NUEVO_MEMORY_STICK);
        AgregarAPaquete(paq_registro, &tam_memoria, sizeof(int));
        
        //mando el puerto de escucha de este stick
        AgregarAPaquete(paq_registro, &puerto_escucha, sizeof(int));
        AgregarAPaquete(paq_registro,  ip_propia, strlen(ip_propia) + 1);
        //mando el ip de este stick

        EnviarPaquete(paq_registro, socket_kernel_memory);
        EliminarPaquete(paq_registro);

        log_info(logger, "## Memory Stick de %d bytes conectado al Kernel Memory",tam_memoria);

        pthread_t hilo_km;
        int *p_socket_km = malloc(sizeof(int));
        *p_socket_km = socket_kernel_memory;

        if (pthread_create(&hilo_km, NULL, (void*)atender_peticiones, p_socket_km) != 0) {
            log_error(logger, "Error al crear el hilo de atención para Kernel Memory");
            free(p_socket_km);
            free(espacio_almacenamiento); 
            return EXIT_FAILURE; 
        } else {
            pthread_detach(hilo_km);
        }

    }else{
        log_error(logger, "No se pudo conectar al Kernel Memory. Cerrando" );
        free(espacio_almacenamiento);
        return EXIT_FAILURE;
    }

    socket_servidor = IniciarServidor(puerto_escucha);
    log_info(logger, "## Memory Stick escuchando peticiones en el puerto %d", puerto_escucha);

    int socket_cliente;
    while(1){
        socket_cliente = EsperarConexion(socket_servidor);
        log_info(logger, "Se conecto el modulo de cpu"); //agregue algo mas joaco

        pthread_t hilo;
        int* p_socket = malloc(sizeof(int));
        *p_socket = socket_cliente;

        if (pthread_create(&hilo, NULL, (void*)atender_peticiones, p_socket) != 0){
            log_error(logger, "Error al crear el hilo de atención");
            free(p_socket);
            close(socket_cliente);

        }else{
            pthread_detach(hilo);
        }  
    }
     free(ip_kernel_memory);
    free(espacio_almacenamiento);
    log_destroy(logger);

    return 0;
}
