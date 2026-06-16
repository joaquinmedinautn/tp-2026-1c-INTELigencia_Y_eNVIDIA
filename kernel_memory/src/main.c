#include "kernel_memory.h"


t_config *config;
t_log_level log_level;
t_log *logger;


char* scripts_path;
int socket_servidor;
int puerto_escucha;
int socket_kernel_scheduler;
int socket_memory_stick;
int socket_cpu;
int socket_swap;
int socket_misterioso;
int instruction_delay;


int segment_max_size;
char* allocation_strategy;

int socket_kernel_scheduler_eventos = -1;


int swap_block_size;
int swap_file_size;
int cant_bloques_swap;
t_bitarray* bitmap_swap;
pthread_mutex_t  mutex_bitmap_swap;


t_list* lista_huecos_libres;
pthread_mutex_t mutex_huecos;

t_dictionary* tabla_segmentos;
pthread_mutex_t mutex_tabla_segmentos;

t_dictionary* procesos;
pthread_mutex_t mutex_procesos;

t_list* lista_memory_sticks;
pthread_mutex_t mutex_memory_sticks;

t_list* segmentos_a_compactar;


void manejo_de_memoria_corrupta(){
    log_error(logger, "Un Memory Stick falló");
    if (socket_kernel_scheduler_eventos != -1){
        EnviarOpCode(socket_kernel_scheduler_eventos, MEMORIA_CORRUPTA);
    }
}

t_memory_stick* buscar_stick_por_direccion(uint32_t dir_fisica){
    pthread_mutex_lock(&mutex_memory_sticks);
    for(int i = 0; i < list_size(lista_memory_sticks); i++){
        t_memory_stick* stick = list_get(lista_memory_sticks, i);
        if (dir_fisica >= stick->base_global && dir_fisica < stick->limite){
            pthread_mutex_unlock(&mutex_memory_sticks);
            return stick;
        }
    }
        pthread_mutex_unlock(&mutex_memory_sticks);
        return NULL;
}


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

    if (tabla == NULL) {
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
    t_list* tabla = dictionary_remove(tabla_segmentos, key);
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    free(key);

    if (tabla != NULL){
        pthread_mutex_lock(&mutex_huecos);
        for (int i = 0; i < list_size(tabla); i++){
            t_segmento* seg = list_get(tabla, i);
            
            //devuelvo espacio a los  huevcos libres
            t_hueco_libre* nuevo_hueco =malloc(sizeof(t_hueco_libre));
            nuevo_hueco->base = seg->base;
            nuevo_hueco->tamanio = seg->tamanio;
            list_add(lista_huecos_libres, nuevo_hueco);
            free(seg);

        }
        consolidar_huecos_libres();
        pthread_mutex_unlock(&mutex_huecos);
        list_destroy(tabla);
    }
}


void InicarEstructurasMemoria(){


    lista_huecos_libres = list_create();
    pthread_mutex_init(&mutex_huecos, NULL);

    lista_memory_sticks = list_create();
    pthread_mutex_init(&mutex_memory_sticks, NULL); 
    
    log_info(logger, "## Estructuras de Memoria iniciadas. Esperando Memory Sticks.");
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

    segment_max_size = config_get_int_value(config, "SEGMENT_MAX_SIZE");
    allocation_strategy = string_duplicate(config_get_string_value(config, "ALLOCATION_STRATEGY" ));

    log_level =  log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);
    //config_destroy(config);
    
}

