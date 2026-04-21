#ifndef GESTOR_INTRUCCIONES_H_
#define GESTOR_INTRUCCIONES_H_

typedef struct{
    t_list* intrucciones;

    uint32_t PC;
    uint8_t AX, BX, CX, DX;
    uint32_t EAX, EBX, ECX, EDX;
    uint32_t SI, DI;
} t_contexto;

void inicializar_gestor();
void cargar_instrucciones(uint32_t pid, char* path_relativo);
char* obtener_instruccion(uint32_t pid, uint32_t pc);
void eliminar_contexto(uint32_t pid);

#endif