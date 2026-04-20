#include <utils/utils.h>

extern t_log *logger;

void saludar(char *quien)
{
    printf("Hola desde %s!!\n", quien);
}

int IniciarServidor(int puerto)
{
    int socket_servidor;

    struct addrinfo hints, *server_info;

    char puerto_str[6]; // getaddrinfo(2) necesita como parametro un const char *

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(puerto_str, sizeof(puerto_str), "%d", puerto); // aca se pasa el puerto como int y devuelve el puerto como const char *

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


int RecibirOperacion(int socket_cliente)
{
	int op_code;
	if(recv(socket_cliente, &op_code, sizeof(int), MSG_WAITALL) > 0)
		return op_code;
	else
	{
		close(socket_cliente);
		return -1;
	}
}

void CrearBuffer(t_paquete *paquete)
{
    paquete->buffer = malloc(sizeof(t_buffer));
    paquete->buffer->size = 0;
    paquete->buffer->stream = NULL;
}

t_paquete *CrearPaquete(int OP)
{
    t_paquete *paquete = malloc(sizeof(t_paquete));
    paquete->codigo_operacion = OP;
    CrearBuffer(paquete);
    return paquete;
}

void *SerializarPaquete(t_paquete *paquete, int bytes)
{
    void* magic = malloc(bytes);
    int offset = 0;

    // copio el codigo de operacion en memoria principal
    memcpy(magic + offset, &(paquete->codigo_operacion), sizeof(int));
    offset += sizeof(int); // avanza en memoria
    memcpy(magic + offset, &(paquete->buffer->size), sizeof(int));
    offset += sizeof(int);
    memcpy(magic + offset, &(paquete->buffer->stream), paquete->buffer->size);

    return magic;
}

void EnviarPaquete(t_paquete *paquete, int socket_conexion)
{
    int bytes = paquete->buffer->size + 2 * sizeof(int);
    void *a_enviar = SerializarPaquete(paquete, bytes);

    send(socket_conexion, a_enviar, bytes, 0);

    free(a_enviar);
}

void EnviarOpCode(int socket_conexion, int OP)
{
    int size = 0;  // Buffer vacío
    void *buffer = malloc(2 * sizeof(int));
    int offset = 0;
    
    memcpy(buffer + offset, &OP, sizeof(int));
    offset += sizeof(int);
    memcpy(buffer + offset, &size, sizeof(int));
    
    send(socket_conexion, buffer, 2 * sizeof(int), 0);
    
    free(buffer);
}