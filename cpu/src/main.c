#include "cpu.h"
#include <commons/string.h>
#include <string.h>

t_config *config;
char *ip_kernel_scheduler;
int puerto_kernel_scheduler;
char *ip_kernel_memory;
int puerto_kernel_memory;
char *ip_memory_stick;
int puerto_memory_stick;
t_log_level log_level;
t_log *logger;
int socket_kernel_scheduler;
int socket_kernel_memory;
int socket_memory_stick;
t_list *sockets_memory_sticks;
int socket_kernel_interrupt;
int segment_max_size;
int socket_kernel_memory_notif;
pthread_mutex_t mutex_memory_sticks = PTHREAD_MUTEX_INITIALIZER;

int codigo_interrupcion = 0;

void *escuchar_interrupciones(void *arg)
{
    while (1)
    {
        // Leemos EXCLUSIVAMENTE del socket de interrupciones
        int operacion = RecibirOperacion(socket_kernel_interrupt);

        if (operacion == -1 || operacion == DESCONEXION)
        {
            log_error(logger, "Se desconectó el canal de interrupciones");
            break;
        }

        // Si llega CUALQUIER señal por este socket, es una interrupción
        pthread_mutex_lock(&mutex_interrupcion);
        hay_interrupcion = 1;
        codigo_interrupcion = operacion;
        pthread_mutex_unlock(&mutex_interrupcion);
    }
    return NULL;
}

void *escuchar_nuevos_sticks(void *arg)
{
    while (1)
    {
        int operacion = RecibirOperacion(socket_kernel_memory_notif);
        if (operacion == -1 || operacion == DESCONEXION)
            break;

        if (operacion == NUEVO_MEMORY_STICK)
        {
            t_list *valores = RecibirPaquete(socket_kernel_memory_notif);
            uint32_t tamanio = *(uint32_t *)list_get(valores, 0);
            uint32_t base = *(uint32_t *)list_get(valores, 1);
            int puerto_stick = *(int *)list_get(valores, 2);
            char *ip_stick = (char *)list_get(valores, 3);

            int socket_stick = CrearConexion(ip_stick, puerto_stick);
            EnviarOpCode(socket_stick, CPU_A_MEMORY_STICK);

            pthread_mutex_lock(&mutex_memory_sticks);
            list_add(sockets_memory_sticks, (void *)(intptr_t)socket_stick);
            pthread_mutex_unlock(&mutex_memory_sticks);

            log_info(logger, "## Nuevo Memory Stick conectado en %s:%d", ip_stick, puerto_stick);
            list_destroy_and_destroy_elements(valores, free);
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    // Corrección de los argumentos: El estándar del TP es [Archivo Config] [Identificador]
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s [Archivo Config] [Identificador]\n", argv[0]);
        return 1;
    }

    // Obtenemos el ID de CPU del segundo argumento
    int id_cpu = atoi(argv[2]);

    saludar("cpu");
    // Obtenemos config pasándole la ruta del primer argumento (como hicimos con IO)
    ObtenerConfig(argv[1]);
    printf("Se obtuvo el config\n"); // debug

    char *log_filename = string_from_format("cpu_%d.log", id_cpu);
    logger = log_create(log_filename, "CPU", true, log_level);
    free(log_filename);

    sockets_memory_sticks = list_create();
    printf("Se crea la lista de memory sitsck\n"); // debug

    // ==========================================================
    // 1. CONEXIÓN DISPATCH (Principal para Procesos y Syscalls)
    // ==========================================================

    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);
    EnviarOpCode(socket_kernel_scheduler, CPU_A_KERNEL_SCHEDULER);
    send(socket_kernel_scheduler, &id_cpu, sizeof(int), 0);
    printf("Se conecto al kernel scheduler\n");

    // ==========================================================
    // 2. CONEXIÓN INTERRUPT (Exclusiva para desalojos/quantum)
    // ==========================================================
    socket_kernel_interrupt = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);
    EnviarOpCode(socket_kernel_interrupt, CPU_A_KERNEL_SCHEDULER_INTERRUPT);
    send(socket_kernel_interrupt, &id_cpu, sizeof(int), 0);
    printf("Se conecto socket para desalojos\n");

    // 3. NACIMIENTO DEL HILO DE INTERRUPCIONES (Una sola vez)
    pthread_t hilo_interrupciones;
    pthread_create(&hilo_interrupciones, NULL, escuchar_interrupciones, NULL);
    pthread_detach(hilo_interrupciones);
    printf("Se creo el hilo de interrupciones\n");

    // ==========================================================
    // CONEXIÓN KERNEL MEMORY Y MEMORY STICKS
    // ==========================================================
    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_kernel_memory, CPU_A_KERNEL_MEMORY);

    socket_kernel_memory_notif = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_kernel_memory_notif, CPU_A_KERNEL_MEMORY_NOTIFICACIONES);

    int op_km = RecibirOperacion(socket_kernel_memory);
    if (op_km == NUEVO_MEMORY_STICK)
    {
        t_list *valores_sticks = RecibirPaquete(socket_kernel_memory);
        segment_max_size = *(int *)list_get(valores_sticks, 0);
        int cant_sticks = *(int *)list_get(valores_sticks, 1);
        int idx = 2;
        for (int i = 0; i < cant_sticks; i++)
        {
            uint32_t tamanio = *(uint32_t *)list_get(valores_sticks, idx++);
            uint32_t base = *(uint32_t *)list_get(valores_sticks, idx++);

            int puerto_stick = *(int *)list_get(valores_sticks, idx++);
            char *ip_stick = (char *)list_get(valores_sticks, idx++);

            int socket_stick = CrearConexion(ip_stick, puerto_stick);
            EnviarOpCode(socket_stick, CPU_A_MEMORY_STICK);
            list_add(sockets_memory_sticks, (void *)(intptr_t)socket_stick);
            log_info(logger, "## Conectado a Memory Stick en %s:%d", ip_stick, puerto_stick);
        }
        list_destroy_and_destroy_elements(valores_sticks, free);
    }

    pthread_t hilo_sticks;
    pthread_create(&hilo_sticks, NULL, escuchar_nuevos_sticks, NULL);
    pthread_detach(hilo_sticks);

    // ==========================================================
    // BUCLE PRINCIPAL DE DISPATCH
    // ==========================================================
    while (1)
    {
        // Solo escuchamos OpCodes del socket PRINCIPAL
        int op_code = RecibirOperacion(socket_kernel_scheduler);

        if (op_code == PROCESO)
        {
            t_list *valores = RecibirPaquete(socket_kernel_scheduler);
            int pid = *(int *)list_get(valores, 0);
            list_destroy_and_destroy_elements(valores, free);

            iniciar_ciclo(pid);
        }
        else if (op_code <= 0)
        {
            log_error(logger, "Scheduler desconectado.");
            break;
        }
    }

    return 0;
}

void ObtenerConfig(char *path)
{
    config = config_create(path);
    if (config == NULL)
    {
        fprintf(stderr, "No se pudo abrir cpu.config desde %s\n", getcwd(NULL, 0));
        exit(1);
    }

    ip_kernel_scheduler = strdup(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");

    ip_kernel_memory = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    ip_memory_stick = strdup(config_get_string_value(config, "IP_MEMORY_STICK"));
    puerto_memory_stick = config_get_int_value(config, "PUERTO_MEMORY_STICK");

    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
}