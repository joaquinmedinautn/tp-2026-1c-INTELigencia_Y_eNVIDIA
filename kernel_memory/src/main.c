#include "kernel_memory.h"

t_config *config;
t_log_level log_level;
t_log *logger;

char *scripts_path;
int socket_servidor;
int puerto_escucha;
int socket_kernel_scheduler;
int socket_memory_stick;
int socket_cpu = -1;
int socket_swap;
int socket_misterioso;
int instruction_delay;
int socket_cpu_notif = -1;
int compaction_delay;

int segment_max_size;
char *allocation_strategy;

int socket_kernel_scheduler_eventos = -1;

int swap_block_size;
int swap_file_size;
int cant_bloques_swap;
t_bitarray *bitmap_swap;
pthread_mutex_t mutex_bitmap_swap;

t_list *lista_huecos_libres;
pthread_mutex_t mutex_huecos;

t_dictionary *tabla_segmentos;
pthread_mutex_t mutex_tabla_segmentos;

t_dictionary *procesos;
pthread_mutex_t mutex_procesos;

t_list *lista_memory_sticks;
pthread_mutex_t mutex_memory_sticks;

t_list *segmentos_a_compactar;

void manejo_de_memoria_corrupta() {
  log_error(logger, "Un Memory Stick falló");
  if (socket_kernel_scheduler_eventos != -1) {
    EnviarOpCode(socket_kernel_scheduler_eventos, MEMORIA_CORRUPTA);
  }
}

t_memory_stick *buscar_stick_por_direccion(uint32_t dir_fisica) {
  pthread_mutex_lock(&mutex_memory_sticks);
  for (int i = 0; i < list_size(lista_memory_sticks); i++) {
    t_memory_stick *stick = list_get(lista_memory_sticks, i);
    if (dir_fisica >= stick->base_global && dir_fisica < stick->limite) {
      pthread_mutex_unlock(&mutex_memory_sticks);
      return stick;
    }
  }
  pthread_mutex_unlock(&mutex_memory_sticks);
  return NULL;
}

bool ordenar_huecos_por_base(void *a, void *b) {
  t_hueco_libre *h1 = (t_hueco_libre *)a;
  t_hueco_libre *h2 = (t_hueco_libre *)b;
  return h1->base < h2->base;
}

void consolidar_huecos_libres() {
  list_sort(lista_huecos_libres, ordenar_huecos_por_base);

  for (int i = 0; i < list_size(lista_huecos_libres) - 1; i++) {
    t_hueco_libre *h_actual = list_get(lista_huecos_libres, i);
    t_hueco_libre *h_sig = list_get(lista_huecos_libres, i + 1);

    if ((h_actual->base + h_actual->tamanio) == h_sig->base) {
      h_actual->tamanio += h_sig->tamanio;
      list_remove(lista_huecos_libres, i + 1);
      free(h_sig);
      i--;
    }
  }
}

t_hueco_libre *buscar_hueco_libre(uint32_t tamanio_pedido) {
  t_hueco_libre *hueco_seleccionado = NULL;

  for (int i = 0; i < list_size(lista_huecos_libres); i++) {
    t_hueco_libre *hueco = list_get(lista_huecos_libres, i);

    if (hueco->tamanio >= tamanio_pedido) {
      if (hueco_seleccionado == NULL) {
        hueco_seleccionado = hueco;
      } else {
        if (strcmp(allocation_strategy, "BEST") == 0) {
          if (hueco->tamanio < hueco_seleccionado->tamanio)
            hueco_seleccionado = hueco;
        } else if (strcmp(allocation_strategy, "WORST") == 0) {
          if (hueco->tamanio > hueco_seleccionado->tamanio)
            hueco_seleccionado = hueco;
        }
      }
    }
  }
  if (hueco_seleccionado == NULL) {
    log_warning(logger, "¡No se encontró hueco adecuado para %d bytes!",
                tamanio_pedido);
  }
  return hueco_seleccionado;
}

void liberar_memoria_proceso(uint32_t pid) {
  char *key = string_itoa(pid);
  pthread_mutex_lock(&mutex_tabla_segmentos);
  t_list *tabla = dictionary_remove(tabla_segmentos, key);
  pthread_mutex_unlock(&mutex_tabla_segmentos);
  free(key);

  if (tabla != NULL) {
    pthread_mutex_lock(&mutex_huecos);
    for (int i = 0; i < list_size(tabla); i++) {
      t_segmento *seg = list_get(tabla, i);

      // devuelvo espacio a los  huecos libres
      if (seg->presente) {
        t_hueco_libre *nuevo_hueco = malloc(sizeof(t_hueco_libre));
        nuevo_hueco->base = seg->base;
        nuevo_hueco->tamanio = seg->tamanio;
        list_add(lista_huecos_libres, nuevo_hueco);
      }

      if (seg->bloques_swap != NULL) {
        // devolver los bloques al bitmap del SWAP
        pthread_mutex_lock(&mutex_bitmap_swap);
        for (int j = 0; j < list_size(seg->bloques_swap); j++) {
          int nro_bloque = *(int *)list_get(seg->bloques_swap, j);
          bitarray_clean_bit(bitmap_swap, nro_bloque);
        }
        pthread_mutex_unlock(&mutex_bitmap_swap);

        list_destroy_and_destroy_elements(seg->bloques_swap, free);
      }
      free(seg);
    }
    consolidar_huecos_libres();
    pthread_mutex_unlock(&mutex_huecos);
    list_destroy(tabla);
  }
}

