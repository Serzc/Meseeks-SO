/*
 * meseeks.c - Tarea 3, Box de Mr. Meeseeks (version inicial)
 * IC-6600 Principios de Sistemas Operativos - Sergio Andres Zuniga Castillo (2024146884)
 *
 * Menu simple: cada opcion es una solicitud con una dificultad distinta.
 * Por cada solicitud se crea un Mr. Meeseeks con fork() que la evalua.
 *
 * Compilar: gcc -Wall -pthread -o meseeks meseeks.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_NIVELES 32

/* Contadores compartidos entre todos los procesos (memoria compartida).
 * instancias[N] = cuantos Meeseeks se han creado en el nivel N.
 * El semaforo mutex protege el acceso a los contadores. */
typedef struct {
    sem_t mutex;
    int nivel_max;
    int instancias[MAX_NIVELES + 1];
} Contadores;

static Contadores *cont;   /* apunta a la memoria compartida */

/* Crea la memoria compartida e inicializa el semaforo (antes de cualquier fork) */
void contadores_crear(void)
{
    cont = mmap(NULL, sizeof(Contadores), PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (cont == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    sem_init(&cont->mutex, 1, 1);   /* 1 = compartido entre procesos */
    cont->nivel_max = 0;
    for (int n = 0; n <= MAX_NIVELES; n++)
        cont->instancias[n] = 0;
}

/* Pone los contadores en cero para una nueva solicitud */
void contadores_reiniciar(void)
{
    sem_wait(&cont->mutex);
    cont->nivel_max = 0;
    for (int n = 0; n <= MAX_NIVELES; n++)
        cont->instancias[n] = 0;
    sem_post(&cont->mutex);
}

/* Asigna N e i a un Meeseeks nuevo: N = nivel del padre + 1,
 * i = siguiente instancia de ese nivel. Seccion critica con el semaforo. */
void asignar_nivel_instancia(int nivel_padre, int *N, int *i)
{
    sem_wait(&cont->mutex);
    *N = nivel_padre + 1;
    if (*N > MAX_NIVELES)
        *N = MAX_NIVELES;
    cont->instancias[*N]++;
    *i = cont->instancias[*N];
    if (*N > cont->nivel_max)
        cont->nivel_max = *N;
    sem_post(&cont->mutex);
}

/* Libera el semaforo y la memoria compartida al salir */
void contadores_destruir(void)
{
    sem_destroy(&cont->mutex);
    munmap(cont, sizeof(Contadores));
}

/* Una opcion del menu: id, nombre que se muestra y dificultad */
typedef struct {
    int id;
    const char *nombre;
    int dificultad;
} Opcion;

/* Lista de opciones del menu */
static const Opcion opciones[] = {
    { 1, "Abrir un frasco",                         20 },
    { 2, "Quiero un cafe frances",                  40 },
    { 3, "Quiero ser feliz",                        60 },
    { 4, "Quitarle dos golpes al juego de Jerry",   80 },
    { 5, "Que Jerry sea un buen golfista",         100 },
};
#define NUM_OPCIONES (int)(sizeof(opciones) / sizeof(opciones[0]))

/* Codigo que ejecuta el Mr. Meeseeks (proceso hijo) */
void meeseeks(int solicitud, const Opcion *op, int nivel_padre)
{
    int nivel, instancia;

    asignar_nivel_instancia(nivel_padre, &nivel, &instancia);

    printf("Hi, I'm Mr. Meeseeks! Look at Meeeee. (%d, %d, %d, %d)\n",
           getpid(), getppid(), nivel, instancia);
    printf("[%d] I am evaluating request #%d: \"%s\" (dificultad %d)\n",
           getpid(), solicitud, op->nombre, op->dificultad);

    exit(0);
}

int main(void)
{
    int opcion, fila, solicitud = 0;
    pid_t pid;

    setbuf(stdout, NULL);   /* sin buffer: fork no duplica la salida */
    contadores_crear();

    while (1) {
        printf("\n===== Meeseeks Box (pid %d) =====\n", getpid());
        for (int i = 0; i < NUM_OPCIONES; i++)
            printf("%d) %s (dificultad %d)\n",
                   opciones[i].id, opciones[i].nombre, opciones[i].dificultad);
        printf("0) Salir\n");
        printf("Opcion: ");

        if (scanf("%d", &opcion) != 1 || opcion == 0)
            break;
        /* buscar la fila de la opcion elegida */
        fila = -1;
        for (int i = 0; i < NUM_OPCIONES; i++)
            if (opciones[i].id == opcion)
                fila = i;
        if (fila < 0) {
            printf("Opcion invalida\n");
            continue;
        }

        solicitud++;
        contadores_reiniciar();
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0)
            meeseeks(solicitud, &opciones[fila], 0);     /* hijo: la Box es nivel 0 */

        waitpid(pid, NULL, 0);                           /* padre espera */
    }

    printf("Adios!\n");
    contadores_destruir();
    return 0;
}