void suspender_proceso(uint32_t pid){
    char* key = string_itoa(pid);
    pthread_mutex_lock(&mutex_tabla_segmentos);
    t_list* tabla = dictionary_get(tabla_segmentos, key);
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    free(key);

    if (tabla == NULL) return;

    for (int i = 0; i < list_size(tabla); i++) {
        t_segmento* seg = list_get(tabla, i);

        if (seg->presente) {
            // calculo cuántos bloques de SWAP necesitamos para este segmento
            int bloques_necesarios = (seg->tamanio + swap_block_size - 1) / swap_block_size;
            
            int offset_segmento = 0; // Para ir cortando el segmento de a pedazos

            pthread_mutex_lock(&mutex_bitmap_swap);
            for (int b = 0; b < bloques_necesarios; b++) {
                // busco un bloque libre en el bitmap del SWAP
                int bloque_libre = -1;
                for (int j = 0; j < cant_bloques_swap; j++) {
                    if (!bitarray_test_bit(bitmap_swap, j)) {
                        bitarray_set_bit(bitmap_swap, j);
                        bloque_libre = j;
                        break;
                    }
                }

                if (bloque_libre == -1) {
                    log_error(logger, "CRÍTICO: No hay espacio en SWAP para suspender PID %d", pid);
                    
                    pthread_mutex_unlock(&mutex_bitmap_swap);
                    return;
                }

                // anoto el bloque en la lista del segmento
                int* num_bloque = malloc(sizeof(int));
                *num_bloque = bloque_libre;
                list_add(seg->bloques_swap, num_bloque);

                // calculo cuánto vamos a enviar en este bloque
                int bytes_a_enviar = swap_block_size;
                if (offset_segmento + swap_block_size > seg->tamanio) {
                    bytes_a_enviar = seg->tamanio - offset_segmento;
                }

                uint32_t dir_fisica_leer = seg->base + offset_segmento;
                t_memory_stick* stick =buscar_stick_por_direccion(dir_fisica_leer);

                // leo de RAM y mandar al SWAP ESCRITURA_BLOQUE
                void* buffer_bloque = malloc(swap_block_size);
                memset(buffer_bloque, 0, swap_block_size); // lleno con ceros por las dudas
                
                if(stick != NULL){
                    uint32_t offset_local = dir_fisica_leer - stick->base_global;
                    t_paquete* paq_leer = CrearPaquete(LECTURA_BLOQUE);
                    AgregarAPaquete(paq_leer,  &offset_local, sizeof(uint32_t));
                    AgregarAPaquete(paq_leer, &bytes_a_enviar, sizeof(uint32_t));
                    EnviarPaquete(paq_leer, stick->socket);
                    EliminarPaquete(paq_leer);
                
                    if(RecibirOperacion(stick->socket) == LECTURA_BLOQUE){
                    t_list* resp = RecibirPaquete(stick->socket);
                    memcpy(buffer_bloque, list_get(resp, 0), bytes_a_enviar);
                    list_clean_and_destroy_elements(resp, free);
                    } else {
                    manejo_de_memoria_corrupta();
                    }
                } else {
                    manejo_de_memoria_corrupta();
                }

                t_paquete* paq_swap = CrearPaquete(ESCRITURA_BLOQUE); 
                AgregarAPaquete(paq_swap, &bloque_libre, sizeof(int));
                AgregarAPaquete(paq_swap, buffer_bloque, swap_block_size);
                EnviarPaquete(paq_swap, socket_swap);
                EliminarPaquete(paq_swap);
                free(buffer_bloque);

                // espero el OK del SWAP antes de mandar el siguiente
                RecibirOperacion(socket_swap); 

                offset_segmento += bytes_a_enviar;
            }
            pthread_mutex_unlock(&mutex_bitmap_swap);

            // libero el espacio en la RAM real (creo el hueco)
            pthread_mutex_lock(&mutex_huecos);
            t_hueco_libre* nuevo_hueco = malloc(sizeof(t_hueco_libre));
            nuevo_hueco->base = seg->base;
            nuevo_hueco->tamanio = seg->tamanio;
            list_add(lista_huecos_libres, nuevo_hueco);
            consolidar_huecos_libres();
            pthread_mutex_unlock(&mutex_huecos);

            // marco como que ya no está en RAM
            seg->presente = false;
            
            log_info(logger, "## PID: %d - Segmento %d movido a SWAP", pid, seg->id_segmento);
        }
    }
}

