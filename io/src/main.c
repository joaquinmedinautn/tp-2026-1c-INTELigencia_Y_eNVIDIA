#include "io.h"

t_log* logger;

void ObtenerConfig(char* path, char** ip_scheduler, int* puerto_scheduler) {
    t_config* config = config_create(path);
    *ip_scheduler     = config_get_string_value(config, "IP_SCHEDULER");
    *puerto_scheduler = config_get_int_value(config, "PUERTO_SCHEDULER");
    config_destroy(config);
}
int main(int argc, char* argv[]){
    
    char* ip_scheduler;       
    int puerto_scheduler;     

    //aca lee el config
    ObtenerConfig(argv[1], &ip_scheduler, &puerto_scheduler);
    
    
    saludar("I/O");    

    logger= log_create("io.log","IO",true,LOG_LEVEL_INFO);
    char* tipo = argv[2];
    
    
    //se conecta con el sch
    int fd_scheduler = CrearConexion(ip_scheduler,puerto_scheduler);
    log_info(logger,"## Conectado a Kernel Scheduler"); 

    //le aviso al scheduler con quien se conecto
    t_tipo_modulo tipo = HANDSHAKE_IO; 
    send(fd_scheduler,&tipo,sizeof(tipo),0);


    pause();

    close(fd_scheduler);
    
    log_destroy(logger);

    return 0;
}


