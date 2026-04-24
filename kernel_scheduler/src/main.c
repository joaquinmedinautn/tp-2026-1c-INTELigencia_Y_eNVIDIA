#include "kernel_scheduler.h"

char* path_proceso_inicial;
int socket_servidor;
int puerto_escucha;
t_config *config;
int socket_cpu;
int socket_io;
int socket_kernel_memory;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log *logger;
t_log_level log_level;
t_queue *cola_new;
t_queue *cola_ready;
sem_t sem_new;
sem_t sem_ready;
sem_t sem_cpu_disponible;
pthread_mutex_t mutex_ready;
pthread_mutex_t mutex_new;
pthread_mutex_t mutex_cpus;
pthread_mutex_t mutex_ios;
pthread_mutex_t mutex_procesos;
t_list *lista_cpus;
t_list *lista_ios;
t_list* lista_procesos;

void ObtenerConfig(char *archivo_config)
{
    config = config_create(archivo_config);
    puerto_escucha = config_get_int_value(config, "PUERTO_ESCUCHA");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    ip_kernel_memory = config_get_string_value(config, "IP_KERNEL_MEMORY");
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    // No destruir config aún, puede ser necesario después
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
}

// Procesar una syscall de la CPU
void ProcesarSyscall(t_cpu_conectada *cpu)
{
    int pid;
    char syscall_nombre[50];

    recv(cpu->socket, &pid, sizeof(int), MSG_WAITALL);
    recv(cpu->socket, syscall_nombre, sizeof(syscall_nombre), MSG_WAITALL);

    log_info(logger, "## (%d) - Solicito syscall: %s", pid, syscall_nombre);

    // Cambiar estado a BLOCK
    proceso* p = BuscarProceso(pid);
    CambiarEstado(p, BLOCK);

    cpu->ocupado = false;
    cpu->pid_actual = -1;
    sem_post(&sem_cpu_disponible);

    // Aquí se enviaría la orden al IO correspondiente
    // EnviarAIO(...);
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
    strcpy(io->tipo_io, tipo_io);

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
                
                proceso* p = BuscarProceso(pid);
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
    int socket_io = io->socket;

    while (1)
    {
        // El IO se queda esperando órdenes del Scheduler
        int op_code = RecibirOperacion(socket_io);

        switch (op_code)
        {
        case IO_STDIN_READ:
        {
            int pid;
            int tamanio;
            recv(socket_io, &pid, sizeof(int), MSG_WAITALL);
            recv(socket_io, &tamanio, sizeof(int), MSG_WAITALL);

            log_info(logger, "## (%d) Inicio de IO - STDIN", pid);
            // Aquí el IO procesaría la lectura
            // Simulamos respuesta
            log_info(logger, "## (%d) Fin de IO - STDIN", pid);

            // Notificar al scheduler que la IO terminó
            EnviarOpCode(socket_kernel_memory, IO_FINALIZADO);
        }
        break;

        case IO_STDOUT_WRITE:
        {
            int pid;
            recv(socket_io, &pid, sizeof(int), MSG_WAITALL);
            log_info(logger, "## (%d) Inicio de IO - STDOUT", pid);
            // Procesar...
            log_info(logger, "## (%d) Fin de IO - STDOUT", pid);
        }
        break;

        case IO_SLEEP:
        {
            int pid;
            int tiempo;
            recv(socket_io, &pid, sizeof(int), MSG_WAITALL);
            recv(socket_io, &tiempo, sizeof(int), MSG_WAITALL);
            log_info(logger, "## (%d) Inicio de IO - SLEEP por %d ms", pid, tiempo);
            usleep(tiempo * 1000);
            log_info(logger, "## (%d) Fin de IO - SLEEP", pid);
        }
        break;

        case DESCONEXION:
            log_info(logger, "## IO Desconectado (%s)", io->tipo_io);
            pthread_mutex_lock(&mutex_ios);
            list_remove_element(lista_ios, io);
            pthread_mutex_unlock(&mutex_ios);
            free(io);
            return NULL;
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

        t_paquete *paquete = CrearPaquete(CARGAR_PROC);
        AgregarAPaquete(paquete, &(p->pid), sizeof(int));
        AgregarAPaquete(paquete, p->codigo, strlen(p->codigo) + 1);
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        int respuesta_memoria = RecibirOperacion(socket_kernel_memory);

        if (respuesta_memoria == PROCESO_CARGADO)
        {
            log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", p->pid);
            log_info(logger, "Se añade el proceso %d a READY", p->pid);

            pthread_mutex_lock(&mutex_ready);
            CambiarEstado(p, READY);
            queue_push(cola_ready, queue_pop(cola_new));
            pthread_mutex_unlock(&mutex_ready);

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

        // Cambiar estado del proceso (agregar esta función después)
        // cambiar_estado_proceso(p->pid, EXEC);
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
    list_add(lista_procesos, p);
    pthread_mutex_unlock(&mutex_procesos);

    // Agregarlo a NEW
    pthread_mutex_lock(&mutex_new);
    queue_push(cola_new, proc_inicial);
    pthread_mutex_unlock(&mutex_new);

    log_info(logger, "## (%d) Se crea el proceso - Estado: NEW", p->pid);

    sem_post(&sem_new); // despertar al planificador de largo plazo
}

char* NombreEstado(t_estado estado) {
    switch(estado) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case EXEC:       return "EXEC";
        case BLOCK:      return "BLOCK";
        case SUSP_BLOCK: return "SUSP. BLOCK";
        case SUSP_READY: return "SUSP. READY";
        case EXIT:       return "EXIT";
        default:         return "DESCONOCIDO";
    }
}

void CambiarEstado(proceso* p, t_estado nuevo_estado) {
    log_info(logger, "## (%d) Pasa del estado %s al estado %s", p->pid, NombreEstado(p->estado), NombreEstado(nuevo_estado));
    p->estado = nuevo_estado;
}

proceso* BuscarProceso(int pid) {
    pthread_mutex_lock(&mutex_procesos);
    proceso* resultado = NULL;
    for (int i = 0; i < list_size(lista_procesos); i++) {
        proceso* p = list_get(lista_procesos, i);
        if (p->pid == pid) {
            resultado = p;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_procesos);
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