void desuspender_proceso(uint32_t pid, int socket_cliente) {
    char* key = string_itoa(pid);
    pthread_mutex_lock(&mutex_tabla_segmentos);
    t_list* tabla = dictionary_get(tabla_segmentos, key);
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    free(key);

    if (tabla == NULL) return;

    for (int i = 0; i < list_size(tabla); i++) {
        t_segmento* seg = list_get(tabla, i);

        // proceso solo los que están en SWAP
        if (!seg->presente) {
            pthread_mutex_lock(&mutex_huecos);
            t_hueco_libre* hueco = buscar_hueco_libre(seg->tamanio);
            
            if (hueco == NULL) {
                // El Scheduler se supone que verificó el espacio antes, pero por las dudas: 
                log_error(logger, "No hay hueco para des-suspender el segmento %d del PID %d", seg->id_segmento, pid);
                pthread_mutex_unlock(&mutex_huecos);
                return; 
            }

            // actualizo la base del segmento al nuevo hueco en RAM
            seg->base = hueco->base;

            // achicamos el hueco
            if (hueco->tamanio == seg->tamanio) {
                list_remove_element(lista_huecos_libres, hueco);
                free(hueco);
            } else {
                hueco->base += seg->tamanio;
                hueco->tamanio -= seg->tamanio;
            }
            pthread_mutex_unlock(&mutex_huecos);

            // traigo los bloques del SWAP a la RAM
            int offset_segmento = 0;
            pthread_mutex_lock(&mutex_bitmap_swap);
            for (int b = 0; b < list_size(seg->bloques_swap); b++) {
                int* num_bloque = list_get(seg->bloques_swap, b);

                // Pido el bloque al SWAP
                t_paquete* paq_lectura = CrearPaquete(LECTURA_BLOQUE);
                AgregarAPaquete(paq_lectura, num_bloque, sizeof(int));
                EnviarPaquete(paq_lectura, socket_swap);
                EliminarPaquete(paq_lectura);

                // recibo el contenido del bloque
                int op_recibida = RecibirOperacion(socket_swap);
                if (op_recibida == LECTURA_BLOQUE) {
                    t_list* valores_bloque = RecibirPaquete(socket_swap);
                    void* datos_bloque = list_get(valores_bloque, 0);

                    int bytes_a_copiar = swap_block_size;
                    if (offset_segmento + swap_block_size > seg->tamanio) {
                        bytes_a_copiar = seg->tamanio - offset_segmento;
                    }

                    uint32_t dir_fisica_escribir =  seg->base + offset_segmento;
                    t_memory_stick* stick = buscar_stick_por_direccion(dir_fisica_escribir);

                    if(stick != NULL){
                    uint32_t offset_local = dir_fisica_escribir - stick->base_global;
                    t_paquete* paq_escribir = CrearPaquete(ESCRIBIR);
                    AgregarAPaquete(paq_escribir,  &offset_local, sizeof(uint32_t));
                    AgregarAPaquete(paq_escribir, &bytes_a_copiar, sizeof(uint32_t));
                    AgregarAPaquete(paq_escribir, datos_bloque, bytes_a_copiar);
                    EnviarPaquete(paq_escribir, stick->socket);
                    EliminarPaquete(paq_escribir);
                    
                        if(RecibirOperacion(stick->socket) !=  MENSAJE){
                     manejo_de_memoria_corrupta();
                    }
                    } else {
                    manejo_de_memoria_corrupta();
                    }

                    offset_segmento += bytes_a_copiar;
                    list_destroy_and_destroy_elements(valores_bloque, free);
                }

                // libero el bloque en el bitmap del SWAP
                bitarray_clean_bit(bitmap_swap, *num_bloque);
            }
            pthread_mutex_unlock(&mutex_bitmap_swap);

            // limpio la lista de bloques del segmento porque ya no está en SWAP
            list_clean_and_destroy_elements(seg->bloques_swap, free);
            seg->presente = true;

            log_info(logger, "## PID: %d - Segmento %d restaurado en RAM (Base: %d)", pid, seg->id_segmento, seg->base);
        }
    }
}

