#include "io.h"

t_config *config;
char *ip_kernel_scheduler;
int puerto_kernel_scheduler;
t_log_level log_level;
t_log *logger;
int socket_kernel_scheduler;

void ObtenerConfig() {
    config = config_create("io.config");
    if (config == NULL) {
        perror("No se pudo crear el config");
        exit(EXIT_FAILURE);
    }
    ip_kernel_scheduler     = strdup(config_get_string_value(config, "IP_KERNEL_SCHEDULER"));
    puerto_kernel_scheduler = config_get_int_value(config, "PUERTO_KERNEL_SCHEDULER");
    log_level = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    config_destroy(config);
}

int main(int argc, char *argv[])
{
if (argc < 2) {
        fprintf(stderr, "Uso: %s [TIPO (STDIN/STDOUT/SLEEP)]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* tipo = argv[1]; // recibe el tipo por parámetro
    ObtenerConfig();
    logger = log_create("io.log", "IO", true, log_level);

    socket_kernel_scheduler = CrearConexion(ip_kernel_scheduler, puerto_kernel_scheduler);
    
    // identificación inicial ante el Scheduler
    EnviarOpCode(socket_kernel_scheduler, IO);
    send(socket_kernel_scheduler, tipo, 20, 0); // envio el tipo para que el Scheduler nos reconozca

    log_info(logger, "## Conectado a Kernel Scheduler como %s", tipo);

    while (1) {
        int cod_op = RecibirOperacion(socket_kernel_scheduler);

        if (cod_op == -1) {
            log_error(logger, "Desconectado del Scheduler");
            break;
        }

        switch (cod_op) {
            case IO_SLEEP: {
                t_list* valores = RecibirPaquete(socket_kernel_scheduler);
                int pid = *(int*)list_get(valores, 0);
                int tiempo = *(int*)list_get(valores, 1);

                log_info(logger, "## PID: %d - Inicio de IO", pid);
                log_info(logger, "## PID: %d - Haciendo sleep por %d milisegundos.", pid, tiempo);
                
                usleep(tiempo * 1000); 
                
                log_info(logger, "## PID: %d - Fin de IO", pid);
                
                // notificar fin al Scheduler para que desbloquee el proceso
                t_paquete* p = CrearPaquete(IO_FINALIZADO);
                AgregarAPaquete(p, &pid, sizeof(int));
                EnviarPaquete(p, socket_kernel_scheduler);
                EliminarPaquete(p);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case IO_STDOUT_WRITE: {
                t_list* valores = RecibirPaquete(socket_kernel_scheduler);
                int pid = *(int*)list_get(valores, 0);
                char* texto = (char*)list_get(valores, 1); // el scheduler ya nos manda los bytes leídos de memoria

                log_info(logger, "## PID: %d - Inicio de IO", pid);
                log_info(logger, "## PID: %d - %s", pid, texto);
                log_info(logger, "## PID: %d - Fin de IO", pid);

                t_paquete* p = CrearPaquete(IO_FINALIZADO);
                AgregarAPaquete(p, &pid, sizeof(int));
                EnviarPaquete(p, socket_kernel_scheduler);
                EliminarPaquete(p);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case IO_STDIN_READ: {
                t_list* valores = RecibirPaquete(socket_kernel_scheduler);
                int pid = *(int*)list_get(valores, 0);
                int tam_a_leer = *(int*)list_get(valores, 1);

                log_info(logger, "## PID: %d - Inicio de IO", pid);
                printf("## PID: %d - Ingrese %d caracteres: ", pid, tam_a_leer);
                
                char* buffer = calloc(1, tam_a_leer + 1);
                fgets(buffer, tam_a_leer + 1, stdin);
                buffer[strcspn(buffer, "\n")] = '\0'; // limpiar el enter

                log_info(logger, "## PID: %d - Fin de IO", pid);[cite: 2]

                // Enviamos los bytes leídos al Scheduler para que los mande a memoria
                t_paquete* p = CrearPaquete(IO_FINALIZADO);
                AgregarAPaquete(p, &pid, sizeof(int));
                AgregarAPaquete(p, buffer, tam_a_leer);
                EnviarPaquete(p, socket_kernel_scheduler);
                
                free(buffer);
                EliminarPaquete(p);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            default:
                log_warning(logger, "Operación no soportada por esta interfaz");
                break;
        }
    }
    
    return 0;
}