void InicarEstructurasMemoria() {

  lista_huecos_libres = list_create();
  pthread_mutex_init(&mutex_huecos, NULL);

  lista_memory_sticks = list_create();
  pthread_mutex_init(&mutex_memory_sticks, NULL);

  log_info(logger,
           "## Estructuras de Memoria iniciadas. Esperando Memory Sticks.");
}

void ObtenerConfig(char *path) {
  config = config_create(path);
  if (config == NULL) {
    perror("No se pudo crear el config");
    exit(EXIT_FAILURE);
  }
  puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
  instruction_delay = config_get_int_value(config, "INSTRUCTION_DELAY");
  scripts_path =
      string_duplicate(config_get_string_value(config, "SCRIPTS_BASEPATH"));

  segment_max_size = config_get_int_value(config, "SEGMENT_MAX_SIZE");
  allocation_strategy =
      string_duplicate(config_get_string_value(config, "ALLOCATION_STRATEGY"));
  compaction_delay = config_get_int_value(config, "COMPACTION_DELAY");

  log_level =
      log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
  logger = log_create("kernel_memory.log", "KERNEL_MEMORY", true, log_level);
  // config_destroy(config);
}

void suspender_proceso(uint32_t pid) {
  char *key = string_itoa(pid);
  pthread_mutex_lock(&mutex_tabla_segmentos);
  t_list *tabla = dictionary_get(tabla_segmentos, key);
  pthread_mutex_unlock(&mutex_tabla_segmentos);
  free(key);

  if (tabla == NULL)
    return;

  for (int i = 0; i < list_size(tabla); i++) {
    t_segmento *seg = list_get(tabla, i);

    if (seg->presente) {
      // calculo cuántos bloques de SWAP necesitamos para este segmento
      int bloques_necesarios =
          (seg->tamanio + swap_block_size - 1) / swap_block_size;

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
          log_error(logger,
                    "CRÍTICO: No hay espacio en SWAP para suspender PID %d",
                    pid);

          pthread_mutex_unlock(&mutex_bitmap_swap);
          return;
        }

        // anoto el bloque en la lista del segmento
        int *num_bloque = malloc(sizeof(int));
        *num_bloque = bloque_libre;
        list_add(seg->bloques_swap, num_bloque);

        // calculo cuánto vamos a enviar en este bloque
        int bytes_a_enviar = swap_block_size;
        if (offset_segmento + swap_block_size > seg->tamanio) {
          bytes_a_enviar = seg->tamanio - offset_segmento;
        }

        uint32_t dir_fisica_leer = seg->base + offset_segmento;
        t_memory_stick *stick = buscar_stick_por_direccion(dir_fisica_leer);

        // leo de RAM y mandar al SWAP ESCRITURA_BLOQUE
        void *buffer_bloque = malloc(swap_block_size);
        memset(buffer_bloque, 0,
               swap_block_size); // lleno con ceros por las dudas

        if (stick != NULL) {
          uint32_t offset_local = dir_fisica_leer - stick->base_global;
          t_paquete *paq_leer = CrearPaquete(LEER);
          AgregarAPaquete(paq_leer, &offset_local, sizeof(uint32_t));
          AgregarAPaquete(paq_leer, &bytes_a_enviar, sizeof(uint32_t));
          EnviarPaquete(paq_leer, stick->socket);
          EliminarPaquete(paq_leer);

          if (RecibirOperacion(stick->socket) == LEER) {
            t_list *resp = RecibirPaquete(stick->socket);
            memcpy(buffer_bloque, list_get(resp, 0), bytes_a_enviar);
            list_clean_and_destroy_elements(resp, free);
          } else {
            manejo_de_memoria_corrupta();
          }
        } else {
          manejo_de_memoria_corrupta();
        }

        t_paquete *paq_swap = CrearPaquete(ESCRITURA_BLOQUE);
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
      t_hueco_libre *nuevo_hueco = malloc(sizeof(t_hueco_libre));
      nuevo_hueco->base = seg->base;
      nuevo_hueco->tamanio = seg->tamanio;
      list_add(lista_huecos_libres, nuevo_hueco);
      consolidar_huecos_libres();
      pthread_mutex_unlock(&mutex_huecos);

      seg->presente = false;

      log_info(logger, "## PID: %d - Segmento %d movido a SWAP", pid,
               seg->id_segmento);
    }
  }
}

