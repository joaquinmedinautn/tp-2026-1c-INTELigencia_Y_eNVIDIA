#include "kernel_scheduler.h"

char *path_proceso_inicial;
int socket_servidor;
int puerto_escucha;
t_config *config;
int socket_cpu;
int socket_io;
int socket_kernel_memory;
int socket_km_eventos;
char *ip_kernel_memory;
int puerto_kernel_memory;
int rr_quantum;
bool quantum_activo = false;
char *algoritmo;
int suspension_timeout;
int siguiente_pid = 1;
char *planification_algorithm; // FIFO, RR o CMN
char **queues_algorithms;      // algoritmo de cada cola
int cantidad_colas;
bool queue_preemption;
bool compactacion_en_curso = false;
int cpus_a_esperar = 0;
t_log *logger;
t_log_level log_level;
t_queue *cola_new;
t_queue *cola_block;
t_queue *cola_susp_block;
t_queue *cola_susp_ready;
t_queue **colas_ready;
sem_t sem_new;
sem_t sem_ready;
sem_t sem_cpu_disponible;
sem_t sem_memoria_disponible;
sem_t sem_cpus_desalojadas; // cuenta cuántas CPUs confirmaron el desalojo
pthread_mutex_t mutex_ready;
pthread_mutex_t mutex_new;
pthread_mutex_t mutex_cpus;
pthread_mutex_t mutex_ios;
pthread_mutex_t mutex_procesos;
pthread_mutex_t mutex_block;
pthread_mutex_t mutex_susp;
pthread_mutex_t mutex_kernel_memory;
pthread_mutex_t mutex_quantum;
pthread_mutex_t mutex_lista_mutex;
pthread_mutex_t mutex_pid;
pthread_mutex_t *mutex_colas_ready;
pthread_mutex_t mutex_compactacion;
t_list *lista_cpus;
t_list *lista_ios;
t_list *lista_procesos;
t_list *lista_mutex;

void ObtenerConfig(char *archivo_config)
{
    config = config_create(archivo_config);
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    rr_quantum = config_get_int_value(config, "RR_QUANTUM");
    algoritmo = strdup(config_get_string_value(config, "QUEUES_ALGORITHMS"));
    suspension_timeout = config_get_int_value(config, "SUSPENSION_TIMEOUT");

    planification_algorithm = strdup(config_get_string_value(config, "PLANIFICATION_ALGORITHM"));

    if (strcmp(planification_algorithm, "CMN") == 0)
    {
        char **lista = config_get_array_value(config, "QUEUES_ALGORITHMS");

        cantidad_colas = 0;
        while (lista[cantidad_colas] != NULL)
            cantidad_colas++;

        queues_algorithms = malloc(sizeof(char *) * cantidad_colas);
        for (int i = 0; i < cantidad_colas; i++)
            queues_algorithms[i] = strdup(lista[i]);

        string_array_destroy(lista);

        char *preemption = config_get_string_value(config, "QUEUE_PREEMPTION");
        queue_preemption = (strcmp(preemption, "TRUE") == 0);
    }
    else
    {
        // FIFO o RR: una sola cola, sin preemption
        cantidad_colas = 1;
        queues_algorithms = malloc(sizeof(char *));
        queues_algorithms[0] = strdup(planification_algorithm);
        queue_preemption = false;
    }
}

void IniciarSemaforos()
{
    sem_init(&sem_new, 0, 0);
    sem_init(&sem_ready, 0, 0);
    sem_init(&sem_cpu_disponible, 0, 0); // Inicialmente en 0, se incrementa cuando hay CPUs
    sem_init(&sem_memoria_disponible, 0, 0);
    sem_init(&sem_cpus_desalojadas, 0, 0);
    pthread_mutex_init(&mutex_ready, NULL);
    pthread_mutex_init(&mutex_cpus, NULL);
    pthread_mutex_init(&mutex_ios, NULL);
    pthread_mutex_init(&mutex_procesos, NULL);
    pthread_mutex_init(&mutex_block, NULL);
    pthread_mutex_init(&mutex_susp, NULL);
    pthread_mutex_init(&mutex_kernel_memory, NULL);
    pthread_mutex_init(&mutex_lista_mutex, NULL);
    pthread_mutex_init(&mutex_pid, NULL);
    pthread_mutex_init(&mutex_compactacion, NULL);
}

