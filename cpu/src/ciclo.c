#include "ciclo.h"
#include <string.h>
#include <stdio.h>

t_contexto contexto;
int pid_actual;
extern int hay_interrupcion;
extern pthread_mutex_t mutex_interrupcion;

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

int tamanio_registro(char* nombre) {
    if (strcmp(nombre, "AX") == 0) return 1;
    if (strcmp(nombre, "BX") == 0) return 1;
    if (strcmp(nombre, "CX") == 0) return 1;
    if (strcmp(nombre, "DX") == 0) return 1;
    if (strcmp(nombre, "EAX") == 0) return 4;
    if (strcmp(nombre, "EBX") == 0) return 4;
    if (strcmp(nombre, "ECX") == 0) return 4;
    if (strcmp(nombre, "EDX") == 0) return 4;
    if (strcmp(nombre, "SI") == 0) return 4;
    if (strcmp(nombre, "DI") == 0) return 4;
    if (strcmp(nombre, "PC") == 0) return 4;

return 0;
}

void actualizar_contexto() {
    t_paquete* paquete = CrearPaquete(CPU_A_KERNEL_MEMORY);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &contexto, sizeof(t_contexto));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);
}

char* fetch() {
    // Le pedimos la instrucción al Kernel Memory
    // Le mandamos el PID y el PC actual
    t_paquete* paquete = CrearPaquete(SOLICITUD_INSTRUCCION);
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
    } else if (strcmp(inst->nombre, "EXIT") == 0) {
    t_paquete* paquete = CrearPaquete(CPU_PID_DEVUELTO);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    char* motivo = "EXIT";
    AgregarAPaquete(paquete, motivo, strlen(motivo) + 1);
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    return 2;
    } 
    
    else if (strcmp(inst->nombre, "SLEEP") == 0) {
    int tiempo = atoi(inst->params[0]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &tiempo, sizeof(int));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }
    
    else if (strcmp(inst->nombre, "INIT_PROC") == 0) {
    int prioridad = atoi(inst->params[1]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, inst->params[0], strlen(inst->params[0]) + 1);
    AgregarAPaquete(paquete, &prioridad, sizeof(int));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }

    else if (strcmp(inst->nombre, "MEM_ALLOC") == 0) {
    int id_segmento = atoi(inst->params[0]);
    int tamanio = atoi(inst->params[1]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &id_segmento, sizeof(int));
    AgregarAPaquete(paquete, &tamanio, sizeof(int));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);

    contexto.PC++;
    actualizar_contexto();
    return 2;
    }

    else if (strcmp(inst->nombre, "MEM_FREE") == 0) {
    int id_segmento = atoi(inst->params[0]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &id_segmento, sizeof(int));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }  
    
    else if (strcmp(inst->nombre, "MUTEX_CREATE") == 0) {
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, inst->params[0], strlen(inst->params[0]) + 1);
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }

    else if (strcmp(inst->nombre, "MUTEX_LOCK") == 0) {
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, inst->params[0], strlen(inst->params[0]) + 1);
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }

    else if (strcmp(inst->nombre, "MUTEX_UNLOCK") == 0) {
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, inst->params[0], strlen(inst->params[0]) + 1);
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }  

    else if (strcmp(inst->nombre, "STDIN") == 0) {
    uint32_t dir_logica = obtener_valor_registro(inst->params[0]);
    uint32_t tamanio = obtener_valor_registro(inst->params[1]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &dir_logica, sizeof(uint32_t));
    AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    }   
    
    else if (strcmp(inst->nombre, "STDOUT") == 0) {
    uint32_t dir_logica = obtener_valor_registro(inst->params[0]);
    uint32_t tamanio = obtener_valor_registro(inst->params[1]);
    t_paquete* paquete = CrearPaquete(CPU_SYSCALL);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &dir_logica, sizeof(uint32_t));
    AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
    EnviarPaquete(paquete, socket_kernel_scheduler);
    EliminarPaquete(paquete);
    contexto.PC++;
    actualizar_contexto();
    return 2;
    } 
    
    else if (strcmp(inst->nombre, "MOV_IN") == 0) {
    uint32_t dir_logica = obtener_valor_registro("SI");
    int tamanio = tamanio_registro(inst->params[0]);

    t_paquete* paquete = CrearPaquete(LEER);
    AgregarAPaquete(paquete, &pid_actual, sizeof(uint32_t));
    AgregarAPaquete(paquete, &dir_logica, sizeof(uint32_t));
    AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);

    int operacion_movin = RecibirOperacion(socket_kernel_memory);

    if (operacion_movin == SEGMENTATION_FAULT) {
        t_paquete* paquete_pid = CrearPaquete(CPU_PID_DEVUELTO);
        AgregarAPaquete(paquete_pid, &pid_actual, sizeof(int));
        char* motivo = "SEG_FAULT";
        AgregarAPaquete(paquete_pid, motivo, strlen(motivo) + 1);
        EnviarPaquete(paquete_pid, socket_kernel_scheduler);
        EliminarPaquete(paquete_pid);
        return 2;
    } 
    else {
        t_list* valores = RecibirPaquete(socket_kernel_memory);
        void* dato = list_get(valores, 0);
        uint32_t valor;
        if (tamanio == 1) {
            valor = *(uint8_t*)dato;
        } else {
            valor = *(uint32_t*)dato;
        }
        asignar_valor_registro(inst->params[0], valor);
        list_destroy_and_destroy_elements(valores, free);
    }

} 
    else if (strcmp(inst->nombre, "MOV_OUT") == 0) {
    uint32_t dir_logica = obtener_valor_registro("DI");
    uint32_t valor = obtener_valor_registro(inst->params[0]);
    int tamanio = tamanio_registro(inst->params[0]);
    uint8_t val8 = (uint8_t)valor;
    void* dato = (tamanio == 1) ? (void*)&val8 : (void*)&valor;
    
    t_paquete* paquete = CrearPaquete(ESCRIBIR);
    AgregarAPaquete(paquete, &pid_actual, sizeof(uint32_t));
    AgregarAPaquete(paquete, &dir_logica, sizeof(uint32_t));
    AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
    AgregarAPaquete(paquete, dato, tamanio);
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);

    int operacion_movout = RecibirOperacion(socket_kernel_memory);

    if (operacion_movout == SEGMENTATION_FAULT) {
        t_paquete* paquete_pid = CrearPaquete(CPU_PID_DEVUELTO);
        AgregarAPaquete(paquete_pid, &pid_actual, sizeof(int));
        char* motivo = "SEG_FAULT";
        AgregarAPaquete(paquete_pid, motivo, strlen(motivo) + 1);
        EnviarPaquete(paquete_pid, socket_kernel_scheduler);
        EliminarPaquete(paquete_pid);
        return 2;
    }
    return 0;
}

