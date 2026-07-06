#include "swap.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

t_config *config;
char *ip_kernel_memory;
int puerto_kernel_memory;
t_log_level log_level;
t_log *logger;
int socket_kernel_memory;

char* swap_file_path;
int swap_file_size;
int block_size;

void* espacio_swap;
int fd_swap;


void ObtenerConfig(char* path) {
    config               = config_create(path);
    ip_kernel_memory     = strdup(config_get_string_value(config, "IP_KERNEL_MEMORY"));
    puerto_kernel_memory = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
    log_level            = log_level_from_string(config_get_string_value(config, "LOG_LEVEL"));
    
    swap_file_path = strdup(config_get_string_value(config, "SWAP_FILE_PATH"));
    swap_file_size = config_get_int_value(config, "SWAP_FILE_SIZE");
    block_size     = config_get_int_value(config, "BLOCK_SIZE");

    config_destroy(config);

}

void IniciarArchivoSwap(){
    fd_swap = open(swap_file_path, O_RDWR | O_CREAT |  O_TRUNC, S_IRUSR | S_IWUSR);

    if (fd_swap == -1){
        log_error(logger, "No se pudo abrir o crear el archivo de SWAP en %s", swap_file_path);
        exit(EXIT_FAILURE);
    }

    if(ftruncate(fd_swap, swap_file_size) == -1){
        log_error(logger, "Error al darle tamaño al archivo de SWAP");
        close(fd_swap);
        exit(EXIT_FAILURE);
    }

    espacio_swap = mmap(NULL, swap_file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_swap, 0);
    if(espacio_swap == MAP_FAILED){
        log_error(logger, "Error al mapear el archivo de SWAP con mmap");
        close(fd_swap);
        exit(EXIT_FAILURE);
    }

    log_info(logger, "## Archivo SWAP iniciado correctamente en %s", swap_file_path);
}

void CerarArchivoSwap(){
    munmap(espacio_swap, swap_file_size);
    close(fd_swap);
    free(swap_file_path);
}

int main(int argc, char *argv[])
{
    if(argc < 2){
        printf("Error: Falta el archivo de configuracion.\n");
        return(EXIT_FAILURE);
    }
    saludar("swap");
    ObtenerConfig(argv[1]);

    logger = log_create("swap.log", "SWAP", true, log_level);
    IniciarArchivoSwap();
    socket_kernel_memory = CrearConexion(ip_kernel_memory, puerto_kernel_memory);
    log_info(logger,"## Conectado a Kernel Memory");

    t_paquete* paq_handshake = CrearPaquete(SWAP);
    AgregarAPaquete(paq_handshake, &block_size, sizeof(int));
    AgregarAPaquete(paq_handshake, &swap_file_size, sizeof(int));
    EnviarPaquete(paq_handshake, socket_kernel_memory);
    EliminarPaquete(paq_handshake);


    while (1){
        int cod_op = RecibirOperacion(socket_kernel_memory);

        if(cod_op == -1){
            log_error(logger,"Kernel  Memoryse desconcectó. Cerrando SWAP..");
            break;
        }
        switch (cod_op){
            case ESCRITURA_BLOQUE: {
                t_list* valores = RecibirPaquete(socket_kernel_memory);
                int numero_bloque =*(int*)list_get(valores, 0);
                void* contenido = list_get(valores, 1);

                log_info(logger, "## Escritura del bloque: %d", numero_bloque);

                int  offset =  numero_bloque * block_size;

                memcpy((char*)espacio_swap + offset, contenido, block_size);

                msync(espacio_swap, swap_file_size,  MS_SYNC);

                EnviarOpCode(socket_kernel_memory, MENSAJE);

                list_destroy_and_destroy_elements(valores, free);
                break;
            }

            case LECTURA_BLOQUE:{
                t_list* valores = RecibirPaquete(socket_kernel_memory);
                int numero_bloque = *(int*)list_get(valores, 0);

                log_info(logger, "## Lectura del bloque: %d", numero_bloque);

                int offset = numero_bloque * block_size;

                void* buffer_lectura = malloc(block_size);
                memcpy(buffer_lectura, (char*)espacio_swap + offset, block_size);

                t_paquete* res =  CrearPaquete(LECTURA_BLOQUE);
                AgregarAPaquete(res, buffer_lectura, block_size);
                EnviarPaquete(res, socket_kernel_memory);

                EliminarPaquete(res);
                free(buffer_lectura);
                list_destroy_and_destroy_elements(valores, free);
                break;
            }
            default:
            log_warning(logger,"Operacion  desconocida recibida en SWAP: %d", cod_op);
            break;
        }
    }

    CerarArchivoSwap();
    log_destroy(logger);
    free(ip_kernel_memory);
    return EXIT_SUCCESS;
}