// Procesar una syscall de la CPU
void ProcesarSyscall(t_cpu_conectada *cpu)
{
    int pid;
    char syscall_nombre[50];
    recv(cpu->socket, &pid, sizeof(int), MSG_WAITALL);
    recv(cpu->socket, syscall_nombre, sizeof(syscall_nombre), MSG_WAITALL);

    log_info(logger, "## (%d) - Solicitó syscall: %s", pid, syscall_nombre);

    proceso *p = BuscarProceso(pid);

    if (strcmp(syscall_nombre, "SLEEP") == 0)
    {
        CambiarEstado(p, BLOCK);
        // lanzar timer de suspensión
        t_args_suspension *args_susp = malloc(sizeof(t_args_suspension));
        args_susp->pid = pid;
        pthread_t hilo_susp;
        pthread_create(&hilo_susp, NULL, HiloSuspension, args_susp);
        pthread_detach(hilo_susp);

        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, p);
        pthread_mutex_unlock(&mutex_block);

        int tiempo;
        recv(cpu->socket, &tiempo, sizeof(int), MSG_WAITALL);
        EnviarAIO("SLEEP", IO_SLEEP, pid, &tiempo);

        cpu->ocupado = false;
        cpu->pid_actual = -1;
        sem_post(&sem_cpu_disponible);
    }
    else if (strcmp(syscall_nombre, "STDIN") == 0)
    {
        CambiarEstado(p, BLOCK);
        // lanzar timer de suspensión
        t_args_suspension *args_susp = malloc(sizeof(t_args_suspension));
        args_susp->pid = pid;
        pthread_t hilo_susp;
        pthread_create(&hilo_susp, NULL, HiloSuspension, args_susp);
        pthread_detach(hilo_susp);

        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, p);
        pthread_mutex_unlock(&mutex_block);

        int direccion_fisica, tamanio;
        recv(cpu->socket, &direccion_fisica, sizeof(int), MSG_WAITALL);
        recv(cpu->socket, &tamanio, sizeof(int), MSG_WAITALL);
        p->dir_fisica_io = direccion_fisica;
        p->tamanio_io = tamanio;

        int params[1] = {tamanio};
        EnviarAIO("STDIN", IO_STDIN_READ, pid, params);

        cpu->ocupado = false;
        cpu->pid_actual = -1;
        sem_post(&sem_cpu_disponible);
    }
    else if (strcmp(syscall_nombre, "STDOUT") == 0)
    {
        CambiarEstado(p, BLOCK);
        // lanzar timer de suspensión
        t_args_suspension *args_susp = malloc(sizeof(t_args_suspension));
        args_susp->pid = pid;
        pthread_t hilo_susp;
        pthread_create(&hilo_susp, NULL, HiloSuspension, args_susp);
        pthread_detach(hilo_susp);

        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, p);
        pthread_mutex_unlock(&mutex_block);

        int direccion_fisica, tamanio;
        recv(cpu->socket, &direccion_fisica, sizeof(int), MSG_WAITALL);
        recv(cpu->socket, &tamanio, sizeof(int), MSG_WAITALL);
        p->dir_fisica_io = direccion_fisica;
        p->tamanio_io = tamanio;

        int params[2] = {direccion_fisica, tamanio};
        EnviarAIO("STDOUT", IO_STDOUT_WRITE, pid, params);

        cpu->ocupado = false;
        cpu->pid_actual = -1;
        sem_post(&sem_cpu_disponible);
    }
    else if (strcmp(syscall_nombre, "MUTEX_CREATE") == 0)
    {
        // no bloquea
        char nombre_mutex[50];
        recv(cpu->socket, nombre_mutex, sizeof(nombre_mutex), MSG_WAITALL);

        // solo crear si no existe
        t_mutex_proceso *m = BuscarMutex(nombre_mutex);
        if (m == NULL)
        {
            m = malloc(sizeof(t_mutex_proceso));
            strcpy(m->nombre, nombre_mutex);
            m->pid_dueño = -1;
            m->cola_espera = queue_create();
            pthread_mutex_init(&m->mutex_syscall, NULL);

            pthread_mutex_lock(&mutex_lista_mutex);
            list_add(lista_mutex, m);
            pthread_mutex_unlock(&mutex_lista_mutex);
        }

        CambiarEstado(p, EXEC);
        cpu->ocupado = true;
        cpu->pid_actual = p->pid;
    }
    else if (strcmp(syscall_nombre, "MUTEX_LOCK") == 0)
    {
        // bloquea solo si el mutex está tomado
        char nombre_mutex[50];
        recv(cpu->socket, nombre_mutex, sizeof(nombre_mutex), MSG_WAITALL);

        t_mutex_proceso *m = BuscarMutex(nombre_mutex);
        if (m == NULL)
        {
            log_error(logger, "Mutex %s no existe", nombre_mutex);
            return;
        }

        pthread_mutex_lock(&m->mutex_syscall);
        if (m->pid_dueño == -1)
        {
            // mutex libre, el proceso lo toma y sigue ejecutando
            m->pid_dueño = pid;
            pthread_mutex_unlock(&m->mutex_syscall);

            log_info(logger, "## (%d) Toma el Mutex %s", pid, nombre_mutex);

            CambiarEstado(p, EXEC);
            cpu->ocupado = true;
            cpu->pid_actual = p->pid;
        }
        else
        {
            // mutex tomado, el proceso queda en BLOCK esperando
            log_info(logger, "## (%d) queda bloqueado esperando Mutex %s", pid, nombre_mutex);
            queue_push(m->cola_espera, p);
            pthread_mutex_unlock(&m->mutex_syscall);

            CambiarEstado(p, BLOCK);
            // lanzar timer de suspensión
            t_args_suspension *args_susp = malloc(sizeof(t_args_suspension));
            args_susp->pid = pid;
            pthread_t hilo_susp;
            pthread_create(&hilo_susp, NULL, HiloSuspension, args_susp);
            pthread_detach(hilo_susp);
            pthread_mutex_lock(&mutex_block);
            queue_push(cola_block, p);
            pthread_mutex_unlock(&mutex_block);
            cpu->ocupado = false;
            cpu->pid_actual = -1;
            sem_post(&sem_cpu_disponible);
        }
    }
    else if (strcmp(syscall_nombre, "MUTEX_UNLOCK") == 0)
    {
        // no bloquea
        char nombre_mutex[50];
        recv(cpu->socket, nombre_mutex, sizeof(nombre_mutex), MSG_WAITALL);

        t_mutex_proceso *m = BuscarMutex(nombre_mutex);
        if (m == NULL)
        {
            log_error(logger, "Mutex %s no existe", nombre_mutex);
            return;
        }

        log_info(logger, "## (%d) Libera el Mutex %s", pid, nombre_mutex);

        // restaurar prioridad heredada si la tenía
        if (p->prioridad_heredada)
        {
            CambiarPrioridadProceso(p, p->prioridad_original);
            p->prioridad_heredada = false;
        }

        pthread_mutex_lock(&m->mutex_syscall);
        if (!queue_is_empty(m->cola_espera))
        {
            // hay procesos esperando, el primero toma el mutex
            proceso *siguiente = queue_pop(m->cola_espera);
            m->pid_dueño = siguiente->pid;
            pthread_mutex_unlock(&m->mutex_syscall);

            log_info(logger, "## (%d) Toma el Mutex %s", siguiente->pid, nombre_mutex);
            CambiarEstado(siguiente, READY);

            pthread_mutex_lock(&mutex_block);
            list_remove_element(cola_block->elements, siguiente);
            pthread_mutex_unlock(&mutex_block);

            EncolarReady(siguiente);
        }
        else
        {
            // nadie esperando, el mutex queda libre
            m->pid_dueño = -1;
            pthread_mutex_unlock(&m->mutex_syscall);
        }

        // el proceso que hizo unlock sigue ejecutando
        CambiarEstado(p, EXEC);
        cpu->ocupado = true;
        cpu->pid_actual = p->pid;
    }
    else if (strcmp(syscall_nombre, "MEM_ALLOC") == 0)
    {
        int id_segmento, tamanio_seg;
        recv(cpu->socket, &id_segmento, sizeof(int), MSG_WAITALL);
        recv(cpu->socket, &tamanio_seg, sizeof(int), MSG_WAITALL);

        pthread_mutex_lock(&mutex_kernel_memory);
        t_paquete *paquete = CrearPaquete(CREAR_SEGMENTO);
        AgregarAPaquete(paquete, &pid, sizeof(int));
        AgregarAPaquete(paquete, &id_segmento, sizeof(int));
        AgregarAPaquete(paquete, &tamanio_seg, sizeof(int));
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        int respuesta = RecibirOperacion(socket_kernel_memory);
        pthread_mutex_unlock(&mutex_kernel_memory);

        if (respuesta == NECESITA_COMPACTACION)
        {
            // disparar compactación (lo vemos en el fix de compactación)
        }

        // en todos los casos reenviar el proceso a la MISMA CPU
        CambiarEstado(p, EXEC);
        cpu->ocupado = true;
        cpu->pid_actual = p->pid;
        // no liberar la CPU, el proceso vuelve a ella
    }
    else if (strcmp(syscall_nombre, "MEM_FREE") == 0)
    {
        int id_segmento;
        recv(cpu->socket, &id_segmento, sizeof(int), MSG_WAITALL);

        pthread_mutex_lock(&mutex_kernel_memory);
        t_paquete *paquete = CrearPaquete(ELIMINAR_SEGMENTO);
        AgregarAPaquete(paquete, &pid, sizeof(int));
        AgregarAPaquete(paquete, &id_segmento, sizeof(int));
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        RecibirOperacion(socket_kernel_memory); // confirmación
        pthread_mutex_unlock(&mutex_kernel_memory);

        // reenviar a la MISMA CPU
        CambiarEstado(p, EXEC);
        cpu->ocupado = true;
        cpu->pid_actual = p->pid;

        // MEM_FREE libera memoria, avisar al mediano plazo
        sem_post(&sem_memoria_disponible);
    }
    else if (strcmp(syscall_nombre, "EXIT") == 0)
    {
        pthread_mutex_lock(&mutex_kernel_memory);
        t_paquete *paquete = CrearPaquete(FINALIZAR_PROCESO);
        AgregarAPaquete(paquete, &pid, sizeof(int));
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);
        RecibirOperacion(socket_kernel_memory);
        pthread_mutex_unlock(&mutex_kernel_memory);

        CambiarEstado(p, EXIT);
        log_info(logger, "## (%d) finalizó su ejecución con motivo de EXIT", pid);

        pthread_mutex_lock(&mutex_procesos);
        list_remove_element(lista_procesos, p);
        pthread_mutex_unlock(&mutex_procesos);

        if (p->codigo_dinamico)
            free(p->codigo);
        free(p);

        // liberar la CPU
        cpu->ocupado = false;
        cpu->pid_actual = -1;
        sem_post(&sem_cpu_disponible);

        // EXIT libera memoria también
        sem_post(&sem_memoria_disponible);
    }
    else if (strcmp(syscall_nombre, "INIT_PROC") == 0)
    {
        char archivo_instrucciones[100];
        int prioridad_nuevo;

        recv(cpu->socket, archivo_instrucciones, sizeof(archivo_instrucciones), MSG_WAITALL);
        recv(cpu->socket, &prioridad_nuevo, sizeof(int), MSG_WAITALL);

        // asignar nuevo PID
        pthread_mutex_lock(&mutex_pid);
        int nuevo_pid = siguiente_pid++;
        pthread_mutex_unlock(&mutex_pid);

        proceso *nuevo = malloc(sizeof(proceso));
        nuevo->pid = nuevo_pid;
        nuevo->tamanio = 0;
        nuevo->codigo = strdup(archivo_instrucciones); // copia propia, no apunta al buffer del socket
        nuevo->codigo_dinamico = true;
        nuevo->prioridad = prioridad_nuevo;
        nuevo->prioridad_heredada = false;
        nuevo->estado = NEW;
        nuevo->dir_fisica_io = 0;
        nuevo->tamanio_io = 0;
        nuevo->tiempo_suspension = 0;

        pthread_mutex_lock(&mutex_procesos);
        list_add(lista_procesos, nuevo);
        pthread_mutex_unlock(&mutex_procesos);

        pthread_mutex_lock(&mutex_new);
        queue_push(cola_new, nuevo);
        pthread_mutex_unlock(&mutex_new);

        log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", nuevo->pid);

        sem_post(&sem_new); // despertar al planificador de largo plazo

        // el proceso que hizo INIT_PROC vuelve a la MISMA CPU
        CambiarEstado(p, EXEC);
        cpu->ocupado = true;
        cpu->pid_actual = p->pid;
    }
}

