#include "kernel_scheduler.h"

char *path_proceso_inicial;
int socket_servidor;
int puerto_escucha;
t_config *config;
int socket_cpu;
int socket_io;
int socket_kernel_memory;
char *ip_kernel_memory;
int puerto_kernel_memory;
int rr_quantum;
bool quantum_activo = false;
char *algoritmo;
t_log *logger;
t_log_level log_level;
t_queue *cola_new;
t_queue *cola_ready;
t_queue *cola_block;
t_queue *cola_susp_block;
t_queue *cola_susp_ready;
sem_t sem_new;
sem_t sem_ready;
sem_t sem_cpu_disponible;
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
t_list *lista_cpus;
t_list *lista_ios;
t_list *lista_procesos;
t_list *lista_mutex;

void ObtenerConfig(char *archivo_config)
{
    config = config_create(archivo_config);
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    rr_quantum = config_get_int_value(config, "RR_QUANTUM");
    algoritmo = config_get_string_value(config, "ALGORITMO_DE_PLANIFICACION");

    config_destroy(config);
}

void IniciarSemaforos()
{
    sem_init(&sem_new, 0, 0);
    sem_init(&sem_ready, 0, 0);
    sem_init(&sem_cpu_disponible, 0, 0); // Inicialmente en 0, se incrementa cuando hay CPUs
    pthread_mutex_init(&mutex_ready, NULL);
    pthread_mutex_init(&mutex_cpus, NULL);
    pthread_mutex_init(&mutex_ios, NULL);
    pthread_mutex_init(&mutex_procesos, NULL);
    pthread_mutex_init(&mutex_block, NULL);
    pthread_mutex_init(&mutex_susp, NULL);
    pthread_mutex_init(&mutex_kernel_memory, NULL);
    pthread_mutex_init(&mutex_lista_mutex, NULL);
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
        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, p);
        pthread_mutex_unlock(&mutex_block);
        int direccion_logica, tamanio;
        recv(cpu->socket, &direccion_logica, sizeof(int), MSG_WAITALL);
        recv(cpu->socket, &tamanio, sizeof(int), MSG_WAITALL);
        p->dir_logica_io = direccion_logica;
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
        pthread_mutex_lock(&mutex_block);
        queue_push(cola_block, p);
        pthread_mutex_unlock(&mutex_block);
        int direccion_logica, tamanio;
        recv(cpu->socket, &direccion_logica, sizeof(int), MSG_WAITALL);
        recv(cpu->socket, &tamanio, sizeof(int), MSG_WAITALL);
        p->dir_logica_io = direccion_logica;
        p->tamanio_io = tamanio;
        int params[2] = {direccion_logica, tamanio};
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

            pthread_mutex_lock(&mutex_lista_mutex);
            list_add(lista_mutex, m);
            pthread_mutex_unlock(&mutex_lista_mutex);
        }

        // el proceso NO pasa a BLOCK, sigue ejecutando
        // cancelar el CambiarEstado(p, BLOCK) que hiciste arriba
        CambiarEstado(p, EXEC);
        pthread_mutex_lock(&mutex_block);
        queue_pop(cola_block); // sacarlo de BLOCK porque no se bloquea
        pthread_mutex_unlock(&mutex_block);

        cpu->ocupado = true;
        cpu->pid_actual = p->pid;
        // no liberar la CPU
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

        if (m->pid_dueño == -1)
        {
            // mutex libre, el proceso lo toma y sigue ejecutando
            m->pid_dueño = pid;
            log_info(logger, "## (%d) Toma el Mutex %s", pid, nombre_mutex);

            CambiarEstado(p, EXEC);
            pthread_mutex_lock(&mutex_block);
            queue_pop(cola_block);
            pthread_mutex_unlock(&mutex_block);

            cpu->ocupado = true;
            cpu->pid_actual = p->pid;
        }
        else
        {
            // mutex tomado, el proceso queda en BLOCK esperando
            log_info(logger, "## (%d) queda bloqueado esperando Mutex %s", pid, nombre_mutex);
            queue_push(m->cola_espera, p);
            // el proceso ya está en BLOCK, la CPU queda libre
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
        char nombre_mutex[50];
        recv(cpu->socket, nombre_mutex, sizeof(nombre_mutex), MSG_WAITALL);

        t_mutex_proceso *m = BuscarMutex(nombre_mutex);
        if (m == NULL)
        {
            log_error(logger, "Mutex %s no existe", nombre_mutex);
            return;
        }

        log_info(logger, "## (%d) Libera el Mutex %s", pid, nombre_mutex);

        if (!queue_is_empty(m->cola_espera))
        {
            // hay procesos esperando, el primero toma el mutex
            proceso *siguiente = queue_pop(m->cola_espera);
            m->pid_dueño = siguiente->pid;
            log_info(logger, "## (%d) Toma el Mutex %s", siguiente->pid, nombre_mutex);

            CambiarEstado(siguiente, READY);
            pthread_mutex_lock(&mutex_block);
            list_remove_element(cola_block->elements, siguiente);
            pthread_mutex_unlock(&mutex_block);

            pthread_mutex_lock(&mutex_ready);
            queue_push(cola_ready, siguiente);
            pthread_mutex_unlock(&mutex_ready);

            sem_post(&sem_ready);
        }
        else
        {
            // nadie esperando, el mutex queda libre
            m->pid_dueño = -1;
        }

        // el proceso que hizo unlock sigue ejecutando
        CambiarEstado(p, EXEC);
        pthread_mutex_lock(&mutex_block);
        queue_pop(cola_block);
        pthread_mutex_unlock(&mutex_block);

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

// Enviar PID a CPU (cuando la CPU pide trabajo)
void EnviarPIDaCPU(t_cpu_conectada *cpu)
{
    pthread_mutex_lock(&mutex_ready);
    if (queue_is_empty(cola_ready))
    {
        pthread_mutex_unlock(&mutex_ready);
        // No hay procesos, enviar código de espera
        EnviarOpCode(cpu->socket, KERNEL_NO_HAY_PROCESOS);
        return;
    }

    proceso *p = queue_pop(cola_ready);
    pthread_mutex_unlock(&mutex_ready);

    cpu->ocupado = true;
    cpu->pid_actual = p->pid;

    t_paquete *paquete = CrearPaquete(KERNEL_ENVIA_PID);
    AgregarAPaquete(paquete, &(p->pid), sizeof(int));
    EnviarPaquete(paquete, cpu->socket);
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
    strncpy(io->tipo_io, tipo_io);

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

                CambiarEstado(p, READY);

                pthread_mutex_lock(&mutex_ready);
                queue_push(cola_ready, p);
                pthread_mutex_unlock(&mutex_ready);

                sem_post(&sem_ready);
                cpu->ocupado = false;
                cpu->pid_actual = -1;
                sem_post(&sem_cpu_disponible);
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
                AgregarAPaquete(paquete, &(p->dir_logica_io), sizeof(int));
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
                CambiarEstado(p, READY);
                pthread_mutex_lock(&mutex_ready);
                queue_push(cola_ready, p);
                pthread_mutex_unlock(&mutex_ready);
                sem_post(&sem_ready);
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

    exit(1);
}

void *EscucharKernelMemory(void *args)
{
    while (1)
    {
        pthread_mutex_lock(&mutex_kernel_memory); // <-- lock
        int op_code = RecibirOperacion(socket_kernel_memory);
        pthread_mutex_unlock(&mutex_kernel_memory); // <-- unlock

        switch (op_code)
        {
        case MEMORIA_CORRUPTA:
            EjecutarBSOD();
            return NULL;

        default:
            log_error(logger, "Mensaje desconocido de Kernel Memory: %d", op_code);
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
        proceso *p = queue_peek(cola_new);
        pthread_mutex_unlock(&mutex_new);

        pthread_mutex_lock(&mutex_kernel_memory);

        t_paquete *paquete = CrearPaquete(CARGAR_PROC);
        AgregarAPaquete(paquete, &(p->pid), sizeof(int));
        AgregarAPaquete(paquete, p->codigo, strlen(p->codigo) + 1);
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        int respuesta_memoria = RecibirOperacion(socket_kernel_memory);

        pthread_mutex_unlock(&mutex_kernel_memory);

        if (respuesta_memoria == PROCESO_CARGADO)
        {
            log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", p->pid);

            CambiarEstado(p, READY);
            pthread_mutex_lock(&mutex_ready);
            queue_push(cola_ready, queue_pop(cola_new));
            pthread_mutex_unlock(&mutex_ready);
            log_info(logger, "Se añade el proceso %d a READY", p->pid);

            sem_post(&sem_ready);
        }
        else if (respuesta_memoria == ESPACIO_INSUFICIENTE)
        {
            log_info(logger, "No hay espacio suficiente para el proceso %d con tamaño %d",
                     p->pid, p->tamanio);
            // En un caso real, habría que manejar esto (suspender, etc.)
            queue_pop(cola_new); // Sacarlo de NEW
            free(p);
        }
    }
}

// Planificador de corto plazo (READY -> EXEC)
void *PlanificadorCortoPlazo(void *args)
{
    while (1)
    {
        sem_wait(&sem_ready);
        sem_wait(&sem_cpu_disponible); // Esperar a que haya una CPU libre

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
            // No debería pasar porque sem_cpu_disponible lo controla
            continue;
        }

        // Sacar proceso de READY
        pthread_mutex_lock(&mutex_ready);
        proceso *p = queue_pop(cola_ready);
        pthread_mutex_unlock(&mutex_ready);

        // Marcar CPU como ocupada
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

        // si es RR, lanzar el timer
        if (strcmp(algoritmo, "RR") == 0)
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
}

void IniciarProceso(int tamanio, char *codigo)
{
    // Crear el proceso inicial (PID 0)
    proceso *proc_inicial = malloc(sizeof(proceso));
    proc_inicial->pid = 0;
    proc_inicial->tamanio = 0; // el tamaño lo maneja Kernel Memory con el archivo
    proc_inicial->codigo = path_proceso_inicial;
    proc_inicial->prioridad = 0;
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
    EnviarOpCode(cpu->socket, DESCONEXION); // op code de interrupción por quantum

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

    // Inicializar colas
    cola_new = queue_create();
    cola_ready = queue_create();
    cola_block = queue_create();
    cola_susp_block = queue_create();
    cola_susp_ready = queue_create();

    // Iniciar servidor multihilo
    IniciarServidorMultihilo();

    // Crear hilos de planificación
    pthread_t hilo_largo, hilo_corto;
    pthread_create(&hilo_largo, NULL, PlanificadorLargoPlazo, NULL);
    pthread_create(&hilo_corto, NULL, PlanificadorCortoPlazo, NULL);

    IniciarProceso(0, path_proceso_inicial);

    // Esperar (el programa sigue corriendo)
    pthread_join(hilo_largo, NULL);
    pthread_join(hilo_corto, NULL);

    return 0;
}