void recolectar_segmentos_presentes(char* key, void* value) {
    t_list* tabla_proc = (t_list*)value;
    for (int i = 0; i < list_size(tabla_proc); i++) {
        t_segmento* seg = list_get(tabla_proc, i);
        if (seg->presente) {
            list_add(segmentos_a_compactar, seg);
        }
    }
}

void compactar_memoria() {
    log_info(logger, "## Iniciando Compactación...");

    // eecolectl TODOS los segmentos presentes en RAM
    segmentos_a_compactar = list_create();
    
    pthread_mutex_lock(&mutex_tabla_segmentos); 
    dictionary_iterator(tabla_segmentos, recolectar_segmentos_presentes);
    pthread_mutex_unlock(&mutex_tabla_segmentos);

    // ordeno los segmentos por su dirección base actual
    bool ordenar_segmentos_por_base(void* a, void* b) {
        return ((t_segmento*)a)->base < ((t_segmento*)b)->base;
    }
    list_sort(segmentos_a_compactar, ordenar_segmentos_por_base);

    // empujo todos los segmentos hacia el principio (Base 0)
    uint32_t desplazamiento_actual = 0; 


    for (int i = 0; i < list_size(segmentos_a_compactar); i++) {
        t_segmento* seg = list_get(segmentos_a_compactar, i);
        
        if (seg->base != desplazamiento_actual){
        t_memory_stick* stick_origen = buscar_stick_por_direccion(seg->base);
        uint32_t offset_origen = seg->base - stick_origen->base_global;
        
        t_paquete* p_leer = CrearPaquete(LEER);
        AgregarAPaquete(p_leer,  &offset_origen, sizeof(uint32_t));
        AgregarAPaquete(p_leer, &seg->tamanio, sizeof(uint32_t));
        EnviarPaquete(p_leer, stick_origen->socket);
        EliminarPaquete(p_leer);

        RecibirOperacion(stick_origen->socket);
        t_list* resp = RecibirPaquete(stick_origen->socket);
        void* buffer_temp = list_get(resp, 0);

        t_memory_stick*  stick_destino = buscar_stick_por_direccion(desplazamiento_actual);
        uint32_t offset_destino = desplazamiento_actual - stick_destino->base_global;

        t_paquete* p_escribir = CrearPaquete(ESCRIBIR);
        AgregarAPaquete(p_escribir,  &offset_destino, sizeof(uint32_t));
        AgregarAPaquete(p_escribir, &seg->tamanio, sizeof(uint32_t));
        AgregarAPaquete(p_escribir, buffer_temp, seg->tamanio);
        EnviarPaquete(p_escribir, stick_destino->socket);
        EliminarPaquete(p_escribir);

        RecibirOperacion(stick_destino->socket);

        list_clean_and_destroy_elements(resp,free);
        seg->base  =  desplazamiento_actual;
        }
        
        log_info(logger, "## PID: %d - Segmento: %d - Base: %d - Tamaño: %d", seg->pid, seg->id_segmento, seg->base, seg->tamanio);
        
        desplazamiento_actual += seg->tamanio;
    }
    list_destroy(segmentos_a_compactar);

    // destruyo la vieja lista de huecos y crear uno solo al final
    pthread_mutex_lock(&mutex_huecos);
    list_clean_and_destroy_elements(lista_huecos_libres, free);

    uint32_t ram_total = 0;
    for (int i=0; i<list_size(lista_memory_sticks);i++){
        ram_total += ((t_memory_stick*)list_get(lista_memory_sticks,i))->tamanio;
    }
    if(ram_total > desplazamiento_actual){
        t_hueco_libre* hueco_final = malloc(sizeof(t_hueco_libre));
        hueco_final->base = desplazamiento_actual;
        hueco_final->tamanio = ram_total - desplazamiento_actual;
        list_add(lista_huecos_libres, hueco_final);
         log_info(logger, "## Compactación finalizada. Hueco libre restante: %d bytes en Base %d", hueco_final->tamanio, hueco_final->base);
    }
    pthread_mutex_unlock(&mutex_huecos);
}

