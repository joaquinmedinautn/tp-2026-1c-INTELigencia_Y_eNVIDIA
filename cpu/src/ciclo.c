#include "ciclo.h"
#include <string.h>
#include <stdio.h>

t_contexto contexto;
int pid_actual;

uint32_t obtener_valor_registro(char* nombre) {
    if (strcmp(nombre, "PC") == 0)  return contexto.PC;
    if (strcmp(nombre, "AX") == 0)  return contexto.AX;
    if (strcmp(nombre, "BX") == 0)  return contexto.BX;
    if (strcmp(nombre, "CX") == 0)  return contexto.CX;
    if (strcmp(nombre, "DX") == 0)  return contexto.DX;
    if (strcmp(nombre, "EAX") == 0) return contexto.EAX;
    if (strcmp(nombre, "EBX") == 0) return contexto.EBX;
    if (strcmp(nombre, "ECX") == 0) return contexto.ECX;
    if (strcmp(nombre, "EDX") == 0) return contexto.EDX;
    if (strcmp(nombre, "SI") == 0)  return contexto.SI;
    if (strcmp(nombre, "DI") == 0)  return contexto.DI;
    return 0;
}

void asignar_valor_registro(char* nombre, uint32_t valor) {
    if (strcmp(nombre, "PC") == 0)  { contexto.PC  = valor; return; }
    if (strcmp(nombre, "AX") == 0)  { contexto.AX  = valor; return; }
    if (strcmp(nombre, "BX") == 0)  { contexto.BX  = valor; return; }
    if (strcmp(nombre, "CX") == 0)  { contexto.CX  = valor; return; }
    if (strcmp(nombre, "DX") == 0)  { contexto.DX  = valor; return; }
    if (strcmp(nombre, "EAX") == 0) { contexto.EAX = valor; return; }
    if (strcmp(nombre, "EBX") == 0) { contexto.EBX = valor; return; }
    if (strcmp(nombre, "ECX") == 0) { contexto.ECX = valor; return; }
    if (strcmp(nombre, "EDX") == 0) { contexto.EDX = valor; return; }
    if (strcmp(nombre, "SI") == 0)  { contexto.SI  = valor; return; }
    if (strcmp(nombre, "DI") == 0)  { contexto.DI  = valor; return; }
}

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

t_instruccion* decode(char* instruccion) {
    t_instruccion* inst = malloc(sizeof(t_instruccion));
    inst->cant_params = 0;

    // Separamos el string por espacios
    char* token = strtok(instruccion, " ");
    inst->nombre = token;

    // Los siguientes tokens son los parámetros
    token = strtok(NULL, " ");
    while (token != NULL && inst->cant_params < 4) {
        inst->params[inst->cant_params] = token;
        inst->cant_params++;
        token = strtok(NULL, " ");
    }

    log_info(logger, "## PID: %d - Ejecutando: %s", pid_actual, inst->nombre);

    return inst;
}

int execute(t_instruccion* inst) {
    if (strcmp(inst->nombre, "NOOP") == 0) {
        // no hace nada

    } else if (strcmp(inst->nombre, "SET") == 0) {
        uint32_t valor = atoi(inst->params[1]);
        asignar_valor_registro(inst->params[0], valor);

    } else if (strcmp(inst->nombre, "SUM") == 0) {
        uint32_t resultado = obtener_valor_registro(inst->params[0]) 
                           + obtener_valor_registro(inst->params[1]);
        asignar_valor_registro(inst->params[0], resultado);

    } else if (strcmp(inst->nombre, "SUB") == 0) {
        uint32_t resultado = obtener_valor_registro(inst->params[0]) 
                           - obtener_valor_registro(inst->params[1]);
        asignar_valor_registro(inst->params[0], resultado);

    } else if (strcmp(inst->nombre, "JNZ") == 0) {
    uint32_t valor = obtener_valor_registro(inst->params[0]);
    if (valor != 0) {
        contexto.PC = atoi(inst->params[1]);
        return 1;  // PC modificado, no sumar 1
        }
    }
return 0;  // PC no fue modificado
}