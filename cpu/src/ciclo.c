#include "ciclo.h"
#include <string.h>
#include <stdio.h>

t_contexto contexto;
int pid_actual;
extern int hay_interrupcion;
extern pthread_mutex_t mutex_interrupcion;

extern int segment_max_size;

extern int codigo_interrupcion;


pthread_t hilo_interrupciones;

int mmu_traducir(uint32_t dir_logica, uint32_t tamanio, uint32_t *dir_fisica)
{
    uint32_t num_segmento = dir_logica / segment_max_size;
    uint32_t desplazamiento = dir_logica % segment_max_size;

    if (contexto.tabla_segmentos == NULL) {
        return -1; // SEG_FAULT: No hay tabla de segmentos inicializada
    }
    
    t_segmento *segmento_encontrado = NULL;
    for (int i = 0; i < list_size(contexto.tabla_segmentos); i++)
    {
        t_segmento *seg = list_get(contexto.tabla_segmentos, i);
        if (seg->id_segmento == num_segmento)
        {
            segmento_encontrado = seg;
            break;
        }
    }

    if (segmento_encontrado == NULL)
        return -1; // SEG_FAULT: Segmento no existe

    if ((desplazamiento + tamanio) > segmento_encontrado->tamanio)
        return -1; // SEG_FAULT: Out of bounds

    *dir_fisica = segmento_encontrado->base + desplazamiento;
    return 0;
}

uint32_t obtener_valor_registro(char *nombre)
{
    if (strcmp(nombre, "PC") == 0)
        return contexto.PC;
    if (strcmp(nombre, "AX") == 0)
        return contexto.AX;
    if (strcmp(nombre, "BX") == 0)
        return contexto.BX;
    if (strcmp(nombre, "CX") == 0)
        return contexto.CX;
    if (strcmp(nombre, "DX") == 0)
        return contexto.DX;
    if (strcmp(nombre, "EAX") == 0)
        return contexto.EAX;
    if (strcmp(nombre, "EBX") == 0)
        return contexto.EBX;
    if (strcmp(nombre, "ECX") == 0)
        return contexto.ECX;
    if (strcmp(nombre, "EDX") == 0)
        return contexto.EDX;
    if (strcmp(nombre, "SI") == 0)
        return contexto.SI;
    if (strcmp(nombre, "DI") == 0)
        return contexto.DI;
    return 0;
}

void asignar_valor_registro(char *nombre, uint32_t valor)
{
    if (strcmp(nombre, "PC") == 0)
    {
        contexto.PC = valor;
        return;
    }
    if (strcmp(nombre, "AX") == 0)
    {
        contexto.AX = valor;
        return;
    }
    if (strcmp(nombre, "BX") == 0)
    {
        contexto.BX = valor;
        return;
    }
    if (strcmp(nombre, "CX") == 0)
    {
        contexto.CX = valor;
        return;
    }
    if (strcmp(nombre, "DX") == 0)
    {
        contexto.DX = valor;
        return;
    }
    if (strcmp(nombre, "EAX") == 0)
    {
        contexto.EAX = valor;
        return;
    }
    if (strcmp(nombre, "EBX") == 0)
    {
        contexto.EBX = valor;
        return;
    }
    if (strcmp(nombre, "ECX") == 0)
    {
        contexto.ECX = valor;
        return;
    }
    if (strcmp(nombre, "EDX") == 0)
    {
        contexto.EDX = valor;
        return;
    }
    if (strcmp(nombre, "SI") == 0)
    {
        contexto.SI = valor;
        return;
    }
    if (strcmp(nombre, "DI") == 0)
    {
        contexto.DI = valor;
        return;
    }
}

