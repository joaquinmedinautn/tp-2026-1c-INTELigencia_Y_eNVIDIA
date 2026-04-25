#include "kernel_memory.h"


extern t_dictionary* procesos;
extern pthread_mutex_t mx_procesos;
extern char* scripts_path; // SCRIPTS_BASEPATH del config 

void cargar_instrucciones(uint32_t pid, char* path_relativo) {
    // 1. armo la ruta completa combinando basepath y el path del proceso 
    char* path_full = string_from_format("%s/%s", scripts_path, path_relativo);
    
    FILE* archivo = fopen(path_full, "r");
    if (archivo == NULL) {
        // Si no existe, manejo el error 
        free(path_full);
        return;
    }

    // 2. creo y Limpio el contexto de ejecucion 
    t_contexto* contexto = malloc(sizeof(t_contexto));
    contexto->instrucciones = list_create();
    
    // inicializar registros en 0 
    contexto->PC = 0;
    contexto->AX = 0; contexto->BX = 0; contexto->CX = 0; contexto->DX = 0;
    contexto->EAX = 0; contexto->EBX = 0; contexto->ECX = 0; contexto->EDX = 0;
    contexto->SI = 0; contexto->DI = 0;

    // 3. leo el archivo linea por linea y guardar en la lista 
    char* linea = NULL;
    size_t tam = 0;
    while (getline(&linea, &tam, archivo) != -1) {
        // saco el \n del final para que no rompa los logs 
        linea[strcspn(linea, "\n")] = '\0';
        list_add(contexto->instrucciones, string_duplicate(linea));
    }

    // 4. guardo en el diccionario global (protegido con mutex) 
    char* key = string_itoa(pid);
    pthread_mutex_lock(&mx_procesos);
    dictionary_put(procesos, key, contexto);
    pthread_mutex_unlock(&mx_procesos);

    // Limpieza de memoria temporal
    free(linea);
    free(key);
    free(path_full);
    fclose(archivo);
}

char* obtener_instruccion(uint32_t pid, uint32_t pc) {
    char* key = string_itoa(pid);
    
    pthread_mutex_lock(&mx_procesos);
    t_contexto* ctx = dictionary_get(procesos, key);
    
    // el PC es el índice de nuestra lista 
    char* instruccion = NULL;
    if (ctx != NULL && pc < list_size(ctx->instrucciones)) {
        instruccion = list_get(ctx->instrucciones, pc);
    }
    pthread_mutex_unlock(&mx_procesos);
    
    free(key);
    return instruccion;
}