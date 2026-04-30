#include "ciclo.h"
#include <string.h>
#include <stdio.h>

t_contexto contexto;
int pid_actual;

char* fetch() {
    // Le pedimos la instrucción al Kernel Memory
    // Le mandamos el PID y el PC actual
    t_paquete* paquete = CrearPaquete(SOLICITUD_INTRUCCION);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &contexto.PC, sizeof(uint32_t));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);

    // Recibimos la instrucción como string
    int size;
    char* instruccion = RecibirBuffer(&size, socket_kernel_memory);

    log_info(logger, "## PID: %d - FETCH - Program Counter: %d", pid_actual, contexto.PC);

    return instruccion;
}