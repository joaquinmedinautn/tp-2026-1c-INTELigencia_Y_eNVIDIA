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

int segment_max_size;

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s [Identificador]\n", argv[0]);
        return 1;
    }
    int id_cpu = atoi(argv[1]);

    saludar("cpu");
    ObtenerConfig();

    char *log_filename = string_from_format("cpu_%s.log", argv[2]);
    logger = log_create(log_filename, "CPU", true, log_level);
    free(log_filename);

    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);
    EnviarOpCode(socket_kernel_scheduler, CPU_A_KERNEL_SCHEDULER);
    send(socket_kernel_scheduler, &id_cpu, sizeof(int), 0); // manda el cpu id

    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    EnviarOpCode(socket_kernel_memory, CPU_A_KERNEL_MEMORY);

    int op_km = RecibirOperacion(socket_kernel_memory);
    if (op_km == NUEVO_MEMORY_STICK)
    {
        t_list *sticks = RecibirPaquete(socket_kernel_memory);
        list_destroy_and_destroy_elements(sticks, free);
    }

    // socket_memory_stick = CrearConexion(ip_memory_stick, puerto_memory_stick);
    // EnviarOpCode(socket_memory_stick, CPU_A_MEMORY_STICK);

    /* while (1) {
      // Esperamos un PID del Scheduler
      int size;
      void* buffer = RecibirBuffer(&size, socket_kernel_scheduler);
      int pid = *(int*)buffer;
      free(buffer);

      // Arrancamos el ciclo con ese PID
      iniciar_ciclo(pid);
  }*/
    while (1)
    {
        // 1. Primero recibimos el OpCode para saber qué nos mandan
        int op_code = RecibirOperacion(socket_kernel_scheduler);

        if (op_code == PROCESO)
        {
            // 2. Desempaquetamos la lista de valores
            t_list *valores = RecibirPaquete(socket_kernel_scheduler);

            // 3. Extraemos el PID (el Scheduler manda el PID como primer elemento)
            int pid = *(int *)list_get(valores, 0);

            // 4. Limpiamos memoria para evitar fugas (memory leaks)
            list_destroy_and_destroy_elements(valores, free);

            // Arrancamos el ciclo con ese PID
            iniciar_ciclo(pid);
        }
        else if (op_code == -1)
        {
            log_error(logger, "Scheduler desconectado.");
            break;
        }
    }

    return 0;
}

void ObtenerConfig()
{
    config = config_create("cpu.config");
    if (config == NULL)
    {
        fprintf(stderr, "No se pudo abrir cpu.config desde %s\n", getcwd(NULL, 0));
        exit(1);
    }

    ip_kernel_scheduler = strdup(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");

    ip_kernel_memory = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");

    segment_max_size = config_get_int_value(config, "SEGMENT_MAX_SIZE");
    // ip_memory_stick = config_get_string_value(config, "IP_MEMORY_STICK");
    // puerto_memory_stick = config_get_int_value(config, "PUERTO_MEMORY_STICK");

    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));

    config_destroy(config);
}