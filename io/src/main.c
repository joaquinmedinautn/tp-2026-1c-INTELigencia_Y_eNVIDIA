#include "io.h"

t_log* logger; 

int main(int argc, char* argv[]){

    saludar("Hola soy la I/O");

    t_config* config = config_create(argv[1]);
    char* tipo = argv[2]:

    logger= log_create("io.log","IO",true,LOG_LEVEL_INFO);

    //aca lee el config
    char* ip_scheduler = config_get_string_value(config,"IP_SCHEDULER");
    int puerto_scheduler = config_get_int_value(config,"PUERTO_SCHEDULER");

    //se conecta con el sch
    int fd_scheduler = CrearConexion(ip_scheduler,puerto_scheduler);
    log_info(logger,"Conectado con el Scheduler"); 

    //le aviso al scheduler con quien se conecto
    t_tipo_modulo mi_tipo = HANDSHAKE_IO; 
    send(fd_scheduler,&mi_tipo,sizeof(mi_tipo),0);


    pause();

    close(fd_scheduler);
    config_destroy(config);
    log_destroy(logger);

    return 0;
}


