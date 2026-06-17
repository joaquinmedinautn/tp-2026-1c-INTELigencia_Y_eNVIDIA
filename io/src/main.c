#include "io.h"

t_config *config;
char *ip_kernel_scheduler;
int puerto_kernel_scheduler;
t_log_level log_level;
t_log *logger;
int socket_kernel_scheduler;

void ObtenerConfig(char *archivo_config)
{
    config = config_create(archivo_config);
    if (config == NULL)
    {
        perror("No se pudo crear el config");
        exit(EXIT_FAILURE);
    }
    ip_kernel_scheduler = strdup(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
}

int main(int argc, char *argv[])
{
    // validacion de argumentos se debe pasar el tipo (STDIN/STDOUT/SLEEP) al ejecutar
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s [Archivo Config] [TIPO (STDIN/STDOUT/SLEEP)]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *path_config = argv[1];
    char *tipo = argv[2]; // recibe el tipo por parámetro
    ObtenerConfig(path_config);
    // logger obligatorio
    logger = log_create("io.log", "IO", true, log_level);

    // conexion con el scheduler
    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);

    // identificación inicial ante el Scheduler
    EnviarOpCode(socket_kernel_scheduler, IO);
    // send(socket_kernel_scheduler, tipo, 20, 0); // envio el tipo para que el Scheduler nos reconozca

    char buffer_tipo[20];
    memset(buffer_tipo, 0, sizeof(buffer_tipo)); // Llenamos de ceros
    strncpy(buffer_tipo, tipo, 19);              // Copiamos de forma segura
    send(socket_kernel_scheduler, buffer_tipo, 20, 0);

    log_info(logger, "## Conectado a Kernel Scheduler como %s", tipo);

    // bucle principal: espero ordenes de scheduler indefinidamente
    while (1)
    {
        // espera el proximo op code
        int cod_op = RecibirOperacion(socket_kernel_scheduler);
        printf("Codigo de op: %d\n",cod_op); // debug
        fflush(stdout);

        if (cod_op == -1)
        {
            log_error(logger, "Desconectado del Scheduler");
            break;
        }

        switch (cod_op)
        {
        case IO_SLEEP:
        {
            // deserializo el PID y el tiempo de espera
            t_list *valores = RecibirPaquete(socket_kernel_scheduler);
            int pid = *(int *)list_get(valores, 0);
            int tiempo = *(int *)list_get(valores, 1);

            log_info(logger, "## PID: %d - Inicio de IO", pid);
            log_info(logger, "## PID: %d - Haciendo sleep por %d milisegundos.", pid, tiempo);

            usleep(tiempo * 1000);

            log_info(logger, "## PID: %d - Fin de IO", pid);

            // notificar fin al Scheduler para que desbloquee el proceso
            t_paquete *p = CrearPaquete(IO_FINALIZADO);
            AgregarAPaquete(p, &pid, sizeof(int));
            EnviarPaquete(p, socket_kernel_scheduler);
            EliminarPaquete(p);

            // limpieza de la lista recibida para evitar fugas de memoria
            list_destroy_and_destroy_elements(valores, free);
            break;
        }

        case IO_STDOUT_WRITE:
        {
            t_list *valores = RecibirPaquete(socket_kernel_scheduler);
            int pid = *(int *)list_get(valores, 0);
            char *texto = (char *)list_get(valores, 1); // el scheduler ya nos manda los bytes leídos de memoria

            log_info(logger, "## PID: %d - Inicio de IO", pid);
            log_info(logger, "## PID: %d - %s", pid, texto);
            log_info(logger, "## PID: %d - Fin de IO", pid);

            // confirmo finalizacion
            t_paquete *p = CrearPaquete(IO_FINALIZADO);
            AgregarAPaquete(p, &pid, sizeof(int));
            EnviarPaquete(p, socket_kernel_scheduler);
            EliminarPaquete(p);

            list_destroy_and_destroy_elements(valores, free);
            break;
        }

        case IO_STDIN_READ:
        {
            t_list *valores = RecibirPaquete(socket_kernel_scheduler);
            int pid = *(int *)list_get(valores, 0);
            int tam_a_leer = *(int *)list_get(valores, 1);

            log_info(logger, "## PID: %d - Inicio de IO", pid);
            printf("## PID: %d - Ingrese %d caracteres: ", pid, tam_a_leer);

            // reservo memoria para el ingreso de datos
            char *buffer = calloc(1, tam_a_leer + 1);
            if (fgets(buffer, tam_a_leer + 1, stdin) != NULL)
            {
                buffer[strcspn(buffer, "\n")] = '\0'; // limpiar el enter
            }
            if (strchr(buffer, '\n') == NULL) {
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF);
                }

            log_info(logger, "## PID: %d - Fin de IO", pid);
            // enviamos el PID y los caracteres leídos.

            // envio los bytes leídos al Scheduler para que los mande a memoria
            t_paquete *p = CrearPaquete(IO_FINALIZADO);
            AgregarAPaquete(p, &pid, sizeof(int));
            AgregarAPaquete(p, buffer, tam_a_leer);
            EnviarPaquete(p, socket_kernel_scheduler);

            EliminarPaquete(p);
            free(buffer);
            list_destroy_and_destroy_elements(valores, free);
            break;
        }

        default:
            log_warning(logger, "Operación no soportada por esta interfaz");
            break;
        }
    }
    log_destroy(logger);
    free(ip_kernel_scheduler);
    return 0;
}