bool desuspender_proceso(uint32_t pid, int socket_cliente) {
  char *key = string_itoa(pid);
  pthread_mutex_lock(&mutex_tabla_segmentos);
  t_list *tabla = dictionary_get(tabla_segmentos, key);
  pthread_mutex_unlock(&mutex_tabla_segmentos);
  free(key);

  if (tabla == NULL)
    return false;

  for (int i = 0; i < list_size(tabla); i++) {
    t_segmento *seg = list_get(tabla, i);

    // proceso solo los que están en SWAP
    if (!seg->presente) {
      pthread_mutex_lock(&mutex_huecos);
      t_hueco_libre *hueco = buscar_hueco_libre(seg->tamanio);

      if (hueco == NULL) {
        // El Scheduler se supone que verificó el espacio antes, pero por las
        // dudas:
        log_error(logger,
                  "No hay hueco para des-suspender el segmento %d del PID %d",
                  seg->id_segmento, pid);
        pthread_mutex_unlock(&mutex_huecos);
        return false;
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
        int *num_bloque = list_get(seg->bloques_swap, b);

        // Pido el bloque al SWAP
        t_paquete *paq_lectura = CrearPaquete(LECTURA_BLOQUE);
        AgregarAPaquete(paq_lectura, num_bloque, sizeof(int));
        EnviarPaquete(paq_lectura, socket_swap);
        EliminarPaquete(paq_lectura);

        // recibo el contenido del bloque
        int op_recibida = RecibirOperacion(socket_swap);
        if (op_recibida == LECTURA_BLOQUE) {
          t_list *valores_bloque = RecibirPaquete(socket_swap);
          void *datos_bloque = list_get(valores_bloque, 0);

          int bytes_a_copiar = swap_block_size;
          if (offset_segmento + swap_block_size > seg->tamanio) {
            bytes_a_copiar = seg->tamanio - offset_segmento;
          }

          uint32_t dir_fisica_escribir = seg->base + offset_segmento;
          t_memory_stick *stick =
              buscar_stick_por_direccion(dir_fisica_escribir);

          if (stick != NULL) {
            uint32_t offset_local = dir_fisica_escribir - stick->base_global;
            t_paquete *paq_escribir = CrearPaquete(ESCRIBIR);
            AgregarAPaquete(paq_escribir, &offset_local, sizeof(uint32_t));
            AgregarAPaquete(paq_escribir, &bytes_a_copiar, sizeof(uint32_t));
            AgregarAPaquete(paq_escribir, datos_bloque, bytes_a_copiar);
            EnviarPaquete(paq_escribir, stick->socket);
            EliminarPaquete(paq_escribir);

            if (RecibirOperacion(stick->socket) != MENSAJE) {
              manejo_de_memoria_corrupta();
            } else {
              log_info(logger,
                       "## Segmento %d (offset %d): Copiados %d bytes del Swap "
                       "al Stick",
                       seg->id_segmento, offset_segmento, bytes_a_copiar);
            }
          } else {
            manejo_de_memoria_corrupta();
          }

          offset_segmento += bytes_a_copiar;
          list_destroy(valores_bloque);
        }

        // libero el bloque en el bitmap del SWAP
        bitarray_clean_bit(bitmap_swap, *num_bloque);
      }
      pthread_mutex_unlock(&mutex_bitmap_swap);

      // limpio la lista de bloques del segmento porque ya no está en SWAP
      list_clean_and_destroy_elements(seg->bloques_swap, free);
      seg->presente = true;

      log_info(logger, "## PID: %d - Segmento %d restaurado en RAM (Base: %d)",
               pid, seg->id_segmento, seg->base);
    }
  }
  return true;
}

