#include <utils/utils.h>

extern t_log* logger;

void saludar(char *quien)
{
    printf("Hola desde %s!!\n", quien);
}

int IniciarServidor(int puerto)
{
    int socket_servidor;

    struct addrinfo hints, *server_info;

    char puerto_str[6];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto);

    getaddrinfo(NULL, puerto_str, &hints, &server_info);

    socket_servidor = socket(server_info->ai_family,
                             server_info->ai_socktype,
                             server_info->ai_protocol);

    
    setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));

    bind(socket_servidor, server_info->ai_addr, server_info->ai_addrlen);

    listen(socket_servidor, SOMAXCONN);

    freeaddrinfo(server_info);
    log_trace(logger, "Listo para escuchar a mi cliente");

    return socket_servidor;
}

int EsperarConexion(int socket_servidor)
{
    int socket_cliente = accept(socket_servidor, NULL, NULL);
	log_info(logger, "Se conecto un cliente!");

	return socket_cliente;
}

int CrearConexion(char *ip, int puerto)
{
	struct addrinfo hints;
	struct addrinfo *server_info;

    char puerto_str[6];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto);

	getaddrinfo(ip, puerto_str, &hints, &server_info);

	int socket_cliente = socket(server_info->ai_family,
                         server_info->ai_socktype,
                         server_info->ai_protocol);
                         
	connect(socket_cliente, server_info->ai_addr, server_info->ai_addrlen);

	freeaddrinfo(server_info);

	return socket_cliente;
}