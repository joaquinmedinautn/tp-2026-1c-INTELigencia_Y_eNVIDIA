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
int tam_memoria;
int segment_max_size;
char* allocation_strategy;


int swap_block_size;
int swap_file_size;
int cant_bloques_swap;
t_bitarray* bitmap_swap;
pthread_mutex_t  mutex_bitmap_swap;


t_list* lista_huevos_libres;
pthread_mutex_t mutex_huecos;

t_dictionary* tabla_segmentos;
pthread_mutex_t mutex_tabla_segmentos;

t_dictionary* procesos;
pthread_mutex_t mutex_procesos;

bool ordenar_huecos_por_base(void* a, void* b){
    t_hueco_libre* h1 = (t_hueco_libre*) a;
    t_hueco_libre* h2 = (t_hueco_libre*) b;
    return h1->base < h2->base;
}

void consolidar_huecos_libres() {
    list_sort(lista_huecos_libres, ordenar_huecos_por_base);

    for (int i = 0; i < list_size(lista_huecos_libres) - 1; i++) {
        t_hueco_libre* h_actual = list_get(lista_huecos_libres, i);
        t_hueco_libre* h_sig = list_get(lista_huecos_libres, i + 1);

        if ((h_actual->base + h_actual->tamanio) == h_sig->base) {
            h_actual->tamanio += h_sig->tamanio;
            list_remove(lista_huecos_libres, i + 1);
            free(h_sig);
            i--; 
        }
    }
}

t_hueco_libre* buscar_hueco_libre(uint32_t tamanio_pedido) {
    t_hueco_libre* hueco_seleccionado = NULL;

    for (int i = 0; i < list_size(lista_huecos_libres); i++) {
        t_hueco_libre* hueco = list_get(lista_huecos_libres, i);

        if (hueco->tamanio >= tamanio_pedido) {
            if (hueco_seleccionado == NULL) {
                hueco_seleccionado = hueco;
            } else {
                if (strcmp(allocation_strategy, "BEST") == 0) {
                    if (hueco->tamanio < hueco_seleccionado->tamanio) hueco_seleccionado = hueco;
                } else if (strcmp(allocation_strategy, "WORST") == 0) {
                    if (hueco->tamanio > hueco_seleccionado->tamanio) hueco_seleccionado = hueco;
                }
            }
        }
    }
    return hueco_seleccionado;
}

void ObtenerConfig() {
    config = config_create("kernel_memory.config");
    if (config == NULL){
        perror("No se pudo crear el config");
        exit(EXIT_FAILURE);
    }
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    instruction_delay = config_get_int_value(config, "INSTRUCTION_DELAY");
    scripts_path = string_duplicate(config_get_string_value(config, "SCRIPTS_BASEPATH"));

    tam_memoria = config_get_int_value(config_temp, "TAM_MEMORIA");
    segment_max_size = config_get_int_value(config, "SEGMENT_MAX_SIZE");
    allocation_strategy = string_duplicate(config_get_string_value(config, "ALLOCATION_STRATEGY" ));

    log_level =  log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);
    config_destroy(config);
    
}