void recolectar_segmentos_presentes(char *key, void *value) {
  t_list *tabla_proc = (t_list *)value;
  for (int i = 0; i < list_size(tabla_proc); i++) {
    t_segmento *seg = list_get(tabla_proc, i);
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
  bool ordenar_segmentos_por_base(void *a, void *b) {
    return ((t_segmento *)a)->base < ((t_segmento *)b)->base;
  }
  list_sort(segmentos_a_compactar, ordenar_segmentos_por_base);

  // empujo todos los segmentos hacia el principio (Base 0)
  uint32_t desplazamiento_actual = 0;

  for (int i = 0; i < list_size(segmentos_a_compactar); i++) {
    t_segmento *seg = list_get(segmentos_a_compactar, i);

    if (seg->base != desplazamiento_actual) {
      t_memory_stick *stick_origen = buscar_stick_por_direccion(seg->base);

      if (stick_origen == NULL) {
        log_error(logger,
                  "## Compactación abortada: base %d del segmento %d (PID %d) "
                  "fuera de todo stick",
                  seg->base, seg->id_segmento, seg->pid);
        break;
      }

      uint32_t offset_origen = seg->base - stick_origen->base_global;

      t_paquete *p_leer = CrearPaquete(LEER);
      AgregarAPaquete(p_leer, &offset_origen, sizeof(uint32_t));
      AgregarAPaquete(p_leer, &seg->tamanio, sizeof(uint32_t));
      EnviarPaquete(p_leer, stick_origen->socket);
      EliminarPaquete(p_leer);

      RecibirOperacion(stick_origen->socket);
      t_list *resp = RecibirPaquete(stick_origen->socket);
      void *buffer_temp = list_get(resp, 0);

      t_memory_stick *stick_destino =
          buscar_stick_por_direccion(desplazamiento_actual);

      if (stick_destino == NULL) {
        log_error(logger,
                  "## Compactación abortada: destino %d fuera de todo stick "
                  "(huecos inconsistentes)",
                  desplazamiento_actual);
        list_clean_and_destroy_elements(
            resp, free); // liberar lo ya leído del stick origen
        break;
      }

      uint32_t offset_destino =
          desplazamiento_actual - stick_destino->base_global;

      t_paquete *p_escribir = CrearPaquete(ESCRIBIR);
      AgregarAPaquete(p_escribir, &offset_destino, sizeof(uint32_t));
      AgregarAPaquete(p_escribir, &seg->tamanio, sizeof(uint32_t));
      AgregarAPaquete(p_escribir, buffer_temp, seg->tamanio);
      EnviarPaquete(p_escribir, stick_destino->socket);
      EliminarPaquete(p_escribir);

      RecibirOperacion(stick_destino->socket);

      list_clean_and_destroy_elements(resp, free);
      seg->base = desplazamiento_actual;
    }

    log_info(logger, "## PID: %d - Segmento: %d - Base: %d - Tamaño: %d",
             seg->pid, seg->id_segmento, seg->base, seg->tamanio);

    desplazamiento_actual += seg->tamanio;
  }
  list_destroy(segmentos_a_compactar);

  // destruyo la vieja lista de huecos y crear uno solo al final
  pthread_mutex_lock(&mutex_huecos);
  list_clean_and_destroy_elements(lista_huecos_libres, free);

  uint32_t ram_total = 0;
  for (int i = 0; i < list_size(lista_memory_sticks); i++) {
    ram_total += ((t_memory_stick *)list_get(lista_memory_sticks, i))->tamanio;
  }
  if (ram_total > desplazamiento_actual) {
    t_hueco_libre *hueco_final = malloc(sizeof(t_hueco_libre));
    hueco_final->base = desplazamiento_actual;
    hueco_final->tamanio = ram_total - desplazamiento_actual;
    list_add(lista_huecos_libres, hueco_final);
    log_info(
        logger,
        "## Compactación finalizada. Hueco libre restante: %d bytes en Base %d",
        hueco_final->tamanio, hueco_final->base);
  }
  pthread_mutex_unlock(&mutex_huecos);
}

void registrar_nuevo_memory_stick(int tamanio_nuevo) {

  EnviarOpCode(socket_kernel_scheduler_eventos, NUEVO_MEMORY_STICK);
  log_info(logger, "Notificando nuevo Memory Stick al Scheduler");
}

void manejar_desconexion_memory_stick() {

  EnviarOpCode(socket_kernel_scheduler_eventos, MEMORIA_CORRUPTA);
  log_error(logger, "CRÍTICO: Memory Stick desconectado. Notificando "
                    "corrupción al Scheduler");
}

int traducir_direccion_logica(uint32_t pid, uint32_t dir_logica,
                              uint32_t *dir_fisica) {
  // Usamos la misma matemática que implementaste en la CPU
  uint32_t num_segmento = dir_logica / segment_max_size;
  uint32_t desplazamiento = dir_logica % segment_max_size;

  char *key = string_itoa(pid);
  pthread_mutex_lock(&mutex_tabla_segmentos);
  t_list *tabla = dictionary_get(tabla_segmentos, key);
  free(key);

  if (tabla == NULL) {
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    return -1;
  }

  t_segmento *segmento_encontrado = NULL;
  for (int i = 0; i < list_size(tabla); i++) {
    t_segmento *seg = list_get(tabla, i);
    // Buscamos por el id real, no por el índice de la lista
    if (seg->id_segmento == num_segmento) {
      segmento_encontrado = seg;
      break;
    }
  }

  if (segmento_encontrado == NULL || !segmento_encontrado->presente) {
    pthread_mutex_unlock(&mutex_tabla_segmentos);
    return -1; // Segmento no existe o está en SWAP
  }

  *dir_fisica = segmento_encontrado->base + desplazamiento;
  pthread_mutex_unlock(&mutex_tabla_segmentos);
  return 0;
}

// ejecuto cada hilo
void atender_cliente(void *void_socket) {
  int socket_cliente = *(int *)void_socket;
  free(void_socket);

  int cod_op = RecibirOperacion(socket_cliente);

  switch (cod_op) {
  case KERNEL_SCHEDULER:
    log_info(logger, "## Kernel Scheduler Conectado");
    break;
  case CPU_A_KERNEL_MEMORY:
    socket_cpu = socket_cliente;
    log_info(logger, "## CPU Conectada");

    pthread_mutex_lock(&mutex_memory_sticks);
    int cant_sticks = list_size(lista_memory_sticks);

    t_paquete *paq_lista = CrearPaquete(NUEVO_MEMORY_STICK);
    AgregarAPaquete(paq_lista, &segment_max_size, sizeof(int));
    AgregarAPaquete(paq_lista, &cant_sticks, sizeof(int));

    for (int i = 0; i < cant_sticks; i++) {
      t_memory_stick *s = list_get(lista_memory_sticks, i);
      AgregarAPaquete(paq_lista, &s->tamanio, sizeof(uint32_t));
      AgregarAPaquete(paq_lista, &s->base_global, sizeof(uint32_t));
      AgregarAPaquete(paq_lista, &s->puerto, sizeof(int));
      AgregarAPaquete(paq_lista, s->ip, strlen(s->ip) + 1);
    }

    pthread_mutex_unlock(&mutex_memory_sticks);

    EnviarPaquete(paq_lista, socket_cpu);
    EliminarPaquete(paq_lista);
    break;
  case CPU_A_KERNEL_MEMORY_NOTIFICACIONES:
    socket_cpu_notif = socket_cliente;
    log_info(logger, "## CPU Conectada (Canal Asincrónico de Notificaciones)");
    break;
  case SWAP: {
    socket_swap = socket_cliente;

    t_list *valores_swap = RecibirPaquete(socket_cliente);
    swap_block_size = *(int *)list_get(valores_swap, 0);
    swap_file_size = *(int *)list_get(valores_swap, 1);

    cant_bloques_swap = swap_file_size / swap_block_size;

    int bytes_bitarray_swap = cant_bloques_swap / 8;
    if (cant_bloques_swap % 8 != 0)
      bytes_bitarray_swap++;

    void *espacio_bitarray_swap = malloc(bytes_bitarray_swap);
    memset(espacio_bitarray_swap, 0, bytes_bitarray_swap);

    bitmap_swap = bitarray_create_with_mode(espacio_bitarray_swap,
                                            cant_bloques_swap, LSB_FIRST);
    pthread_mutex_init(&mutex_bitmap_swap, NULL);

    log_info(logger, "## SWAP Conectado - Bloques: %d, Tamaño total: %d bytes",
             cant_bloques_swap, swap_file_size);

    list_destroy_and_destroy_elements(valores_swap, free);
    return;
  }
  case NUEVO_MEMORY_STICK: {
    t_list *valores = RecibirPaquete(socket_cliente);
    uint32_t tamanio_stick = *(uint32_t *)list_get(valores, 0);
    int puerto_stick = *(int *)list_get(valores, 1);
    char *ip_stick = list_get(valores, 2);

    pthread_mutex_lock(&mutex_memory_sticks);
    uint32_t base_nueva = 0;
    if (!list_is_empty(lista_memory_sticks)) {
      t_memory_stick *ultimo =
          list_get(lista_memory_sticks, list_size(lista_memory_sticks) - 1);
      base_nueva = ultimo->limite;
    }

    t_memory_stick *nuevo_stick = malloc(sizeof(t_memory_stick));
    nuevo_stick->socket = socket_cliente;
    nuevo_stick->tamanio = tamanio_stick;
    nuevo_stick->base_global = base_nueva;
    nuevo_stick->limite = base_nueva + tamanio_stick;
    nuevo_stick->puerto = puerto_stick;
    nuevo_stick->ip = string_duplicate(ip_stick);
    list_add(lista_memory_sticks, nuevo_stick);
    pthread_mutex_unlock(&mutex_memory_sticks);

    
    pthread_mutex_lock(&mutex_huecos);
    // vacio la lista vieja para no fragmentar en 4 partes independientes
    list_clean_and_destroy_elements(lista_huecos_libres, free);

    // calculo el tamaño total de la RAM unificada hasta el momento
    uint32_t ram_total_acumulada = base_nueva + tamanio_stick;

    // creo UN SOLO hueco libre gigante que represente la cinta continua de RAM
    t_hueco_libre *hueco_unico = malloc(sizeof(t_hueco_libre));
    hueco_unico->base = 0; // la RAM vacía arranca obligatoriamente en 0
    hueco_unico->tamanio = ram_total_acumulada; // abarca toda la unión contigua
    list_add(lista_huecos_libres, hueco_unico);
    pthread_mutex_unlock(&mutex_huecos);

    log_info(logger, "## Memory Stick de %d bytes mapeado en base %d",
             tamanio_stick, base_nueva);

    if (socket_cpu_notif != -1) {
      t_paquete *paq_cpu = CrearPaquete(NUEVO_MEMORY_STICK);
      AgregarAPaquete(paq_cpu, &tamanio_stick, sizeof(uint32_t));
      AgregarAPaquete(paq_cpu, &base_nueva, sizeof(uint32_t));
      AgregarAPaquete(paq_cpu, &puerto_stick, sizeof(int));
      AgregarAPaquete(paq_cpu, ip_stick, strlen(ip_stick) + 1);
      EnviarPaquete(paq_cpu, socket_cpu_notif);
      EliminarPaquete(paq_cpu);
      log_info(logger, "## Notificación de nuevo hardware enviada a la CPU");
    } else {
      log_warning(logger, "## Se conectó un Memory Stick pero la CPU no "
                          "abrió su canal de notificaciones");
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

      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      char *path = list_get(valores, 1);

      cargar_instrucciones(pid, path);

      // creo tabla de paginas vacia
      t_list *nueva_tabla = list_create();
      char *key = string_itoa(pid);
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
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      uint32_t pc = *(uint32_t *)list_get(valores, 1);

      char *instruccion = obtener_instruccion(pid, pc);

      usleep(instruction_delay * 1000); // latencia obligatoria

      if (instruccion == NULL) {
        instruccion = strdup("EXIT");
      } else {
        instruccion = strdup(instruccion);
      }

      log_info(logger, "## PID: %d %d Instrucción: %s", pid, pc, instruccion);

      // envio el string a la CPU
      t_paquete *res = CrearPaquete(SOLICITUD_INSTRUCCION);
      AgregarAPaquete(res, instruccion, strlen(instruccion) + 1);
      EnviarPaquete(res, socket_cliente);
      EliminarPaquete(res);

      free(instruccion);

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case SOLICITUD_CONTEXTO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);

      char *key = string_itoa(pid);
      pthread_mutex_lock(&mutex_procesos);
      t_contexto *ctx = dictionary_get(procesos, key);
      pthread_mutex_unlock(&mutex_procesos);
      free(key);

      if (ctx != NULL) {
        t_paquete *res = CrearPaquete(SOLICITUD_CONTEXTO);

        AgregarAPaquete(res, &ctx->PC, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->EAX, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->EBX, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->ECX, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->EDX, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->SI, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->DI, sizeof(uint32_t));
        AgregarAPaquete(res, &ctx->AX, sizeof(uint8_t));
        AgregarAPaquete(res, &ctx->BX, sizeof(uint8_t));
        AgregarAPaquete(res, &ctx->CX, sizeof(uint8_t));
        AgregarAPaquete(res, &ctx->DX, sizeof(uint8_t));

        int cant_segmentos = 0;
        char *key_seg = string_itoa(pid);
        pthread_mutex_lock(&mutex_tabla_segmentos);
        t_list *tabla = dictionary_get(tabla_segmentos, key_seg);
        if (tabla != NULL) {
          cant_segmentos = list_size(tabla);
        }
        AgregarAPaquete(res, &cant_segmentos, sizeof(int));

        for (int i = 0; i < cant_segmentos; i++) {
          t_segmento *seg = list_get(tabla, i);
          AgregarAPaquete(res, seg, sizeof(t_segmento));
        }

        pthread_mutex_unlock(&mutex_tabla_segmentos);
        free(key_seg);

        EnviarPaquete(res, socket_cliente);
        EliminarPaquete(res);
        log_info(logger, "## Contexto enviado a CPU para PID: %d (PC: %d)", pid,
                 ctx->PC);
      } else {
        log_error(logger,
                  "Error: CPU solicitó contexto para un PID inexistente: %d",
                  pid);
      }

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case ELIMINAR_SEGMENTO: {
      // 1. Deserializamos el paquete enviado por el Scheduler
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      uint32_t id_segmento = *(uint32_t *)list_get(valores, 1);

      char *key = string_itoa(pid);

      // 2. Buscamos la tabla de segmentos del proceso
      pthread_mutex_lock(&mutex_tabla_segmentos);
      t_list *tabla = dictionary_get(tabla_segmentos, key);
      pthread_mutex_unlock(&mutex_tabla_segmentos);
      free(key);

      if (tabla != NULL) {
        // 3. Buscamos el segmento específico dentro de su tabla
        t_segmento *seg_a_eliminar = NULL;
        int idx_seg = -1;

        for (int i = 0; i < list_size(tabla); i++) {
          t_segmento *seg = list_get(tabla, i);
          if (seg->id_segmento == id_segmento) {
            seg_a_eliminar = seg;
            idx_seg = i;
            break;
          }
        }

        if (seg_a_eliminar != NULL) {
          // 4. Devolvemos el espacio que ocupaba a la lista de huecos libres
          pthread_mutex_lock(&mutex_huecos);

          t_hueco_libre *nuevo_hueco = malloc(sizeof(t_hueco_libre));
          nuevo_hueco->base = seg_a_eliminar->base;
          nuevo_hueco->tamanio = seg_a_eliminar->tamanio;
          list_add(lista_huecos_libres, nuevo_hueco);

          // Consolida huecos vecinos para evitar fragmentación externa
          consolidar_huecos_libres();
          pthread_mutex_unlock(&mutex_huecos);

          // 5. Quitamos el segmento de la tabla del proceso y liberamos su
          // memoria
          list_remove(tabla, idx_seg);

          // Si tenía bloques de swap asignados (en caso de haber sido
          // suspendido antes)
          if (seg_a_eliminar->bloques_swap != NULL) {
            list_destroy_and_destroy_elements(seg_a_eliminar->bloques_swap,
                                              free);
          }

          // Guardamos el tamaño para el log obligatorio antes de hacerle free
          uint32_t tam_log = seg_a_eliminar->tamanio;
          free(seg_a_eliminar);

          // LOG OBLIGATORIO: El enunciado exige este formato exacto para
          // eliminación/creación
          log_info(logger, "## PID: %d Segmento Eliminado %d Tamaño: %d", pid,
                   id_segmento, tam_log);
        } else {
          log_warning(
              logger,
              "## PID: %d - Intento de eliminar segmento %d inexistente", pid,
              id_segmento);
        }
      } else {
        log_error(
            logger,
            "## Error: No se encontró la tabla de segmentos para el PID %d",
            pid);
      }

      // 6. Respondemos confirmación (MENSAJE) al Scheduler para destrabar su
      // mutex
      EnviarOpCode(socket_cliente, MENSAJE);

      // 7. Limpieza del paquete recibido para evitar memory leaks
      list_destroy_and_destroy_elements(valores, free);
      break;
    }

    case CPU_A_KERNEL_MEMORY: { // el que usa la CPU en actualizar_contexto()
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      t_contexto *contexto_cpu = (t_contexto *)list_get(valores, 1);

      char *key = string_itoa(pid);
      pthread_mutex_lock(&mutex_procesos);
      t_contexto *ctx_guardado = dictionary_get(procesos, key);

      if (ctx_guardado != NULL) {
        // copio los registros nuevos sobre el contexto viejo sin tocar su
        // t_list de instrucciones
        ctx_guardado->PC = contexto_cpu->PC;
        ctx_guardado->AX = contexto_cpu->AX;
        ctx_guardado->BX = contexto_cpu->BX;
        ctx_guardado->CX = contexto_cpu->CX;
        ctx_guardado->DX = contexto_cpu->DX;
        ctx_guardado->EAX = contexto_cpu->EAX;
        ctx_guardado->EBX = contexto_cpu->EBX;
        ctx_guardado->ECX = contexto_cpu->ECX;
        ctx_guardado->EDX = contexto_cpu->EDX;
        ctx_guardado->SI = contexto_cpu->SI;
        ctx_guardado->DI = contexto_cpu->DI;
      }
      pthread_mutex_unlock(&mutex_procesos);
      free(key);

      log_info(logger, "## Contexto actualizado del PID: %d (Nuevo PC: %d)",
               pid, contexto_cpu->PC);

      list_destroy_and_destroy_elements(valores, free);
      break;
    }

    case LEER: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      uint32_t dir_fisica_inicio = *(uint32_t *)list_get(
          valores, 1); // ¡Ya viene como física desde la CPU!
      uint32_t tamanio_total = *(uint32_t *)list_get(valores, 2);

      log_info(logger, "## PID: %d Lectura Dir. Física: %d Tamaño: %d", pid,
               dir_fisica_inicio, tamanio_total);

      uint32_t bytes_pendientes = tamanio_total;
      uint32_t dir_fisica_actual = dir_fisica_inicio;
      uint32_t offset_buffer = 0;
      bool error_hardware = false;

      // reservo un búfer temporal único del tamaño total pedido para
      // consolidar los fragmentos
      void *buffer_consolidado = malloc(tamanio_total);

      // bucle para iterar por los distintos Memory Sticks continuos
      while (bytes_pendientes > 0) {
        t_memory_stick *stick = buscar_stick_por_direccion(dir_fisica_actual);
        if (stick == NULL) {
          error_hardware = true;
          break;
        }

        // calculo cuántos bytes quedan en este Stick antes de su frontera
        uint32_t espacio_disponible_stick = stick->limite - dir_fisica_actual;

        // leo solo lo que falta o lo que le quede al chip
        uint32_t bytes_a_leer_aqui =
            (bytes_pendientes < espacio_disponible_stick)
                ? bytes_pendientes
                : espacio_disponible_stick;

        uint32_t offset_local = dir_fisica_actual - stick->base_global;

        // mando la petición de lectura fraccionada al Stick correspondiente
        t_paquete *p = CrearPaquete(LEER);
        AgregarAPaquete(p, &offset_local, sizeof(uint32_t));
        AgregarAPaquete(p, &bytes_a_leer_aqui, sizeof(uint32_t));
        EnviarPaquete(p, stick->socket);
        EliminarPaquete(p);

        // espero la respuesta de este chip de RAM
        if (RecibirOperacion(stick->socket) == LEER) {
          t_list *resp = RecibirPaquete(stick->socket);
          void *fragmento_bytes = list_get(resp, 0);

          // acomodo los bytes leídos en la posición exacta del búfer final
          memcpy((char *)buffer_consolidado + offset_buffer, fragmento_bytes,
                 bytes_a_leer_aqui);

          list_destroy_and_destroy_elements(resp, free);
        } else {
          error_hardware = true;
          break;
        }

        // avanzo el control al siguiente tramo contiguo
        bytes_pendientes -= bytes_a_leer_aqui;
        dir_fisica_actual += bytes_a_leer_aqui;
        offset_buffer += bytes_a_leer_aqui;
      }

      if (error_hardware) {
        free(buffer_consolidado);
        manejo_de_memoria_corrupta();

      } else {
        // le devuelvo a la CPU un único paquete limpio con la data compactada
        t_paquete *res_final = CrearPaquete(LEER);
        AgregarAPaquete(res_final, buffer_consolidado, tamanio_total);
        EnviarPaquete(res_final, socket_cliente);
        EliminarPaquete(res_final);
        free(buffer_consolidado);
      }

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case ESCRIBIR: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      uint32_t dir_fisica_inicio = *(uint32_t *)list_get(
          valores, 1); // ¡Ya viene como física desde la CPU!
      uint32_t tamanio_total = *(uint32_t *)list_get(valores, 2);
      void *datos_totales = list_get(valores, 3);

      log_info(logger, "## PID: %d Escritura Dir. Física: %d Tamaño: %d", pid,
               dir_fisica_inicio, tamanio_total);

      uint32_t bytes_pendientes = tamanio_total;
      uint32_t dir_fisica_actual = dir_fisica_inicio;
      uint32_t offset_buffer = 0;
      bool error_hardware = false;

      // bucle para fragmentar la escritura a través de los chips continuos
      while (bytes_pendientes > 0) {
        t_memory_stick *stick = buscar_stick_por_direccion(dir_fisica_actual);
        if (stick == NULL) {
          error_hardware = true;
          break;
        }

        // calcuilo cuánto espacio le queda a este Stick antes de terminarse
        uint32_t espacio_disponible_stick = stick->limite - dir_fisica_actual;

        // evaluo si el dato entra completo o si se corta en la frontera del
        // Stick
        uint32_t bytes_a_escribir_aqui =
            (bytes_pendientes < espacio_disponible_stick)
                ? bytes_pendientes
                : espacio_disponible_stick;

        uint32_t offset_local = dir_fisica_actual - stick->base_global;

        // extraigo solo el pedazo de datos que le corresponde a este Stick
        void *fragmento_datos = (char *)datos_totales + offset_buffer;

        // mando el paquete por red al chip correspondiente
        t_paquete *p = CrearPaquete(ESCRIBIR);
        AgregarAPaquete(p, &offset_local, sizeof(uint32_t));
        AgregarAPaquete(p, &bytes_a_escribir_aqui, sizeof(uint32_t));
        AgregarAPaquete(p, fragmento_datos, bytes_a_escribir_aqui);
        EnviarPaquete(p, stick->socket);
        EliminarPaquete(p);

        if (RecibirOperacion(stick->socket) != MENSAJE) {
          error_hardware = true;
          break;
        }

        // desplazo los controles al siguiente tramo continuo
        bytes_pendientes -= bytes_a_escribir_aqui;
        dir_fisica_actual += bytes_a_escribir_aqui;
        offset_buffer += bytes_a_escribir_aqui;
      }

      if (error_hardware) {
        manejo_de_memoria_corrupta();
      } else {
        EnviarOpCode(socket_cliente, MENSAJE); // confirmación final a la CPU
      }

      list_destroy_and_destroy_elements(valores, free);
      break;
    }

    case CONSULTAR_ESPACIO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);

      // verificar si los segmentos no-presentes del PID entran en los huecos
      // actuales
      bool entra = true;
      char *key = string_itoa(pid);
      pthread_mutex_lock(&mutex_tabla_segmentos);
      t_list *tabla = dictionary_get(tabla_segmentos, key);
      if (tabla != NULL) {
        pthread_mutex_lock(&mutex_huecos);
        for (int i = 0; i < list_size(tabla); i++) {
          t_segmento *seg = list_get(tabla, i);
          if (!seg->presente && buscar_hueco_libre(seg->tamanio) == NULL) {
            entra = false;
            break;
          }
        }
        pthread_mutex_unlock(&mutex_huecos);
      }
      pthread_mutex_unlock(&mutex_tabla_segmentos);
      free(key);

      EnviarOpCode(socket_cliente,
                   entra ? PROCESO_CARGADO : ESPACIO_INSUFICIENTE);

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case CREAR_SEGMENTO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      uint32_t id_segmento = *(uint32_t *)list_get(valores, 1);
      uint32_t tamanio_pedido = *(uint32_t *)list_get(valores, 2);

      pthread_mutex_lock(&mutex_huecos);
      t_hueco_libre *hueco = buscar_hueco_libre(tamanio_pedido);

      if (hueco != NULL) {
        // hay espacio
        t_segmento *nuevo_segmento = malloc(sizeof(t_segmento));
        nuevo_segmento->pid = pid;
        nuevo_segmento->id_segmento = id_segmento;
        nuevo_segmento->base = hueco->base;
        nuevo_segmento->tamanio = tamanio_pedido;
        nuevo_segmento->presente = true;
        nuevo_segmento->bloques_swap = list_create();

        char *key = string_itoa(pid);
        pthread_mutex_lock(&mutex_tabla_segmentos);
        t_list *tabla = dictionary_get(tabla_segmentos, key);

        if (tabla != NULL) {
          list_add(tabla, nuevo_segmento);
        } else {
          log_error(
              logger,
              "CRÍTICO: No se encontró la tabla de segmentos para el PID %d",
              pid);
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

        log_info(logger, "## PID: %d Segmento Creado %d Tamaño: %d", pid,
                 id_segmento, tamanio_pedido);
        EnviarOpCode(socket_cliente, MENSAJE); // Exito
      } else {
        int espacio_libre_total = 0;
        for (int i = 0; i < list_size(lista_huecos_libres); i++) {
          t_hueco_libre *h = list_get(lista_huecos_libres, i);
          espacio_libre_total += h->tamanio;
        }

        if (espacio_libre_total >= tamanio_pedido) {
          EnviarOpCode(socket_cliente, NECESITA_COMPACTACION);
          EnviarOpCode(socket_kernel_scheduler_eventos, SOLICITUD_COMPACTACION);
          log_info(logger,
                   "## PID: %d - Requiere compactación para segmento de %d "
                   "bytes. Solicitud enviada al Scheduler.",
                   pid, tamanio_pedido);
        } else {
          EnviarOpCode(socket_cliente, ESPACIO_INSUFICIENTE);
          log_warning(
              logger,
              "## PID: %d - Memoria Insuficiente para segmento de %d  bytes.",
              pid, tamanio_pedido);
        }
      }

      pthread_mutex_unlock(&mutex_huecos);
      list_destroy_and_destroy_elements(valores, free);
      break;
    }

    case SUSPENDER_PROCESO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid_a_suspender = *(uint32_t *)list_get(valores, 0);

      log_info(logger, "## PID: %d - Solicitud de Suspensión recibida",
               pid_a_suspender);

      suspender_proceso(pid_a_suspender);

      EnviarOpCode(socket_cliente, MENSAJE);

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case DESUSPENDER_PROCESO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid_a_desuspender = *(uint32_t *)list_get(valores, 0);

      bool ok = desuspender_proceso(pid_a_desuspender, socket_cliente);
      EnviarOpCode(socket_cliente, ok ? MENSAJE : ESPACIO_INSUFICIENTE);

      list_destroy_and_destroy_elements(valores, free);
      break;
    }
    case CPUS_DESALOJADAS: {
      // las CPUs ya están desalojadas: aplicar el retardo y compactar
      usleep(compaction_delay * 1000);

      compactar_memoria();

      // avisar al Scheduler que terminó (por el mismo socket de eventos)
      EnviarOpCode(socket_cliente, FIN_COMPACTACION);
      break;
    }
    case FINALIZAR_PROCESO: {
      t_list *valores = RecibirPaquete(socket_cliente);
      uint32_t pid = *(uint32_t *)list_get(valores, 0);
      char *key = string_itoa(pid);

      pthread_mutex_lock(&mutex_procesos);
      dictionary_remove_and_destroy(procesos, key, eliminar_contexto);
      pthread_mutex_unlock(&mutex_procesos);

      liberar_memoria_proceso(pid);

      log_info(logger, "## PID: %d - Proceso Finalizado (Memoria liberada)",
               pid);
      EnviarOpCode(socket_cliente, MENSAJE);

      free(key);
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

int main(int argc, char *argv[]) {

  saludar("kernel_memory");
  ObtenerConfig(argv[1]);

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
    int *p_socket = malloc(sizeof(int));
    *p_socket = socket_cliente;

    if (pthread_create(&hilo, NULL, (void *)atender_cliente, p_socket) != 0) {
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

  if (bitmap_swap != NULL)
    bitarray_destroy(bitmap_swap);

  log_destroy(logger);
  return 0;
}