int tamanio_registro(char *nombre)
{
    if (strcmp(nombre, "AX") == 0)
        return 1;
    if (strcmp(nombre, "BX") == 0)
        return 1;
    if (strcmp(nombre, "CX") == 0)
        return 1;
    if (strcmp(nombre, "DX") == 0)
        return 1;
    if (strcmp(nombre, "EAX") == 0)
        return 4;
    if (strcmp(nombre, "EBX") == 0)
        return 4;
    if (strcmp(nombre, "ECX") == 0)
        return 4;
    if (strcmp(nombre, "EDX") == 0)
        return 4;
    if (strcmp(nombre, "SI") == 0)
        return 4;
    if (strcmp(nombre, "DI") == 0)
        return 4;
    if (strcmp(nombre, "PC") == 0)
        return 4;

    return 0;
}

void actualizar_contexto()
{
    t_paquete *paquete = CrearPaquete(CPU_A_KERNEL_MEMORY);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &contexto, sizeof(t_contexto));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);
}

char *fetch()
{
    // Le pedimos la instrucción al Kernel Memory
    // Le mandamos el PID y el PC actual
    t_paquete *paquete = CrearPaquete(SOLICITUD_INSTRUCCION);
    AgregarAPaquete(paquete, &pid_actual, sizeof(int));
    AgregarAPaquete(paquete, &contexto.PC, sizeof(uint32_t));
    EnviarPaquete(paquete, socket_kernel_memory);
    EliminarPaquete(paquete);

    RecibirOperacion(socket_kernel_memory); // Consumimos el OpCode (SOLICITUD_INSTRUCCION)
    t_list *valores = RecibirPaquete(socket_kernel_memory);

    // Extraemos la instrucción y la duplicamos para poder liberar la lista
    char *instruccion = string_duplicate((char *)list_get(valores, 0));
    list_destroy_and_destroy_elements(valores, free);

    log_info(logger, "## PID: %d - FETCH - Program Counter: %d", pid_actual, contexto.PC);
    return instruccion;
}

t_instruccion *decode(char *instruccion)
{
    t_instruccion *inst = malloc(sizeof(t_instruccion));
    inst->cant_params = 0;

    // Separamos el string por espacios
    char *token = strtok(instruccion, " ");
    inst->nombre = token;

    // Los siguientes tokens son los parámetros
    token = strtok(NULL, " ");
    while (token != NULL && inst->cant_params < 4)
    {
        inst->params[inst->cant_params] = token;
        inst->cant_params++;
        token = strtok(NULL, " ");
    }

    // Armamos un string con todos los parámetros
    char params_str[256] = "";
    for (int i = 0; i < inst->cant_params; i++)
    {
        strcat(params_str, inst->params[i]);
        if (i < inst->cant_params - 1)
            strcat(params_str, " ");
    }
    log_info(logger, "## PID: %d - Ejecutando: %s - %s", pid_actual, inst->nombre, params_str);
    return inst;
}

