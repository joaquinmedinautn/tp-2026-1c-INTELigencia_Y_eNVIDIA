#include "kernel_memory.h"


t_config *config;
t_log_level log_level;
t_log *logger;
t_dictionary *procesos;
pthread_mutex_t mutex_procesos;

char* scripts_path;

int socket_servidor;
int puerto_escucha;
int socket_kernel_scheduler;
int socket_memory_stick;
int socket_cpu;
int socket_swap;
int socket_misterioso;
int instruction_delay;

void* memoria_principal;
t_bitarray* bitmap_marcos;
int tam_memoria;
int tam_pagina;
int cant_marcos;

t_dictionary* tablas_paginas;

void ObtenerConfig() {
    config = config_create("kernel_memory.config");
    if (config == NULL){
        perror("No se pudo crear el config");
        exit(EXIT_FAILURE);
    }
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    instruction_delay = config_get_int_value(config, "INSTRUCTION_DELAY");
    
    scripts_path = string_duplicate(config_get_string_value(config, "SCRIPTS_BASEPATH"));

    char* log_level_str = config_get_string_value(config, "LOG_LEVEL");
    log_level = log_level_from_string(log_level_str);

    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);

    config_destroy(config);
    
}

void InicarEstructurasMemoria(){

    t_config* config_temp = config_create("kernel_memory.config");
    tam_memoria = config_get_int_value(config_temp, "TAM_MEMORIA");
    tam_pagina = config_get_int_value(config_temp, "TAM PAGINA");
    config_destroy(config_temp);

    cant_marcos = tam_memoria / tam_pagina

    memoria_principal =malloc(tam_memoria);
    memset(memoria_principal, 0, tam_memoria);

    int bytes_bitarray = cant_marcos / 8;
    if (cant_marcos  % 8 != 0) bytes_bitarray++;

    void* espacio_bitarray = malloc(bytes_bitarray);
    memset(espacio_bitarray, 0, bytes_bitarray);

    bitmap_marcos = bitarray_create_with_size(espacio_bitarray, cant_marcos);

    log_info(logger, "## Estructuras de Memoria iniciadas. Tamaño: %d, bytes, Marcos: %d", tam_memoria, cant_marcos);
}