void EnviarAIO(char *tipo_buscado, int operacion, int pid, void *params)
{
    // buscar el IO del tipo correcto en lista_ios
    pthread_mutex_lock(&mutex_ios);
    t_io_conectado *io_destino = NULL;
    for (int i = 0; i < list_size(lista_ios); i++)
    {
        t_io_conectado *io = list_get(lista_ios, i);
        if (strcmp(io->tipo_io, tipo_buscado) == 0)
        {
            io_destino = io;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_ios);

    if (io_destino == NULL)
    {
        log_error(logger, "No hay IO de tipo %s conectado", tipo_buscado);
        return;
    }

    t_paquete *paquete = CrearPaquete(operacion);
    AgregarAPaquete(paquete, &pid, sizeof(int));

    if (operacion == IO_SLEEP && params != NULL)
    {
        int tiempo = *(int *)params;
        AgregarAPaquete(paquete, &tiempo, sizeof(int));
    }
    else if (operacion == IO_STDIN_READ && params != NULL)
    {
        int tamanio = *(int *)params; // params[0] = tamanio
        AgregarAPaquete(paquete, &tamanio, sizeof(int));
    }
    else if (operacion == IO_STDOUT_WRITE && params != NULL)
    {
        int *p = (int *)params;
        int direccion_logica = p[0];
        int tamanio = p[1];

        // antes de mandar al IO hay que pedir los bytes a Kernel Memory
        pthread_mutex_lock(&mutex_kernel_memory);
        t_paquete *paq_km = CrearPaquete(LEER);
        AgregarAPaquete(paq_km, &pid, sizeof(int));
        AgregarAPaquete(paq_km, &direccion_logica, sizeof(int));
        AgregarAPaquete(paq_km, &tamanio, sizeof(int));
        EnviarPaquete(paq_km, socket_kernel_memory);
        EliminarPaquete(paq_km);

        int size;
        void *bytes = RecibirBuffer(&size, socket_kernel_memory);
        pthread_mutex_unlock(&mutex_kernel_memory);

        // agregar los bytes al paquete para el IO
        AgregarAPaquete(paquete, bytes, size);
        free(bytes);
    }

    EnviarPaquete(paquete, io_destino->socket);
    EliminarPaquete(paquete);
}

// Función principal que inicia el servidor multihilo
void IniciarServidorMultihilo()
{
    socket_servidor = IniciarServidor(puerto_escucha);

    // Crear hilo que acepta conexiones continuamente
    pthread_t hilo_aceptador;
    pthread_create(&hilo_aceptador, NULL, AceptarConexiones, NULL);
    pthread_detach(hilo_aceptador);
}

// Hilo que acepta conexiones indefinidamente
void *AceptarConexiones(void *args)
{
    while (1)
    {
        int socket_cliente = EsperarConexion(socket_servidor);

        if (socket_cliente > 0)
        {
            // Crear un hilo para manejar este cliente
            pthread_t hilo_cliente;
            int *socket_ptr = malloc(sizeof(int));
            *socket_ptr = socket_cliente;
            pthread_create(&hilo_cliente, NULL, ManejarCliente, socket_ptr);
            pthread_detach(hilo_cliente);
        }
    }
    return NULL;
}

// Manejar un cliente recién conectado
void *ManejarCliente(void *arg)
{
    int socket_cliente = *(int *)arg;
    free(arg);

    // Recibir el primer mensaje que identifica al cliente
    int op_code = RecibirOperacion(socket_cliente);

    switch (op_code)
    {
    case CPU_A_KERNEL_SCHEDULER:
        RegistrarCPU(socket_cliente);
        break;
    case IO:
        RegistrarIO(socket_cliente);
        break;
    default:
        log_error(logger, "Operación desconocida: %d", op_code);
        close(socket_cliente);
    }

    return NULL;
}

// Registrar una CPU conectada
void RegistrarCPU(int socket_cliente)
{
    // Recibir el ID de la CPU
    int id_cpu;
    recv(socket_cliente, &id_cpu, sizeof(int), MSG_WAITALL);

    // Crear estructura
    t_cpu_conectada *cpu = malloc(sizeof(t_cpu_conectada));
    cpu->socket = socket_cliente;
    cpu->id_cpu = id_cpu;
    cpu->ocupado = false;
    cpu->pid_actual = -1;

    // Agregar a la lista global
    pthread_mutex_lock(&mutex_cpus);
    list_add(lista_cpus, cpu);
    pthread_mutex_unlock(&mutex_cpus);

    // Log obligatorio
    log_info(logger, "## CPU %d Conectada", id_cpu);

    // Incrementar semáforo de CPU disponible
    sem_post(&sem_cpu_disponible);

    // Crear hilo para atender a esta CPU específica
    pthread_t hilo_atencion;
    pthread_create(&hilo_atencion, NULL, AtenderCPU, cpu);
    pthread_detach(hilo_atencion);
}

// Registrar un módulo IO conectado
void RegistrarIO(int socket_cliente)
{
    // Recibir el tipo de IO
    char tipo_io[20];
    recv(socket_cliente, tipo_io, sizeof(tipo_io), MSG_WAITALL);

    // Crear estructura
    t_io_conectado *io = malloc(sizeof(t_io_conectado));
    io->socket = socket_cliente;
    strncpy(io->tipo_io, tipo_io, sizeof(io->tipo_io) - 1);
    // io->tipo_io[sizeof(io->tipo_io) - 1] = '\0';  // asegurar null terminator, en caso de que el IO no lo haya hecho

    // Agregar a la lista global
    pthread_mutex_lock(&mutex_ios);
    list_add(lista_ios, io);
    pthread_mutex_unlock(&mutex_ios);

    // Log obligatorio
    log_info(logger, "## IO Conectado (%s)", tipo_io);

    // Crear hilo para atender este IO
    pthread_t hilo_atencion;
    pthread_create(&hilo_atencion, NULL, AtenderIO, io);
    pthread_detach(hilo_atencion);
}

// Atender a una CPU específica (bucle de trabajo)
void *AtenderCPU(void *args)
{
    t_cpu_conectada *cpu = (t_cpu_conectada *)args;

    while (1)
    {
        // Esperar a que el scheduler le asigne un proceso o reciba interrupción
        int op_code = RecibirOperacion(cpu->socket);

        switch (op_code)
        {
        case CPU_SOLICITA_PID:
            // La CPU pide trabajo (está libre)
            cpu->ocupado = false;
            cpu->pid_actual = -1;
            sem_post(&sem_cpu_disponible);
            break;

        case CPU_SYSCALL:
            // La CPU ejecutó una syscall
            ProcesarSyscall(cpu);
            break;

        case CPU_PID_DEVUELTO:
            // La CPU terminó/devolvió un proceso
            {
                int pid;
                recv(cpu->socket, &pid, sizeof(int), MSG_WAITALL);
                log_info(logger, "## (%d) Proceso devuelto por CPU %d", pid, cpu->id_cpu);

                proceso *p = BuscarProceso(pid);

                pthread_mutex_lock(&mutex_quantum);
                quantum_activo = false;
                pthread_mutex_unlock(&mutex_quantum);

                cpu->ocupado = false;
                cpu->pid_actual = -1;
                
                pthread_mutex_lock(&mutex_compactacion);
                bool en_compactacion = compactacion_en_curso;
                pthread_mutex_unlock(&mutex_compactacion);

                if (en_compactacion)
                {
                    // desalojado por compactación: va al PRINCIPIO de su cola, no libera CPU todavía
                    EncolarReadyAlPrincipio(p);
                    sem_post(&sem_cpus_desalojadas); // confirmar a IniciarCompactacion
                }
                else
                {
                    EncolarReady(p);
                    sem_post(&sem_cpu_disponible);
                }
            }
            break;

        case DESCONEXION:
            log_info(logger, "## CPU %d Desconectada", cpu->id_cpu);
            // Remover de la lista
            pthread_mutex_lock(&mutex_cpus);
            list_remove_element(lista_cpus, cpu);
            pthread_mutex_unlock(&mutex_cpus);
            free(cpu);
            return NULL;

        default:
            log_error(logger, "Operación desconocida de CPU: %d", op_code);
        }
    }

    return NULL;
}

// Atender a un módulo IO (espera órdenes del Scheduler)
void *AtenderIO(void *args)
{
    t_io_conectado *io = (t_io_conectado *)args;

    while (1)
    {
        int op_code = RecibirOperacion(io->socket);

        switch (op_code)
        {

        case IO_FINALIZADO:
        {
            int pid;
            recv(io->socket, &pid, sizeof(int), MSG_WAITALL);
            proceso *p = BuscarProceso(pid);

            if (strcmp(io->tipo_io, "STDIN") == 0)
            {
                // el IO manda los bytes que leyó del teclado
                int size;
                void *bytes = RecibirBuffer(&size, io->socket);

                pthread_mutex_lock(&mutex_kernel_memory);
                t_paquete *paquete = CrearPaquete(ESCRIBIR);
                AgregarAPaquete(paquete, &pid, sizeof(int));
                AgregarAPaquete(paquete, &(p->dir_fisica_io), sizeof(int));
                AgregarAPaquete(paquete, &(p->tamanio_io), sizeof(int));
                AgregarAPaquete(paquete, bytes, size);
                EnviarPaquete(paquete, socket_kernel_memory);
                EliminarPaquete(paquete);

                RecibirOperacion(socket_kernel_memory); // confirmación
                pthread_mutex_unlock(&mutex_kernel_memory);

                free(bytes);
            }

            if (p->estado == SUSP_BLOCK)
            {
                CambiarEstado(p, SUSP_READY);
                pthread_mutex_lock(&mutex_susp);
                queue_push(cola_susp_ready, p);
                pthread_mutex_unlock(&mutex_susp);
                log_info(logger, "## (%d) finalizó IO y pasa a SUSP. READY", p->pid);
            }
            else
            {
                EncolarReady(p);
                log_info(logger, "## (%d) finalizó IO y pasa a READY", p->pid);
            }
        }
        break;

        case DESCONEXION:
            log_info(logger, "## IO Desconectado (%s)", io->tipo_io);
            pthread_mutex_lock(&mutex_ios);
            list_remove_element(lista_ios, io);
            pthread_mutex_unlock(&mutex_ios);
            free(io);
            return NULL;

        default:
            log_error(logger, "Operación desconocida de IO: %d", op_code);
        }
    }
    return NULL;
}

void EjecutarBSOD()
{
    log_error(logger, "## Blue Screen of Death - Memoria corrupta");

    // cambiar todos los procesos a EXIT
    pthread_mutex_lock(&mutex_procesos);
    for (int i = 0; i < list_size(lista_procesos); i++)
    {
        proceso *p = list_get(lista_procesos, i);
        if (p->estado != EXIT)
        {
            CambiarEstado(p, EXIT);
            log_info(logger, "## (%d) finalizó su ejecución con motivo de BSOD", p->pid);
        }
    }
    pthread_mutex_unlock(&mutex_procesos);

    // desconectar todas las CPUs
    pthread_mutex_lock(&mutex_cpus);
    for (int i = 0; i < list_size(lista_cpus); i++)
    {
        t_cpu_conectada *cpu = list_get(lista_cpus, i);
        EnviarOpCode(cpu->socket, DESCONEXION);
    }
    pthread_mutex_unlock(&mutex_cpus);

    Cleanup();
}

void *EscucharKernelMemory(void *args)
{
    while (1)
    {
        // este socket es exclusivo de este hilo, no necesita mutex
        int op_code = RecibirOperacion(socket_km_eventos);

        switch (op_code)
        {
        case MEMORIA_CORRUPTA:
            EjecutarBSOD();
            return NULL;

        case NUEVO_MEMORY_STICK:
            log_info(logger, "## Nuevo Memory Stick conectado - más memoria disponible");
            sem_post(&sem_memoria_disponible);
            break;

        case SOLICITUD_COMPACTACION:
            IniciarCompactacion();
            break;

        case FIN_COMPACTACION:
            log_info(logger, "## Fin de compactación");
            FinalizarCompactacion();
            sem_post(&sem_memoria_disponible);
            break;

        default:
            log_error(logger, "Notificación desconocida de Kernel Memory: %d", op_code);
        }
    }
    return NULL;
}

// Planificador de largo plazo (NEW -> READY)
void *PlanificadorLargoPlazo(void *args)
{
    while (1)
    {
        sem_wait(&sem_new);

        // Extraer de la cola NEW
        pthread_mutex_lock(&mutex_new);
        proceso *p = queue_pop(cola_new);
        pthread_mutex_unlock(&mutex_new);

        pthread_mutex_lock(&mutex_kernel_memory);

        t_paquete *paquete = CrearPaquete(CREAR_PROCESO);
        AgregarAPaquete(paquete, &(p->pid), sizeof(int));
        AgregarAPaquete(paquete, p->codigo, strlen(p->codigo) + 1);
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        int respuesta_memoria = RecibirOperacion(socket_kernel_memory);

        pthread_mutex_unlock(&mutex_kernel_memory);

        if (respuesta_memoria == MEMORIA_CORRUPTA)
        {
            EjecutarBSOD();
            return NULL;
        }
        else if (respuesta_memoria == PROCESO_CARGADO)
        {
            log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", p->pid);
            EncolarReady(p);
        }
        else if (respuesta_memoria == ESPACIO_INSUFICIENTE)
        {
            log_info(logger, "No hay espacio suficiente para el proceso %d con tamaño %d",
                     p->pid, p->tamanio);
            // En un caso real, habría que manejar esto (suspender, etc.)
            if (p->codigo_dinamico)
                free(p->codigo);
            free(p);
        }
    }
}

void *PlanificadorMedianoPlazo(void *args)
{
    while (1)
    {
        // espera a que haya memoria disponible
        // (se despierta cuando llega nuevo memory stick, se libera memoria, o compactación)
        sem_wait(&sem_memoria_disponible);

        pthread_mutex_lock(&mutex_susp);
        if (queue_is_empty(cola_susp_block) && queue_is_empty(cola_susp_ready))
        {
            pthread_mutex_unlock(&mutex_susp);
            continue;
        }

        // ordenar suspendidos por prioridad, y por tiempo si igual prioridad
        // trabajar sobre una lista temporal para no modificar la cola mientras iteramos
        t_list *suspendidos = list_create();

        // agregar los de SUSP_BLOCK
        t_queue *temp_block = cola_susp_block;
        while (!queue_is_empty(temp_block))
        {
            list_add(suspendidos, queue_pop(temp_block));
        }

        // agregar los de SUSP_READY
        t_queue *temp_ready = cola_susp_ready;
        while (!queue_is_empty(temp_ready))
        {
            list_add(suspendidos, queue_pop(temp_ready));
        }

        pthread_mutex_unlock(&mutex_susp);

        // ordenar: menor prioridad primero, si igual prioridad el que lleva más tiempo suspendido
        list_sort(suspendidos, (void *)comparar_suspension);

        // intentar des-suspender cada proceso en orden
        for (int i = 0; i < list_size(suspendidos); i++)
        {
            proceso *p = list_get(suspendidos, i);

            // consultar a Kernel Memory si hay espacio sin compactar
            pthread_mutex_lock(&mutex_kernel_memory);
            t_paquete *paquete = CrearPaquete(CONSULTAR_ESPACIO);
            AgregarAPaquete(paquete, &(p->pid), sizeof(int));
            EnviarPaquete(paquete, socket_kernel_memory);
            EliminarPaquete(paquete);

            int respuesta = RecibirOperacion(socket_kernel_memory);
            pthread_mutex_unlock(&mutex_kernel_memory);

            if (respuesta == PROCESO_CARGADO)
            {
                // hay espacio: des-suspender
                if (p->estado == SUSP_BLOCK)
                {
                    // todavía esperando IO → SUSP_READY
                    CambiarEstado(p, SUSP_READY);
                    pthread_mutex_lock(&mutex_susp);
                    queue_push(cola_susp_ready, p);
                    pthread_mutex_unlock(&mutex_susp);
                }
                else if (p->estado == SUSP_READY)
                {
                    // IO ya terminó → READY
                    EncolarReady(p);
                }
            }
            else
            {
                // no hay espacio: volver a la cola suspendida
                pthread_mutex_lock(&mutex_susp);
                if (p->estado == SUSP_BLOCK)
                    queue_push(cola_susp_block, p);
                else
                    queue_push(cola_susp_ready, p);
                pthread_mutex_unlock(&mutex_susp);
            }
        }

        list_destroy(suspendidos);
    }
    return NULL;
}

// retorna true si a va antes que b
bool comparar_suspension(proceso *a, proceso *b)
{
    if (a->prioridad != b->prioridad)
        return a->prioridad < b->prioridad;             // menor número = mayor prioridad
    return a->tiempo_suspension < b->tiempo_suspension; // mayor tiempo suspendido primero
}

// Planificador de corto plazo (READY -> EXEC)
void *PlanificadorCortoPlazo(void *args)
{
    while (1)
    {
        sem_wait(&sem_ready);
        sem_wait(&sem_cpu_disponible); // Esperar a que haya una CPU libre

        pthread_mutex_lock(&mutex_compactacion);
        if (compactacion_en_curso)
        {
            pthread_mutex_unlock(&mutex_compactacion);
            sem_post(&sem_ready);
            sem_post(&sem_cpu_disponible);
            usleep(10000);
            continue;
        }
        pthread_mutex_unlock(&mutex_compactacion);

        // Buscar una CPU libre
        pthread_mutex_lock(&mutex_cpus);
        t_cpu_conectada *cpu_libre = NULL;
        for (int i = 0; i < list_size(lista_cpus); i++)
        {
            t_cpu_conectada *cpu = list_get(lista_cpus, i);
            if (!cpu->ocupado)
            {
                cpu_libre = cpu;
                break;
            }
        }
        pthread_mutex_unlock(&mutex_cpus);

        if (cpu_libre == NULL)
        {
            sem_post(&sem_ready); // No debería pasar porque sem_cpu_disponible lo controla
            continue;
        }

        // buscar el proceso de mayor prioridad disponible
        proceso *p = NULL;
        int cola_elegida = -1;

        for (int i = 0; i < cantidad_colas; i++)
        {
            pthread_mutex_lock(&mutex_colas_ready[i]);
            if (!queue_is_empty(colas_ready[i]))
            {
                p = queue_pop(colas_ready[i]);
                cola_elegida = i;
                pthread_mutex_unlock(&mutex_colas_ready[i]);
                break;
            }
            pthread_mutex_unlock(&mutex_colas_ready[i]);
        }

        if (p == NULL)
        {
            // alguien más lo tomó primero
            sem_post(&sem_cpu_disponible);
            continue;
        }

        cpu_libre->ocupado = true;
        cpu_libre->pid_actual = p->pid;

        // Cambiar estado a EXEC
        CambiarEstado(p, EXEC);

        // Enviar el proceso a la CPU
        t_paquete *paquete = CrearPaquete(PROCESO);
        AgregarAPaquete(paquete, &(p->pid), sizeof(int));
        AgregarAPaquete(paquete, p->codigo, p->tamanio);
        EnviarPaquete(paquete, cpu_libre->socket);
        EliminarPaquete(paquete);

        log_info(logger, "## (%d) Enviado a CPU %d", p->pid, cpu_libre->id_cpu);

        // RR según el algoritmo de ESA cola, no global
        if (strcmp(queues_algorithms[cola_elegida], "RR") == 0)
        {
            pthread_mutex_lock(&mutex_quantum);
            quantum_activo = true;
            pthread_mutex_unlock(&mutex_quantum);

            t_args_quantum *args_q = malloc(sizeof(t_args_quantum));
            args_q->cpu = cpu_libre;
            args_q->pid = p->pid;

            pthread_t hilo_q;
            pthread_create(&hilo_q, NULL, HiloQuantum, args_q);
            pthread_detach(hilo_q);
        }
    }
    return NULL;
}

void IniciarProceso(int tamanio, char *codigo)
{
    // Crear el proceso inicial (PID 0)
    proceso *proc_inicial = malloc(sizeof(proceso));
    proc_inicial->pid = 0;
    proc_inicial->tamanio = 0; // el tamaño lo maneja Kernel Memory con el archivo
    proc_inicial->codigo = path_proceso_inicial;
    proc_inicial->codigo_dinamico = false;
    proc_inicial->prioridad = 0;
    proc_inicial->prioridad_heredada = false;
    proc_inicial->estado = NEW;

    pthread_mutex_lock(&mutex_procesos);
    list_add(lista_procesos, proc_inicial);
    pthread_mutex_unlock(&mutex_procesos);

    // Agregarlo a NEW
    pthread_mutex_lock(&mutex_new);
    queue_push(cola_new, proc_inicial);
    pthread_mutex_unlock(&mutex_new);

    log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", proc_inicial->pid);

    sem_post(&sem_new); // despertar al planificador de largo plazo
}

char *NombreEstado(t_estado estado)
{
    switch (estado)
    {
    case NEW:
        return "NEW";
    case READY:
        return "READY";
    case EXEC:
        return "EXEC";
    case BLOCK:
        return "BLOCK";
    case SUSP_BLOCK:
        return "SUSP. BLOCK";
    case SUSP_READY:
        return "SUSP. READY";
    case EXIT:
        return "EXIT";
    default:
        return "DESCONOCIDO";
    }
}

void CambiarEstado(proceso *p, t_estado nuevo_estado)
{
    log_info(logger, "## (%d) Pasa del estado %s al estado %s", p->pid, NombreEstado(p->estado), NombreEstado(nuevo_estado));
    p->estado = nuevo_estado;
}

proceso *BuscarProceso(int pid)
{
    pthread_mutex_lock(&mutex_procesos);
    proceso *resultado = NULL;
    for (int i = 0; i < list_size(lista_procesos); i++)
    {
        proceso *p = list_get(lista_procesos, i);
        if (p->pid == pid)
        {
            resultado = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_procesos);
    return resultado;
}

void *HiloQuantum(void *args)
{
    t_args_quantum *datos = (t_args_quantum *)args;
    t_cpu_conectada *cpu = datos->cpu;
    int pid = datos->pid;
    free(datos);

    // esperar el quantum en microsegundos
    usleep(rr_quantum * 1000);

    pthread_mutex_lock(&mutex_quantum);
    if (!quantum_activo || cpu->pid_actual != pid)
    {
        // el proceso ya terminó o fue desalojado antes, no hacer nada
        pthread_mutex_unlock(&mutex_quantum);
        return NULL;
    }
    quantum_activo = false;
    pthread_mutex_unlock(&mutex_quantum);

    // mandar interrupción a la CPU
    log_info(logger, "## (%d) - Desalojado por fin de quantum", pid);
    EnviarOpCode(cpu->socket, INTERRUPCION_QUANTUM); // op code de interrupción por quantum

    return NULL;
}

void *HiloSuspension(void *args)
{
    t_args_suspension *datos = (t_args_suspension *)args;
    int pid = datos->pid;
    free(datos);

    usleep(suspension_timeout * 1000);

    proceso *p = BuscarProceso(pid);
    if (p == NULL)
        return NULL;

    pthread_mutex_lock(&mutex_procesos);
    if (p->estado != BLOCK)
    {
        // el proceso ya salió de BLOCK antes del timeout (IO terminó)
        pthread_mutex_unlock(&mutex_procesos);
        return NULL;
    }

    // sigue en BLOCK, suspender
    CambiarEstado(p, SUSP_BLOCK);
    p->tiempo_suspension = time(NULL);

    // sacar de cola_block y meter en cola_susp_block
    pthread_mutex_lock(&mutex_block);
    list_remove_element(cola_block->elements, p);
    pthread_mutex_unlock(&mutex_block);

    pthread_mutex_lock(&mutex_susp);
    queue_push(cola_susp_block, p);
    pthread_mutex_unlock(&mutex_susp);

    pthread_mutex_unlock(&mutex_procesos);

    // avisar a Kernel Memory que suspenda el proceso (mueve segmentos a SWAP)
    pthread_mutex_lock(&mutex_kernel_memory);
    t_paquete *paquete = CrearPaquete(SUSPENDER_PROCESO);
    AgregarAPaquete(paquete, &pid, sizeof(int));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);
    RecibirOperacion(socket_kernel_memory); // confirmación
    pthread_mutex_unlock(&mutex_kernel_memory);

    log_info(logger, "## (%d) Pasa del estado BLOCK al estado SUSP. BLOCK por timeout", pid);

    return NULL;
}

t_mutex_proceso *BuscarMutex(char *nombre)
{
    pthread_mutex_lock(&mutex_lista_mutex);
    t_mutex_proceso *resultado = NULL;
    for (int i = 0; i < list_size(lista_mutex); i++)
    {
        t_mutex_proceso *m = list_get(lista_mutex, i);
        if (strcmp(m->nombre, nombre) == 0)
        {
            resultado = m;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_lista_mutex);
    return resultado;
}

void EncolarReady(proceso *p)
{
    int prio = p->prioridad;

    if (prio < 0 || prio >= cantidad_colas)
    {
        log_error(logger, "## (%d) prioridad %d fuera de rango, no planificado", p->pid, prio);
        return;
    }

    CambiarEstado(p, READY);

    pthread_mutex_lock(&mutex_colas_ready[prio]);
    queue_push(colas_ready[prio], p);
    pthread_mutex_unlock(&mutex_colas_ready[prio]);

    sem_post(&sem_ready);

    if (strcmp(planification_algorithm, "CMN") == 0 && queue_preemption)
        IntentarDesalojoPorPrioridad(p);
}

void IntentarDesalojoPorPrioridad(proceso *nuevo)
{
    pthread_mutex_lock(&mutex_cpus);
    for (int i = 0; i < list_size(lista_cpus); i++)
    {
        t_cpu_conectada *cpu = list_get(lista_cpus, i);
        if (cpu->ocupado)
        {
            proceso *ejecutando = BuscarProceso(cpu->pid_actual);
            if (ejecutando != NULL && ejecutando->prioridad > nuevo->prioridad)
            {
                log_info(logger,
                         "## (%d) Prioridad: %d - Desalojado por cola más prioritaria por el proceso %d con prioridad %d",
                         ejecutando->pid, ejecutando->prioridad, nuevo->pid, nuevo->prioridad);

                EnviarOpCode(cpu->socket, INTERRUPCION_DESALOJO_COLA);
                break; // solo se desaloja 1 CPU por evento
            }
        }
    }
    pthread_mutex_unlock(&mutex_cpus);
}

void CambiarPrioridadProceso(proceso *p, int nueva_prioridad)
{
    int prioridad_vieja = p->prioridad;
    if (prioridad_vieja == nueva_prioridad)
        return;

    log_info(logger, "## %d Cambio de prioridad: %d - %d", p->pid, prioridad_vieja, nueva_prioridad);

    if (p->estado == READY && strcmp(planification_algorithm, "CMN") == 0)
    {
        pthread_mutex_lock(&mutex_colas_ready[prioridad_vieja]);
        list_remove_element(colas_ready[prioridad_vieja]->elements, p);
        pthread_mutex_unlock(&mutex_colas_ready[prioridad_vieja]);

        p->prioridad = nueva_prioridad;

        pthread_mutex_lock(&mutex_colas_ready[nueva_prioridad]);
        queue_push(colas_ready[nueva_prioridad], p);
        pthread_mutex_unlock(&mutex_colas_ready[nueva_prioridad]);
    }
    else
    {
        p->prioridad = nueva_prioridad;
    }
}

void IniciarCompactacion()
{
    log_info(logger, "## Inicio de compactación");

    // bloquear el envío de nuevos procesos
    pthread_mutex_lock(&mutex_compactacion);
    compactacion_en_curso = true;
    pthread_mutex_unlock(&mutex_compactacion);

    // contar y desalojar CPUs ocupadas
    pthread_mutex_lock(&mutex_cpus);
    cpus_a_esperar = 0;
    for (int i = 0; i < list_size(lista_cpus); i++)
    {
        t_cpu_conectada *cpu = list_get(lista_cpus, i);
        if (cpu->ocupado)
        {
            cpus_a_esperar++;
            EnviarOpCode(cpu->socket, INTERRUPCION_COMPACTACION);
        }
    }
    pthread_mutex_unlock(&mutex_cpus);

    // esperar a que cada CPU desalojada confirme la devolución del proceso
    for (int i = 0; i < cpus_a_esperar; i++)
        sem_wait(&sem_cpus_desalojadas);

    // avisar a Kernel Memory que ya puede compactar
    EnviarOpCode(socket_km_eventos, CPUS_DESALOJADAS);
}

void EncolarReadyAlPrincipio(proceso *p)
{
    int prio = p->prioridad;

    if (prio < 0 || prio >= cantidad_colas)
    {
        log_error(logger, "## (%d) prioridad %d fuera de rango, no planificado", p->pid, prio);
        return;
    }

    CambiarEstado(p, READY);

    pthread_mutex_lock(&mutex_colas_ready[prio]);
    // queue no tiene push_front, usamos la lista interna
    list_add_in_index(colas_ready[prio]->elements, 0, p);
    pthread_mutex_unlock(&mutex_colas_ready[prio]);

    sem_post(&sem_ready);
}

void FinalizarCompactacion()
{
    log_info(logger, "## Fin de compactación");

    pthread_mutex_lock(&mutex_compactacion);
    compactacion_en_curso = false;
    pthread_mutex_unlock(&mutex_compactacion);

    // las CPUs desalojadas quedaron libres, reactivar el corto plazo
    pthread_mutex_lock(&mutex_cpus);
    for (int i = 0; i < cpus_a_esperar; i++)
        sem_post(&sem_cpu_disponible);
    pthread_mutex_unlock(&mutex_cpus);

    // hay memoria disponible tras compactar
    sem_post(&sem_memoria_disponible);
}

void Cleanup()
{
    log_info(logger, "## Iniciando limpieza de recursos...");

    // liberar strings duplicados con strdup
    free(ip_kernel_memory);
    free(algoritmo);

    // cerrar socket del servidor y de kernel memory
    close(socket_servidor);
    close(socket_kernel_memory);
    close(socket_km_eventos);

    // cerrar y liberar CPUs
    pthread_mutex_lock(&mutex_cpus);
    for (int i = 0; i < list_size(lista_cpus); i++)
    {
        t_cpu_conectada *cpu = list_get(lista_cpus, i);
        close(cpu->socket);
        free(cpu);
    }
    list_destroy(lista_cpus);
    pthread_mutex_unlock(&mutex_cpus);

    // cerrar y liberar IOs
    pthread_mutex_lock(&mutex_ios);
    for (int i = 0; i < list_size(lista_ios); i++)
    {
        t_io_conectado *io = list_get(lista_ios, i);
        close(io->socket);
        free(io);
    }
    list_destroy(lista_ios);
    pthread_mutex_unlock(&mutex_ios);

    // liberar procesos
    pthread_mutex_lock(&mutex_procesos);
    for (int i = 0; i < list_size(lista_procesos); i++)
    {
        proceso *p = list_get(lista_procesos, i);
        if (p->codigo_dinamico)
            free(p->codigo);
        free(p);
    }
    list_destroy(lista_procesos);
    pthread_mutex_unlock(&mutex_procesos);

    // liberar mutexes de usuario y sus colas
    pthread_mutex_lock(&mutex_lista_mutex);
    for (int i = 0; i < list_size(lista_mutex); i++)
    {
        t_mutex_proceso *m = list_get(lista_mutex, i);
        queue_destroy(m->cola_espera);
        pthread_mutex_destroy(&m->mutex_syscall);
        free(m);
    }
    list_destroy(lista_mutex);
    pthread_mutex_unlock(&mutex_lista_mutex);

    // libera el array de colas_ready
    for (int i = 0; i < cantidad_colas; i++)
    {
        queue_destroy(colas_ready[i]);
        pthread_mutex_destroy(&mutex_colas_ready[i]);
        free(queues_algorithms[i]);
    }

    // liberar colas
    queue_destroy(cola_new);
    queue_destroy(cola_block);
    queue_destroy(cola_susp_block);
    queue_destroy(cola_susp_ready);
    free(colas_ready);
    free(mutex_colas_ready);
    free(queues_algorithms);
    free(planification_algorithm);

    // destruir semáforos
    sem_destroy(&sem_new);
    sem_destroy(&sem_ready);
    sem_destroy(&sem_cpu_disponible);

    // destruir mutexes
    pthread_mutex_destroy(&mutex_ready);
    pthread_mutex_destroy(&mutex_new);
    pthread_mutex_destroy(&mutex_cpus);
    pthread_mutex_destroy(&mutex_ios);
    pthread_mutex_destroy(&mutex_procesos);
    pthread_mutex_destroy(&mutex_block);
    pthread_mutex_destroy(&mutex_susp);
    pthread_mutex_destroy(&mutex_kernel_memory);
    pthread_mutex_destroy(&mutex_quantum);
    pthread_mutex_destroy(&mutex_lista_mutex);

    // destruir logger al último
    log_info(logger, "## Limpieza finalizada");
    log_destroy(logger);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s [Archivo Config] [Path Proceso Inicial]\n", argv[0]);
        return 1;
    }

    path_proceso_inicial = argv[2];

    // Inicializar listas
    lista_cpus = list_create();
    lista_ios = list_create();
    lista_procesos = list_create();
    lista_mutex = list_create();

    IniciarSemaforos();
    saludar("kernel_scheduler");
    ObtenerConfig(argv[1]);

    logger = log_create("kernel_scheduler.log", "KERNEL_SCHEDULER", true, log_level);

    // Conectar a Kernel Memory
    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_kernel_memory, KERNEL_SCHEDULER);
    log_info(logger, "## Conectado a Kernel Memory");

    // conexión secundaria para notificaciones espontáneas
    socket_km_eventos = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_km_eventos, KERNEL_SCHEDULER_EVENTOS); // op code nuevo para identificarse

    // Inicializar colas
    cola_new = queue_create();
    colas_ready = malloc(sizeof(t_queue *) * cantidad_colas);
    mutex_colas_ready = malloc(sizeof(pthread_mutex_t) * cantidad_colas);

    for (int i = 0; i < cantidad_colas; i++)
    {
        colas_ready[i] = queue_create();
        pthread_mutex_init(&mutex_colas_ready[i], NULL);
    }
    cola_block = queue_create();
    cola_susp_block = queue_create();
    cola_susp_ready = queue_create();

    // Iniciar servidor multihilo
    IniciarServidorMultihilo();

    // Crear hilos de planificación
    pthread_t hilo_largo, hilo_corto, hilo_mediano, hilo_km;
    pthread_create(&hilo_largo, NULL, PlanificadorLargoPlazo, NULL);
    pthread_create(&hilo_corto, NULL, PlanificadorCortoPlazo, NULL);
    pthread_create(&hilo_mediano, NULL, PlanificadorMedianoPlazo, NULL);
    // Crear hilo para eventos del kernel memory
    pthread_create(&hilo_km, NULL, EscucharKernelMemory, NULL);
    pthread_detach(hilo_km);

    IniciarProceso(0, path_proceso_inicial);

    // Esperar (el programa sigue corriendo)
    pthread_join(hilo_largo, NULL);
    pthread_join(hilo_corto, NULL);
    pthread_join(hilo_mediano, NULL);

    Cleanup();
    return 0;
}