return 0;  // PC no fue modificado
}

int hay_interrupcion = 0;
pthread_mutex_t mutex_interrupcion = PTHREAD_MUTEX_INITIALIZER;

int check_interrupt() {
    pthread_mutex_lock(&mutex_interrupcion);
    int resultado = hay_interrupcion;
    hay_interrupcion = 0;
    pthread_mutex_unlock(&mutex_interrupcion);
    return resultado;
}

void* escuchar_interrupciones(void* arg) {
    while (1) {
        int operacion = RecibirOperacion(socket_kernel_scheduler);
        if (operacion == DESCONEXION) {
            break;
        }
        // Llegó una interrupción
        pthread_mutex_lock(&mutex_interrupcion);
        hay_interrupcion = 1;
        pthread_mutex_unlock(&mutex_interrupcion);
    }
    return NULL;
}

void iniciar_ciclo(int pid) {
    pid_actual = pid;

// Pedimos el contexto al Kernel Memory
t_paquete* paquete_ctx = CrearPaquete(SOLICITUD_CONTEXTO);
AgregarAPaquete(paquete_ctx, &pid_actual, sizeof(uint32_t));
EnviarPaquete(paquete_ctx, socket_kernel_memory);
EliminarPaquete(paquete_ctx);

// Recibimos el contexto
t_list* valores_ctx = RecibirPaquete(socket_kernel_memory);
t_contexto* ctx_recibido = (t_contexto*)list_get(valores_ctx, 0);
contexto.PC  = ctx_recibido->PC;
contexto.AX  = ctx_recibido->AX;  contexto.BX  = ctx_recibido->BX;
contexto.CX  = ctx_recibido->CX;  contexto.DX  = ctx_recibido->DX;
contexto.EAX = ctx_recibido->EAX; contexto.EBX = ctx_recibido->EBX;
contexto.ECX = ctx_recibido->ECX; contexto.EDX = ctx_recibido->EDX;
contexto.SI  = ctx_recibido->SI;  contexto.DI  = ctx_recibido->DI;
list_destroy_and_destroy_elements(valores_ctx, free);

    pthread_t hilo_interrupciones;
pthread_create(&hilo_interrupciones, NULL, escuchar_interrupciones, NULL);
pthread_detach(hilo_interrupciones);

    // Loop principal
    while (1) {
        char* instruccion = fetch();
        t_instruccion* inst = decode(instruccion);
        int pc_modificado = execute(inst);

        if (pc_modificado == 2) {
        break;  // EXIT
        } else if (pc_modificado == 0) {
        contexto.PC++;
        }

        free(inst);
        free(instruccion);

        if (check_interrupt()) {
    log_info(logger, "## Interrupcion recibida");
    
    actualizar_contexto();

    // 2. Devolvemos el PID al Scheduler con motivo INTERRUPCION
    t_paquete* paquete2 = CrearPaquete(CPU_PID_DEVUELTO);
    AgregarAPaquete(paquete2, &pid_actual, sizeof(int));
    char* motivo = "INTERRUPCION";
    AgregarAPaquete(paquete2, motivo, strlen(motivo) + 1);
    EnviarPaquete(paquete2, socket_kernel_scheduler);
    EliminarPaquete(paquete2);
    
    break;
}
    }
}