int execute(t_instruccion *inst)
{
    if (strcmp(inst->nombre, "NOOP") == 0)
    {
        // no hace nada
    }
    else if (strcmp(inst->nombre, "SET") == 0)
    {
        uint32_t valor = atoi(inst->params[1]);
        asignar_valor_registro(inst->params[0], valor);
    }
    else if (strcmp(inst->nombre, "SUM") == 0)
    {
        uint32_t resultado = obtener_valor_registro(inst->params[0]) + obtener_valor_registro(inst->params[1]);
        asignar_valor_registro(inst->params[0], resultado);
    }
    else if (strcmp(inst->nombre, "SUB") == 0)
    {
        uint32_t resultado = obtener_valor_registro(inst->params[0]) - obtener_valor_registro(inst->params[1]);
        asignar_valor_registro(inst->params[0], resultado);
    }
    else if (strcmp(inst->nombre, "JNZ") == 0)
    {
        uint32_t valor = obtener_valor_registro(inst->params[0]);
        if (valor != 0)
        {
            contexto.PC = atoi(inst->params[1]);
            return 1; // PC modificado, no sumar 1
        }
    }
    else if (strcmp(inst->nombre, "EXIT") == 0)
    {
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, "EXIT");
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        return 2; // Ojo: EXIT no suma PC ni actualiza contexto, el proceso muere acá
    }

    else if (strcmp(inst->nombre, "SLEEP") == 0)
    {
        int tiempo = atoi(inst->params[0]);

        // 1. Avisamos que viene una Syscall
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        
        // 2. Enviamos el PID (4 bytes)
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        // 3. Enviamos el nombre de la Syscall (50 bytes exactos)
        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, "SLEEP");
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        // 4. Enviamos el parámetro tiempo (4 bytes)
        send(socket_kernel_scheduler, &tiempo, sizeof(int), 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "INIT_PROC") == 0)
    {
        int prioridad = atoi(inst->params[1]);

        // 1. Avisamos que viene una Syscall
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);

        // 2. Enviamos el PID (4 bytes)
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        // 3. Enviamos el nombre de la Syscall (50 bytes exactos)
        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, "INIT_PROC");
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        // 4. Enviamos el archivo de instrucciones (100 bytes exactos)
        char archivo_instrucciones[100];
        memset(archivo_instrucciones, 0, 100);
        strcpy(archivo_instrucciones, inst->params[0]);
        send(socket_kernel_scheduler, archivo_instrucciones, 100, 0);

        // 5. Enviamos la prioridad (4 bytes)
        send(socket_kernel_scheduler, &prioridad, sizeof(int), 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "MEM_ALLOC") == 0)
    {
        int id_segmento = atoi(inst->params[0]);
        int tamanio = atoi(inst->params[1]);

        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, "MEM_ALLOC");
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        send(socket_kernel_scheduler, &id_segmento, sizeof(int), 0);
        send(socket_kernel_scheduler, &tamanio, sizeof(int), 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "MEM_FREE") == 0)
    {
        int id_segmento = atoi(inst->params[0]);

        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, "MEM_FREE");
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        send(socket_kernel_scheduler, &id_segmento, sizeof(int), 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "MUTEX_CREATE") == 0)
    {
        // Este bloque atiende CREATE, LOCK y UNLOCK juntos porque reciben los mismos parámetros
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, inst->nombre);
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        char nombre_mutex[50];
        memset(nombre_mutex, 0, 50);
        strcpy(nombre_mutex, inst->params[0]);
        send(socket_kernel_scheduler, nombre_mutex, 50, 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "MUTEX_LOCK") == 0)
    {
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, inst->nombre);
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        char nombre_mutex[50];
        memset(nombre_mutex, 0, 50);
        strcpy(nombre_mutex, inst->params[0]);
        send(socket_kernel_scheduler, nombre_mutex, 50, 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }

    else if (strcmp(inst->nombre, "MUTEX_UNLOCK") == 0)
    {
        EnviarOpCode(socket_kernel_scheduler, CPU_SYSCALL);
        send(socket_kernel_scheduler, &pid_actual, sizeof(int), 0);

        char nombre_syscall[50];
        memset(nombre_syscall, 0, 50);
        strcpy(nombre_syscall, inst->nombre);
        send(socket_kernel_scheduler, nombre_syscall, 50, 0);

        char nombre_mutex[50];
        memset(nombre_mutex, 0, 50);
        strcpy(nombre_mutex, inst->params[0]);
        send(socket_kernel_scheduler, nombre_mutex, 50, 0);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }
    else if (strcmp(inst->nombre, "STDIN") == 0 || strcmp(inst->nombre, "STDOUT") == 0)
    {
        uint32_t dir_logica = obtener_valor_registro(inst->params[0]);
        uint32_t tamanio = obtener_valor_registro(inst->params[1]);
        uint32_t dir_fisica;

        // 1. Traducir la dirección Lógica a Física usando la MMU
        if (mmu_traducir(dir_logica, tamanio, &dir_fisica) == -1)
        {
            log_error(logger, "## PID: %d - Error de MMU: Segmentation Fault", pid_actual);

            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        // 2. Enviar Syscall al Scheduler CORRECTAMENTE EMPAQUETADA
        t_paquete *paq_sys = CrearPaquete(CPU_SYSCALL);
        
        AgregarAPaquete(paq_sys, &pid_actual, sizeof(int));
        
        // Empaquetamos el nombre de la instrucción (string limpio)
        AgregarAPaquete(paq_sys, inst->nombre, strlen(inst->nombre) + 1);
        
        // Empaquetamos las direcciones
        AgregarAPaquete(paq_sys, &dir_fisica, sizeof(uint32_t));
        AgregarAPaquete(paq_sys, &tamanio, sizeof(uint32_t));
        
        EnviarPaquete(paq_sys, socket_kernel_scheduler);
        EliminarPaquete(paq_sys);

        contexto.PC++;
        actualizar_contexto();
        return 2;
    }
    else if (strcmp(inst->nombre, "MOV_IN") == 0)
    {
        uint32_t dir_logica = obtener_valor_registro("SI");
        int tamanio = tamanio_registro(inst->params[0]);
        uint32_t dir_fisica;

        // 1. Traducir la dirección Lógica a Física usando la MMU
        if (mmu_traducir(dir_logica, tamanio, &dir_fisica) == -1)
        {
            log_error(logger, "## PID: %d - Error de MMU: Segmentation Fault", pid_actual);

            // Devolvemos el proceso al Scheduler indicando el error
            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2; // Cortamos el ciclo
        }

        // 2. Si la MMU da el OK, solicitamos lectura al Kernel Memory usando la Dir Física
        t_paquete *paquete = CrearPaquete(LEER);
        AgregarAPaquete(paquete, &pid_actual, sizeof(uint32_t));
        AgregarAPaquete(paquete, &dir_fisica, sizeof(uint32_t)); // ¡Dirección Física!
        AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        // 3. Recibir la respuesta del Kernel Memory
        int operacion_movin = RecibirOperacion(socket_kernel_memory);

        // Por si el Kernel Memory detecta algún problema de memoria corrupta
        if (operacion_movin == SEGMENTATION_FAULT)
        {
            t_paquete *paquete_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paquete_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paquete_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paquete_pid, socket_kernel_scheduler);
            EliminarPaquete(paquete_pid);
            return 2;
        }
        else
        {
            t_list *valores = RecibirPaquete(socket_kernel_memory);
            void *dato = list_get(valores, 0);
            uint32_t valor;

            if (tamanio == 1)
            {
                valor = *(uint8_t *)dato;
            }
            else
            {
                valor = *(uint32_t *)dato;
            }

            asignar_valor_registro(inst->params[0], valor);

            // Log obligatorio actualizado para mostrar la dirección física real
            log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %d",
                     pid_actual, dir_fisica, valor);

            list_destroy_and_destroy_elements(valores, free);
        }

        return 0; // Retornamos 0 para que el ciclo incremente el Program Counter
    }
    else if (strcmp(inst->nombre, "MOV_OUT") == 0)
    {
        uint32_t dir_logica = obtener_valor_registro("DI");
        uint32_t valor = obtener_valor_registro(inst->params[0]);
        int tamanio = tamanio_registro(inst->params[0]);
        uint32_t dir_fisica;

        // 1. Traducir la dirección Lógica a Física usando la MMU
        if (mmu_traducir(dir_logica, tamanio, &dir_fisica) == -1)
        {
            log_error(logger, "## PID: %d - Error de MMU: Segmentation Fault", pid_actual);
            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        // 2. Preparar el dato a escribir según el tamaño del registro
        uint8_t val8 = (uint8_t)valor;
        void *dato = (tamanio == 1) ? (void *)&val8 : (void *)&valor;

        // 3. ESCRIBIR en Kernel Memory usando la Dirección Física
        t_paquete *paquete = CrearPaquete(ESCRIBIR);
        AgregarAPaquete(paquete, &pid_actual, sizeof(uint32_t));
        AgregarAPaquete(paquete, &dir_fisica, sizeof(uint32_t));
        AgregarAPaquete(paquete, &tamanio, sizeof(uint32_t));
        AgregarAPaquete(paquete, dato, tamanio);
        EnviarPaquete(paquete, socket_kernel_memory);
        EliminarPaquete(paquete);

        int operacion = RecibirOperacion(socket_kernel_memory);
        if (operacion == SEGMENTATION_FAULT)
        {
            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %d",
                 pid_actual, dir_fisica, valor);

        return 0;
    }
    else if (strcmp(inst->nombre, "COPY_MEM") == 0)
    {
        uint32_t tamanio = obtener_valor_registro(inst->params[0]);
        uint32_t dir_logica_origen = obtener_valor_registro("SI");
        uint32_t dir_logica_destino = obtener_valor_registro("DI");
        uint32_t dir_fisica_origen, dir_fisica_destino;

        // 1. Traducir dirección Lógica a Física de ORIGEN (SI)
        if (mmu_traducir(dir_logica_origen, tamanio, &dir_fisica_origen) == -1)
        {
            log_error(logger, "## PID: %d - Error de MMU (Origen): Segmentation Fault", pid_actual);

            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        // 2. Traducir dirección Lógica a Física de DESTINO (DI)
        if (mmu_traducir(dir_logica_destino, tamanio, &dir_fisica_destino) == -1)
        {
            log_error(logger, "## PID: %d - Error de MMU (Destino): Segmentation Fault", pid_actual);

            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        // 3. LEER desde Kernel Memory usando la Dirección Física de ORIGEN
        t_paquete *paq_leer = CrearPaquete(LEER);
        AgregarAPaquete(paq_leer, &pid_actual, sizeof(uint32_t));
        AgregarAPaquete(paq_leer, &dir_fisica_origen, sizeof(uint32_t));
        AgregarAPaquete(paq_leer, &tamanio, sizeof(uint32_t));
        EnviarPaquete(paq_leer, socket_kernel_memory);
        EliminarPaquete(paq_leer);

        int op_leer = RecibirOperacion(socket_kernel_memory);
        if (op_leer == SEGMENTATION_FAULT)
        {
            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        // 4. Recibimos el buffer que leímos de la memoria
        t_list *valores = RecibirPaquete(socket_kernel_memory);
        void *buffer = list_get(valores, 0);

        // 5. ESCRIBIR en Kernel Memory usando la Dirección Física de DESTINO
        t_paquete *paq_escribir = CrearPaquete(ESCRIBIR);
        AgregarAPaquete(paq_escribir, &pid_actual, sizeof(uint32_t));
        AgregarAPaquete(paq_escribir, &dir_fisica_destino, sizeof(uint32_t));
        AgregarAPaquete(paq_escribir, &tamanio, sizeof(uint32_t));
        AgregarAPaquete(paq_escribir, buffer, tamanio);
        EnviarPaquete(paq_escribir, socket_kernel_memory);
        EliminarPaquete(paq_escribir);

        list_destroy_and_destroy_elements(valores, free);

        int op_escribir = RecibirOperacion(socket_kernel_memory);
        if (op_escribir == SEGMENTATION_FAULT)
        {
            t_paquete *paq_pid = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paq_pid, &pid_actual, sizeof(int));
            char *motivo = "SEG_FAULT";
            AgregarAPaquete(paq_pid, motivo, strlen(motivo) + 1);
            EnviarPaquete(paq_pid, socket_kernel_scheduler);
            EliminarPaquete(paq_pid);
            return 2;
        }

        log_info(logger, "PID: %d - Acción: COPY_MEM - Dir. Física Origen: %d - Dir. Física Destino: %d - Tamaño: %d",
                 pid_actual, dir_fisica_origen, dir_fisica_destino, tamanio);

        return 0;
    }
    return 0;
}

int hay_interrupcion = 0;
pthread_mutex_t mutex_interrupcion = PTHREAD_MUTEX_INITIALIZER;

int check_interrupt()
{
    pthread_mutex_lock(&mutex_interrupcion);
    int resultado = hay_interrupcion;
    pthread_mutex_unlock(&mutex_interrupcion);
    return resultado;
}


void iniciar_ciclo(int pid)
{
    pid_actual = pid;

    pthread_mutex_lock(&mutex_interrupcion);
    hay_interrupcion = 0;
    pthread_mutex_unlock(&mutex_interrupcion);

    // Pedimos el contexto al Kernel Memory
    t_paquete *paquete_ctx = CrearPaquete(SOLICITUD_CONTEXTO);
    AgregarAPaquete(paquete_ctx, &pid_actual, sizeof(uint32_t));
    EnviarPaquete(paquete_ctx, socket_kernel_memory);
    EliminarPaquete(paquete_ctx);

    RecibirOperacion(socket_kernel_memory);
    t_list *valores_ctx = RecibirPaquete(socket_kernel_memory);

    contexto.PC  = *(uint32_t*)list_get(valores_ctx, 0);
    contexto.EAX = *(uint32_t*)list_get(valores_ctx, 1);
    contexto.EBX = *(uint32_t*)list_get(valores_ctx, 2);
    contexto.ECX = *(uint32_t*)list_get(valores_ctx, 3);
    contexto.EDX = *(uint32_t*)list_get(valores_ctx, 4);
    contexto.SI  = *(uint32_t*)list_get(valores_ctx, 5);
    contexto.DI  = *(uint32_t*)list_get(valores_ctx, 6);
    contexto.AX  = *(uint8_t*)list_get(valores_ctx, 7);
    contexto.BX  = *(uint8_t*)list_get(valores_ctx, 8);
    contexto.CX  = *(uint8_t*)list_get(valores_ctx, 9);
    contexto.DX  = *(uint8_t*)list_get(valores_ctx, 10);

     // Reconstruimos la tabla de segmentos del proceso
    if (contexto.tabla_segmentos != NULL) {
        list_destroy_and_destroy_elements(contexto.tabla_segmentos, free);
    }
    contexto.tabla_segmentos = list_create();

    int cant_segmentos = *(int*)list_get(valores_ctx, 11);
    log_info(logger, "DEBUG: cant_segmentos recibido = %d", cant_segmentos);
    for (int i = 0; i < cant_segmentos; i++) {
        t_segmento* seg_recibido = (t_segmento*)list_get(valores_ctx, 12 + i);
        t_segmento* nuevo_seg = malloc(sizeof(t_segmento));
        memcpy(nuevo_seg, seg_recibido, sizeof(t_segmento));
        list_add(contexto.tabla_segmentos, nuevo_seg);
    }
    list_destroy_and_destroy_elements(valores_ctx, free);

    // Loop principal
    while (1)
    {
        char *instruccion = fetch();
        t_instruccion *inst = decode(instruccion);
        int resultado = execute(inst);

        if (resultado == 0)
            contexto.PC++;

        free(inst);
        free(instruccion);

        if (resultado == 2)
            break; // EXIT o syscall que libera la CPU

        if (check_interrupt())
        {
            log_info(logger, "## Interrupcion recibida");

            actualizar_contexto();

            // 2. Devolvemos el PID al Scheduler con motivo INTERRUPCION
            t_paquete *paquete2 = CrearPaquete(CPU_PID_DEVUELTO);
            AgregarAPaquete(paquete2, &pid_actual, sizeof(int));
            //char *motivo = "INTERRUPCION";
            pthread_mutex_lock(&mutex_interrupcion);
            hay_interrupcion = 0;
            int motivo_cod = codigo_interrupcion;
            pthread_mutex_unlock(&mutex_interrupcion);

            if (motivo_cod == INTERRUPCION_COMPACTACION) {
                char *motivo = "COMPACTACION";
                AgregarAPaquete(paquete2, motivo, strlen(motivo) + 1);
            } else {
                char *motivo = "QUANTUM";
                AgregarAPaquete(paquete2, motivo, strlen(motivo) + 1);
            }
            
            EnviarPaquete(paquete2, socket_kernel_scheduler);
            EliminarPaquete(paquete2);

            break;
        }
    }

}