void InicarEstructurasMemoria(){

    memoria_principal =malloc(tam_memoria);
    memset(memoria_principal, 0, tam_memoria);

    lista_huecos_libres = list_create();
    pthread_mutex_init(&mutex_huecos, NULL);

    t_hueco_libre* hueco_inicial= malloc(sizeof(t_hueco_libre));
    hueco_inicial->base=0;
    hueco-incial->tamanio = tam_memoria;
    list_add(lista_huecos_libres,hueco_inicial);
    
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
        case SWAP:{
            socket_swap = socket_cliente;

            t_list* valores_swap = RecibirPaquete(socket_cliente);
            swap_block_size  = *(int*)list_get(valores_swap, 0);
            swap_file_size   = *(int*)list_get(valores_swap, 1);

            cant_bloques_swap =  swap_block_size / swap_block_size;

            int bytes_bitarray_swap = cant_bloques_swap /  8;
            if (cant_bloques_swap % 8 != 0) bytes_bitarray_swap++;

            void* espacio_bitarray_swap = malloc(bytes_bitarray_swap);
            memset(espacio_bitarray_swap, 0, bytes_bitarray_swap);
            
            bitmap_swap  = bitarray_create_with_size(espacio_bitarray_swap, cant_bloques_swap);
            pthread_mutex_init(&mutex_bitmap_swap, NULL);

            log_info(logger, "## SWAP Conectado - Bloques: %d, Tamaño total: %d bytes", cant_bloques_swap, swap_file_size);
            
            list_destroy_and_destroy_elements(valores_swap, free);
            break;
        }
            

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
                pthread_mutex_lock(&mutex_tabla_segmentos);
                dictionary_put(tablas_paginas, key, nueva_tabla);
                pthread_mutex_unlock(&mutex_tabla_segmentos);
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
            case LECTURA_MEMORIA: { 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_fisica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);

                int dir_fisica =  traducir_direccion_logica(pid, dir_logica, tamanio);
                
                if (dir_fisica == -1 ){
                    EnviarOpCode(socket_cliente,SEGMENTATION_FAULT);
                }  else  {

                log_info(logger, "## PID: %d Lectura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio);
                
                void* buffer_leido =  malloc(tamanio);
                memcpy(buffer_leido, memoria_principal + dir_fisica, tamanio);

                t_paquete* res = CrearPaquete(LECTURA_MEMORIA);
                AgregarAPaquete(res, buffer_leido, tamanio);
                EnviarPaquete(res, socket_cliente);
                
                EliminarPaquete(res);
                free(buffer_leido);
                }
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case ESCRITURA_MEMORIA: { 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_fisica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);
                void* datos = list_get(valores, 3);

                uint32_t dir_fisica = traducir_direccion_logica(pid, dir_logica);
                if(dir_fisica == -1){
                    EnviarOpCode(socket_cliente, SEGMENTATION_FAULT);
                }else{log_info(logger, "## PID: %d Escritura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio); 
                
                memcpy(memoria_principal + dir_fisica, datos_a_escribir, tamanio),
                
                EnviarOpCode(socket_cliente, MENSAJE); 
                }
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
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t id_segmento = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio_pedido = *(uint32_t*)list_get(valores, 2);

                pthread_mutex_lock(&mutex_huecos);
                t_hueco_libre* hueco = buscar_hueco_libre(tamanio_pedido);

                if (hueco != NULL) {
                    // hay espacio
                    t_segmento* nuevo_segmento = malloc(sizeof(t_segmento));
                    nuevo_segmento->pid = pid;
                    nuevo_segmento->id_segmento = id_segmento;
                    nuevo_segmento->base = hueco->base;
                    nuevo_segmento->tamanio = tamanio_pedido;
                    nuevo_segmento->presente = true;

                    char* key = string_itoa(pid);
                    pthread_mutex_lock(&mutex_tablas_segmentos);
                    t_list* tabla = dictionary_get(tablas_segmentos, key);
                    list_add(tabla, nuevo_segmento);
                    pthread_mutex_unlock(&mutex_tablas_segmentos);
                    free(key);

                    // achicar el hueco
                    if (hueco->tamanio == tamanio_pedido) {
                        list_remove_element(lista_huecos_libres, hueco);
                        free(hueco);
                    } else {
                        hueco->base += tamanio_pedido;
                        hueco->tamanio -= tamanio_pedido;
                    }
                    
                    log_info(logger, "## PID: %d Segmento Creado %d Tamaño: %d", pid, id_segmento, tamanio_pedido);
                    EnviarOpCode(socket_cliente, MENSAJE); // Exito
                } else {
                    // falta compactar
                    EnviarOpCode(socket_cliente, ESPACIO_INSUFICIENTE);
                }
                
                pthread_mutex_unlock(&mutex_huecos);
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

int traducir_direccion_logica(uint32_t pid, uint32_t dir_logica, uint32_t tamanio_operacion) {
    
    // formulas exactas del enunciado para separar la dirección
    uint32_t num_segmento = dir_logica / segment_max_size;
    uint32_t desplazamiento = dir_logica % segment_max_size;

    // busco la tabla de segmentos del PID en el diccionario
    char* key = string_itoa(pid);
    pthread_mutex_lock(&mutex_tabla_segmentos);
    t_list* tabla = dictionary_get(tabla_segmentos, key);
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    free(key);

    if (tabla_segmentos == NULL) {
        log_error(logger, "PID %d no tiene tabla de segmentos.", pid);
        return -1;
    }

    // busco el segmento específico en la lista
    t_segmento* segmento_buscado = NULL;
    for (int i = 0; i < list_size(tabla); i++) {
        t_segmento* seg = list_get(tabla, i);
        if (seg->id_segmento == num_segmento) {
            segmento_buscado = seg;
            break;
        }
    }

    if (segmento_buscado == NULL) {
        log_error(logger, "Error: Segmento %d no encontrado para PID %d", num_segmento, pid);
        return -1;
    }

    // verifico  Segmentation Fault 
    if ((desplazamiento + tamanio_operacion) > segmento_buscado->tamanio) {
        log_error(logger, "SEG_FAULT: Acceso fuera de límite en segmento %d", num_segmento);
        return -1; 
    }

    // calculo final de la dirección física
    uint32_t dir_fisica = segmento_buscado->base + desplazamiento;
    return dir_fisica;
}

void liberar_memoria_proceso(uint32_t pid){
    char*key  = string_itoa(pid);
    pthread_mutex_lock(&mutex_tabla_segmentos);
    t_list* tabla = dictionary_remove(tablas_segmentos, key);
    pthread_mutex_unlock(&mutex_abla_segmentos);
    free(key);

    if (tabla != NULL){
        pthread_mutex_lock(&mutex_huecos)
        for (int i = 0; i < list_size(tabla); i++){
            t_segmento* = list_get(tabla, i);
            
            //devuelvo espacio a los  huevcos libres
            t_hueco_libre* nuevo_hueco =malloc(sizeof(t_hueco_libre));
            nuevo_hueco->base = seg->base;
            nuevo_hueco->tamanio = seg->tamanio;
            list_add(lista_huecos_libres, nuevo_hueco);
            free(seg);

        }
        consolidar_huecos_libres();
        pthread_mutex_unlock(&mutex_huecos)
        list_destroy(tabla);
    }
}

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    procesos = dictionary_create();
    pthread_mutex_init(&mutex_procesos, NULL)
    
    tablas_segmentos = dictionary_create();
    pthread_mutex_init(&mutex_tabla_segmentos, NULL)

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
    free(allocation_strategy);
    free(memoria_principal);

    if (bitmap_swap != NULL) bitarray_destroy(bitmap_swap);
    
    log_destroy(logger);
    return 0;
}