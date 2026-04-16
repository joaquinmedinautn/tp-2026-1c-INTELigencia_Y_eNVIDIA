#include <swap.h>

void ObtenerConfig(char* path, char** ip_km, int* puerto_km) {
    t_config* config = config_create(path);
    *ip_km     = config_get_string_value(config, "IP_KERNEL_MEMORY");
    *puerto_km = config_get_int_value(config, "PUERTO_KERNEL_MEMORY");
}
t_log* logger;

int main(int argc,char* argv[]){
    char* ip_kernel_memory;
    int puerto_kernel_memory;

    ObtenerConfig(argv[1],&ip_kernel_memory,&puerto_kernel_memory);

    logger = log_create("swap_log","SWAP",true,LOG_LEVEL_INFO);
    saludar("Soy el Swap");

    //Esta es la parte del socket
    int fd_kernel_memory = CrearConexion(ip_kernel_memory,puerto_kernel_memory);
    log_info(logger,"## Conectando a Kernel Memory"); 

    //Parte del handshake
    t_tipo_modulo tipo = HANDSHAKE_SWAP;
    send(fd_kernel_memory,&tipo,sizeof(tipo),0);

    pause();

    close(fd_kernel_memory);

    return 0;

}