// ejecuto cada hilo 
void atender_cliente(void* void_socket) {
    int socket_cliente = *(int*)void_socket;
    free(void_socket);


    int cod_op = RecibirOperacion(socket_cliente);
    
    switch (cod_op) {
        case KERNEL_SCHEDULER:
            log_info(logger, "## Kernel Scheduler Conectado");
            break;
        case CPU_A_KERNEL_MEMORY:
            socket_cpu = socket_cliente;
            log_info(logger, "## CPU Conectada");
            break;
        case SWAP:
            socket_swap = socket_cliente;
            break;
        default:
            log_error(logger, "Código de operación desconocido al iniciar");
            close(socket_cliente);
            return;
    }

    // bucle de mensajes 
    while (1) {
        int cod_op_mensaje = RecibirOperacion(socket_cliente);
        
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

                //creo tabla de paginas vacia
                t_list* nueva_tabla = list_create();
                char* key =  string_itoa(pid);
                pthread_mutex_lock(&mutex_procesos);
                dictionary_put(tablas_paginas, key, nueva_tabla);
                pthread_mutex_unlock(&mutex_procesos);
                free(key);

                log_info(logger, "## PID: %d  Proceso Creado", pid); 

                EnviarOpCode(socket_cliente, PROCESO_CARGADO); 
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case SOLICITUD_INSTRUCCION: {
               t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t pc = *(uint32_t*)list_get(valores, 1);

                pthread_mutex_lock(&mutex_procesos);
                char* instruccion = obtener_instruccion(pid, pc); 
                pthread_mutex_unlock(&mutex_procesos);

                usleep(instruction_delay * 1000); // latencia obligatoria

                if (instruccion == NULL) instruccion = "EXIT"; 

                log_info(logger, "## PID: %d %d Instrucción: %s", pid, pc, instruccion); 

                // envio el string a la CPU
                t_paquete* res = CrearPaquete(SOLICITUD_INSTRUCCION);
                AgregarAPaquete(res, instruccion, strlen(instruccion) + 1);
                EnviarPaquete(res, socket_cliente);
                EliminarPaquete(res);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case LECTURA_MEMORIA: { // Mock, la CPu enviara pedidos de lectura, como no hay segmentacion real, el memory loguea la peticion y devuele un valor generico 0 
                                    // para que la cpu no quede en la espera 
                //logica de repeticion 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_fisica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);

                uint32_t dir_fisica =  traducir_direccion_logica(pid, dir_logica);

                // log obligatorio de acceso a espacio de usuario 
                log_info(logger, "## PID: %d Lectura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio);

                // respuesta mock (valor 0) para que la CPU no se bloquee
                
                void* buffer_leido =  malloc(tamanio);
                memcpy(buffer_leido, memoria_principal + dir_fisica, tamanio);

                t_paquete* res = CrearPaquete(LECTURA_MEMORIA);
                AgregarAPaquete(res, &valor_mock, sizeof(uint32_t));
                EnviarPaquete(res, socket_cliente);
                
                EliminarPaquete(res);
                free(buffer_leido);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case ESCRITURA_MEMORIA: { // Mock, se logea la direccion fisica y el tamanio, pero no se persiste en ningun lado. 
                                    //Se responde con un ok (MENSAJE) para confirmar la repeticion. 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_fisica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);
                void* datos = list_get(valores, 3);

                uint32_t dir_fisica = traducir_direccion_logica(pid, dir_logica);

                log_info(logger, "## PID: %d Escritura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio); 
                
                memcpy(memoria_principal + dir_fisica, datos_a_escribir, tamanio),
                
                EnviarOpCode(socket_cliente, MENSAJE); 

                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case SOLICITUD_ESPACIO_LIBRE: { // mock, el scheduler necesita saber si hay espacio para pasar procesos a READY.
                                            // devolvemos un valor fijo, para permitir el avance de las pruebas.
                uint32_t espacio_libre = 1024; // valor fijo mock
                
                t_paquete* res = CrearPaquete(SOLICITUD_ESPACIO_LIBRE);
                AgregarAPaquete(res, &espacio_libre, sizeof(uint32_t));
                EnviarPaquete(res, socket_cliente);
                EliminarPaquete(res);
                break;
            }
            case ASIGNAR_MEMORIA:{
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid= *(uint32_t*)list_get(valores, 0);
                int paginas_necesarias = *(int*)list_get(valores, 1);

                if(asignar_paginas_proceso(pid,  paginas_necesarias)){
                    EnviarOpCode(socket_cliente, MENSAJE);
                } else {
                    EnviarOpCode(socket_cliente, ESPACIO_INSUFICIENTE);
                }
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case FINALIZAR_PROCESO:{
                    t_list* valores =  RecibirPaquete(socket_cliente);
                    uint32_t pid = *(uint32_t*) list_get(valores, 0);
                    char* key = string_itoa(pid);

                    pthread_mutex_lock(&mutex_procesos);
                    dictionary_remove_and_destroy(procesos, key, eliminar_contexto);
                    pthread_mutex_unlock(&mutex_procesos);

                    liberar_memoria_proceso(pid);

                    log_info(logger, "## PID: %d - Proceso Finalizado (Memodia liberada)", pid);

                    free(key);
                    list_destroy_and_destroy_elements(valores,free);
                    break;
            }
            default:
                log_warning(logger, "Operación desconocida");
                break;
        }
    }
    close(socket_cliente);
}

uint32_t traducir_direccion_logica(uint32_t pid,uint32_t dir_logica){

    uint32_t numero_pagina = dir_logica / tam_pagina;
    uint32_t offset = dir_logica % tam_pagina;

    //busco tabla de paginas del proceso
    char* key = string_itoa(pid);
    pthread_mutex_lock(&mutex_procesos);
    t_list* tabla =  dictionary_get(tablas_paginas, key);
    pthread_mutex_unlock(&mutex_procesos);
    free(key);

    //obtengo el marco fisico
    int* marco_ptr = list_get(tabla, numero_pagina);
    if (marco_ptr == NULL){
        log_error(logger, "Error: Página %d no está asignada en PID %d",  numero_pagina, pid );
        return -1;
    } 

    uint32_t marco = *marco_ptr;
    
    log_info(logger, "## PID: %d - TPREG: %d - MARCO: %d", pid, numero_pagina, marco);
    
    return (marco * tam_pagina) + offset;
} 

void liberar_memoria_proceso(uint32_t pid){
    char*key  = string_itoa(pid);
    pthread_mutex_lock(&mutex_paginas);
    t_list* tabla = dictionary_reove(tablas_paginas, key);
    pthread_mutex_unlock(&mutex_paginas);
    free(key);

    if (tabla != NULL){
        for (int i = 0; i < list_size(tabla); i++){
            int* marco = list_get(tabla, i);
            bitarray_clean_bit(bitmap_marcos, *marco);
            free(marco);

        }
        list_destroy(tabla);
    }
}

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    procesos = dictionary_create();
    tablas_paginas = dictionary_create();

    pthread_mutex_init(&mutex_procesos, NULL);

    InicarEstructurasMemoria();

    
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

    dictionary_destroy_and_destroy_elements(procesos, eliminar_contexto);
    pthread_mutex_destroy(&mutex_procesos);
    free(scripts_path);

    free(memoria_principal);
    bitarray_destroy(bitmap_marcos);

    log_destroy(logger);
    return 0;
}