void registrar_nuevo_memory_stick(int tamanio_nuevo){


    EnviarOpCode(socket_kernel_scheduler_eventos, NUEVO_MEMORY_STICK);
    log_info(logger, "Notificando nuevo Memory Stick al Scheduler");

}

void manejar_desconexion_memory_stick(){

    EnviarOpCode(socket_kernel_scheduler_eventos, MEMORIA_CORRUPTA);
    log_error(logger,"CRÍTICO: Memory Stick desconectado. Notificando corrupción al Scheduler");

}

// ejecuto cada hilo 
void atender_cliente(void* void_socket) {
    int socket_cliente = *(int*)void_socket;
    free(void_socket);


    int cod_op = RecibirOperacion(socket_cliente);

    printf("Codigo de operacion: %d\n",cod_op);
    
    switch (cod_op) {
        case KERNEL_SCHEDULER:
            log_info(logger, "## Kernel Scheduler Conectado");
            break;
        case CPU_A_KERNEL_MEMORY:
            socket_cpu = socket_cliente;
            log_info(logger, "## CPU Conectada");

            pthread_mutex_lock(&mutex_memory_sticks);
            int cant_sticks  =list_size(lista_memory_sticks);

            t_paquete* paq_lista = CrearPaquete(NUEVO_MEMORY_STICK);
            AgregarAPaquete(paq_lista,  &cant_sticks,  sizeof(int));

            for(int i = 0; i < cant_sticks; i++ ){
                t_memory_stick* s = list_get(lista_memory_sticks, i);
                AgregarAPaquete(paq_lista, &s->tamanio, sizeof(uint32_t));
                AgregarAPaquete(paq_lista, &s->base_global, sizeof(uint32_t));
                AgregarAPaquete(paq_lista, &s->puerto, sizeof(int));
                AgregarAPaquete(paq_lista, &s->ip, strlen(s->ip) + 1);
            }
            pthread_mutex_unlock(&mutex_memory_sticks);

            EnviarPaquete(paq_lista, socket_cpu);
            EliminarPaquete(paq_lista);
            break;
        case SWAP:{
            socket_swap = socket_cliente;

            t_list* valores_swap = RecibirPaquete(socket_cliente);
            swap_block_size  = *(int*)list_get(valores_swap, 0);
            swap_file_size   = *(int*)list_get(valores_swap, 1);

            cant_bloques_swap =  swap_file_size / swap_block_size;

            int bytes_bitarray_swap = cant_bloques_swap /  8;
            if (cant_bloques_swap % 8 != 0) bytes_bitarray_swap++;

            void* espacio_bitarray_swap = malloc(bytes_bitarray_swap);
            memset(espacio_bitarray_swap, 0, bytes_bitarray_swap);
            
            bitmap_swap  = bitarray_create_with_mode(espacio_bitarray_swap, cant_bloques_swap, LSB_FIRST);
            pthread_mutex_init(&mutex_bitmap_swap, NULL);

            log_info(logger, "## SWAP Conectado - Bloques: %d, Tamaño total: %d bytes", cant_bloques_swap, swap_file_size);
            
            list_destroy_and_destroy_elements(valores_swap, free);
            break;
        }
        case KERNEL_SCHEDULER_EVENTOS:{
            socket_kernel_scheduler_eventos = socket_cliente;
            log_info(logger, "## Kernel Scheduler Eventos Conectado");
            break;
        }
        case NUEVO_MEMORY_STICK:{
            t_list* valores = RecibirPaquete(socket_cliente);
            uint32_t tamanio_stick = *(uint32_t*)list_get(valores, 0);
            int puerto_stick = *(int*)list_get(valores, 1);
            char* ip_stick = list_get(valores, 2);

            pthread_mutex_lock(&mutex_memory_sticks);
            uint32_t base_nueva = 0;
            if (!list_is_empty(lista_memory_sticks)) {
                t_memory_stick* ultimo = list_get(lista_memory_sticks, list_size(lista_memory_sticks) - 1);
                base_nueva = ultimo->limite;
            }

            t_memory_stick* nuevo_stick = malloc(sizeof(t_memory_stick));
            nuevo_stick->socket = socket_cliente;
            nuevo_stick->tamanio = tamanio_stick;
            nuevo_stick->base_global = base_nueva;
            nuevo_stick->limite = base_nueva + tamanio_stick;
            nuevo_stick->puerto = puerto_stick;
            nuevo_stick->ip = string_duplicate(ip_stick);
            list_add(lista_memory_sticks, nuevo_stick);
            pthread_mutex_unlock(&mutex_memory_sticks);

            pthread_mutex_lock(&mutex_huecos);
            t_hueco_libre* nuevo_hueco = malloc(sizeof(t_hueco_libre));
            nuevo_hueco->base = base_nueva;
            nuevo_hueco->tamanio = tamanio_stick;
            list_add(lista_huecos_libres, nuevo_hueco);
            pthread_mutex_unlock(&mutex_huecos);

            log_info(logger, "## Memory Stick de %d bytes mapeado en base %d", tamanio_stick, base_nueva);
            if(socket_cpu  != -1){
                t_paquete* paq_cpu = CrearPaquete(NUEVO_MEMORY_STICK);

                AgregarAPaquete(paq_cpu, &tamanio_stick, sizeof(uint32_t));
                AgregarAPaquete(paq_cpu, &base_nueva, sizeof(uint32_t));
                AgregarAPaquete(paq_cpu, &puerto_stick, sizeof(int));
                AgregarAPaquete(paq_cpu, &ip_stick, strlen(ip_stick) + 1);
                EnviarPaquete(paq_cpu,socket_cpu);
                EliminarPaquete(paq_cpu);
                log_info(logger,  "## Notificación de nuevo hardware enviada a la CPU");
            }
            if (socket_kernel_scheduler_eventos != -1) {
                EnviarOpCode(socket_kernel_scheduler_eventos, NUEVO_MEMORY_STICK);
            }
            list_destroy_and_destroy_elements(valores, free);
            return;
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
                dictionary_put(tabla_segmentos, key, nueva_tabla);
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

               
                char* instruccion = obtener_instruccion(pid, pc); 
                

                usleep(instruction_delay * 1000); // latencia obligatoria

                if (instruccion == NULL) {instruccion = strdup("EXIT");
                } else {
                    instruccion =  strdup(instruccion);
                } 

                log_info(logger, "## PID: %d %d Instrucción: %s", pid, pc, instruccion); 

                // envio el string a la CPU
                t_paquete* res = CrearPaquete(SOLICITUD_INSTRUCCION);
                AgregarAPaquete(res, instruccion, strlen(instruccion) + 1);
                EnviarPaquete(res, socket_cliente);
                EliminarPaquete(res);

                free(instruccion);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case SOLICITUD_CONTEXTO: {
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);

                char* key = string_itoa(pid);
                pthread_mutex_lock(&mutex_procesos);
                t_contexto* ctx = dictionary_get(procesos, key);
                pthread_mutex_unlock(&mutex_procesos);
                free(key);

                if (ctx != NULL) {
                    t_paquete* res = CrearPaquete(SOLICITUD_CONTEXTO);
                    
                    // mando el struct del contexto (los 11 registros clonados)
                    // no mando la t_list de instrucciones por red, solo el bloque fijo de registros
                    AgregarAPaquete(res, ctx, sizeof(t_contexto));
                    
                    EnviarPaquete(res, socket_cliente);
                    EliminarPaquete(res);
                    log_info(logger, "## Contexto enviado a CPU para PID: %d (PC: %d)", pid, ctx->PC);
                } else {
                    log_error(logger, "Error: CPU solicitó contexto para un PID inexistente: %d", pid);
                    //  envio un código de error a la CPU
                }

                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case CPU_A_KERNEL_MEMORY: { // el que usa la CPU en actualizar_contexto()
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                t_contexto* contexto_cpu = (t_contexto*)list_get(valores, 1);

                char* key = string_itoa(pid);
                pthread_mutex_lock(&mutex_procesos);
                t_contexto* ctx_guardado = dictionary_get(procesos, key);
                
                if (ctx_guardado != NULL) {
                    // copio los registros nuevos sobre el contexto viejo sin tocar su t_list de instrucciones
                    ctx_guardado->PC = contexto_cpu->PC;
                    ctx_guardado->AX = contexto_cpu->AX; ctx_guardado->BX = contexto_cpu->BX;
                    ctx_guardado->CX = contexto_cpu->CX; ctx_guardado->DX = contexto_cpu->DX;
                    ctx_guardado->EAX = contexto_cpu->EAX; ctx_guardado->EBX = contexto_cpu->EBX;
                    ctx_guardado->ECX = contexto_cpu->ECX; ctx_guardado->EDX = contexto_cpu->EDX;
                    ctx_guardado->SI = contexto_cpu->SI; ctx_guardado->DI = contexto_cpu->DI;
                }
                pthread_mutex_unlock(&mutex_procesos);
                free(key);

                log_info(logger, "## Contexto actualizado del PID: %d (Nuevo PC: %d)", pid, contexto_cpu->PC);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case LEER: { 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_logica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);

                int dir_fisica =  traducir_direccion_logica(pid, dir_logica, tamanio);
                
                if (dir_fisica == -1 ){
                    EnviarOpCode(socket_cliente,SEGMENTATION_FAULT);
                }  else  {

                log_info(logger, "## PID: %d Lectura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio);
                
                t_memory_stick* stick = buscar_stick_por_direccion(dir_fisica);
                    if (stick) {
                        uint32_t offset = dir_fisica - stick->base_global;
                        t_paquete* p = CrearPaquete(LEER);
                        AgregarAPaquete(p, &offset, sizeof(uint32_t));
                        AgregarAPaquete(p, &tamanio, sizeof(uint32_t));
                        EnviarPaquete(p, stick->socket);
                        EliminarPaquete(p);

                        if (RecibirOperacion(stick->socket) == LEER) {
                            t_list* resp = RecibirPaquete(stick->socket);
                            t_paquete* res_final = CrearPaquete(LEER);
                            AgregarAPaquete(res_final, list_get(resp, 0), tamanio);
                            EnviarPaquete(res_final, socket_cliente);
                            EliminarPaquete(res_final);
                            list_destroy_and_destroy_elements(resp, free);
                        } else manejo_de_memoria_corrupta();
                    } else manejo_de_memoria_corrupta();
                }
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case ESCRIBIR: { 
                t_list* valores = RecibirPaquete(socket_cliente);
                uint32_t pid = *(uint32_t*)list_get(valores, 0);
                uint32_t dir_logica = *(uint32_t*)list_get(valores, 1);
                uint32_t tamanio = *(uint32_t*)list_get(valores, 2);
                void* datos = list_get(valores, 3);

                uint32_t dir_fisica = traducir_direccion_logica(pid, dir_logica, tamanio);
                if(dir_fisica == -1){
                    EnviarOpCode(socket_cliente, SEGMENTATION_FAULT);
                }else{log_info(logger, "## PID: %d Escritura Dir. Física: %d Tamaño: %d", pid, dir_fisica, tamanio); 
                
                t_memory_stick* stick = buscar_stick_por_direccion(dir_fisica);
                    if (stick) {
                        uint32_t offset = dir_fisica - stick->base_global;
                        t_paquete* p = CrearPaquete(ESCRIBIR);
                        AgregarAPaquete(p, &offset, sizeof(uint32_t));
                        AgregarAPaquete(p, &tamanio, sizeof(uint32_t));
                        AgregarAPaquete(p, datos, tamanio);
                        EnviarPaquete(p, stick->socket);
                        EliminarPaquete(p);

                        if (RecibirOperacion(stick->socket) == MENSAJE) EnviarOpCode(socket_cliente, MENSAJE);
                        else manejo_de_memoria_corrupta();
                    } else manejo_de_memoria_corrupta();
                }
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case CONSULTAR_ESPACIO: { 
                
                t_list* valores = RecibirPaquete(socket_cliente);

                int espacio_libre_total = 0;

                pthread_mutex_lock(&mutex_huecos);
                for(int i = 0; i < list_size(lista_huecos_libres); i++){
                    t_hueco_libre* h = list_get(lista_huecos_libres, i);
                    espacio_libre_total += h->tamanio; 
                }   
                pthread_mutex_unlock(&mutex_huecos);

                t_paquete* res = CrearPaquete(CONSULTAR_ESPACIO);
                AgregarAPaquete(res, &espacio_libre_total, sizeof(uint32_t));
                EnviarPaquete(res, socket_cliente);
                EliminarPaquete(res);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case CREAR_SEGMENTO:{
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
                    nuevo_segmento->bloques_swap = list_create();

                    char* key = string_itoa(pid);
                    pthread_mutex_lock(&mutex_tabla_segmentos);
                    t_list* tabla = dictionary_get(tabla_segmentos, key);
                    
                    if(tabla != NULL){
                        list_add(tabla, nuevo_segmento);
                    } else{
                        log_error(logger, "CRÍTICO: No se encontró la tabla de segmentos para el PID %d", pid);
                        free(nuevo_segmento);
                    }
                    
                    pthread_mutex_unlock(&mutex_tabla_segmentos);
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
                    int espacio_libre_total = 0;
                    for(int i = 0; i < list_size(lista_huecos_libres); i++){
                        t_hueco_libre* h = list_get(lista_huecos_libres, i);
                        espacio_libre_total += h->tamanio;
                    }

                    if(espacio_libre_total >= tamanio_pedido){
                        EnviarOpCode(socket_kernel_scheduler_eventos, NECESITA_COMPACTACION);
                        log_info(logger, "## PID: %d - Requiere comparctación para para segmento de %d bytes. Solicitud enviada al Scheduler.", pid, tamanio_pedido);
                    } else{
                        EnviarOpCode(socket_cliente, ESPACIO_INSUFICIENTE);
                        log_warning(logger, "## PID: %d - Memoria Insuficiente para segmento de %d  bytes.", pid, tamanio_pedido);

                    }
                }
                
                pthread_mutex_unlock(&mutex_huecos);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            case SUSPENDER_PROCESO:{
                t_list* valores =  RecibirPaquete(socket_cliente);
                uint32_t pid_a_suspender = *(uint32_t*) list_get(valores, 0);

                log_info(logger,"## PID: %d - Solicitud de Suspensión recibida", pid_a_suspender);

                suspender_proceso(pid_a_suspender);

                EnviarOpCode(socket_cliente, MENSAJE);

                list_destroy_and_destroy_elements(valores,free);
                break;
            }
            case DESUSPENDER_PROCESO:{
                t_list* valores =  RecibirPaquete(socket_cliente);
                uint32_t pid_a_desuspender = *(uint32_t*) list_get(valores, 0);
                
                desuspender_proceso(pid_a_desuspender, socket_cliente);
                EnviarOpCode(socket_cliente, MENSAJE);

                list_destroy_and_destroy_elements(valores,free);
                break;

            } 
            case COMPACTAR_MEMORIA:{
                usleep(config_get_int_value(config,"COMPACTION_DELAY") * 1000);

                compactar_memoria();
                EnviarOpCode(socket_cliente, MENSAJE);
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

int main(int argc, char *argv[])
{

    saludar("kernel_memory");
    ObtenerConfig();

    procesos = dictionary_create();
    pthread_mutex_init(&mutex_procesos, NULL);
    
    tabla_segmentos = dictionary_create();
    pthread_mutex_init(&mutex_tabla_segmentos, NULL);

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

    free(allocation_strategy);

    if (bitmap_swap != NULL) bitarray_destroy(bitmap_swap);
    
    log_destroy(logger);
    return 0;
}