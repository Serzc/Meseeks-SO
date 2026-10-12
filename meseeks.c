/*
 * meseeks.c - Tarea 3, Box de Mr. Meeseeks
 * IC-6600 Principios de Sistemas Operativos - Sergio Andres Zuniga Castillo (2024146884)
 *
 * Flujo:  Usuario -(menu)-> Box -fork()-> Meeseeks evaluador (uno por solicitud)
 *         -fork() + pipe-> Meeseeks delegados que resuelven la solicitud.
 *
 * Cada opcion del menu tiene su propia funcion, que ejecuta el evaluador.
 * Documentacion de cada funcion: meseeks.md
 *
 * Compilar: gcc -Wall -pthread -o meseeks meseeks.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_NIVELES  32
#define MAX_HIJOS    10
#define NIVEL_LIMITE 6     /* un Meeseeks con N >= 6 ya no puede pedir ayuda */

/* ======================================================================= */
/*  Contadores compartidos de niveles (N) e instancias (i)                 */
/* ======================================================================= */

/* instancias[N] = cuantos Meeseeks se han creado en el nivel N.
 * El semaforo mutex protege el acceso a los contadores. */
typedef struct {
    sem_t mutex;
    int nivel_max;
    int instancias[MAX_NIVELES + 1];
} Contadores;

static Contadores *cont;   /* apunta a la memoria compartida */

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

void contadores_reiniciar(void)
{
    sem_wait(&cont->mutex);
    cont->nivel_max = 0;
    for (int n = 0; n <= MAX_NIVELES; n++)
        cont->instancias[n] = 0;
    sem_post(&cont->mutex);
}

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

int contadores_total(void)
{
    int total = 0;
    sem_wait(&cont->mutex);
    for (int n = 1; n <= MAX_NIVELES; n++)
        total += cont->instancias[n];
    sem_post(&cont->mutex);
    return total;
}

void contadores_destruir(void)
{
    sem_destroy(&cont->mutex);
    munmap(cont, sizeof(Contadores));
}

/* ======================================================================= */
/*  Solicitudes, opciones del menu y utilidades                            */
/* ======================================================================= */

/* Lo que viaja por el pipe del evaluador a sus delegados */
typedef struct {
    int solicitud;    /* numero de solicitud (#k)                     */
    char texto[64];   /* lo que se pidio                              */
    int dificultad;   /* 0..100, 100 = muy facil; -1 = a la suerte    */
    int a, b;         /* operandos (aritmetica y logica)              */
    char operacion;   /* operacion logica: '&', '|', '^', '!'         */
} Tarea;

/* Que datos le pide la Box al usuario antes de crear al evaluador */
enum { PIDE_NADA, PIDE_DOS_NUMEROS, PIDE_DOS_BOOLEANOS, PIDE_UN_BOOLEANO, PIDE_TEXTO };

typedef struct Opcion Opcion;
typedef struct Menu Menu;

struct Opcion {
    int id;
    const char *nombre;
    int dificultad;                                    /* -1 si no aplica          */
    int pide;                                          /* ver enum PIDE_*          */
    int (*funcion)(const Opcion *op, Tarea t, int N);  /* la ejecuta el evaluador;
                                                          1 = completada           */
    const Menu *submenu;                               /* != NULL: abre un submenu */
};

struct Menu {
    const char *titulo;
    const Opcion *opciones;
    int n;
};

int evaluar_texto(const Opcion *op, Tarea t, int N);
int evaluar_suma(const Opcion *op, Tarea t, int N);
int evaluar_resta(const Opcion *op, Tarea t, int N);
int evaluar_multiplicacion(const Opcion *op, Tarea t, int N);
int evaluar_division(const Opcion *op, Tarea t, int N);
int evaluar_y(const Opcion *op, Tarea t, int N);
int evaluar_o(const Opcion *op, Tarea t, int N);
int evaluar_xor(const Opcion *op, Tarea t, int N);
int evaluar_no(const Opcion *op, Tarea t, int N);

#define CANTIDAD(arr) (int)(sizeof(arr) / sizeof(arr[0]))

/* Submenu de aritmetica */
static const Opcion opciones_aritmetica[] = {
    { 1, "Sumar dos numeros",       -1, PIDE_DOS_NUMEROS, evaluar_suma,           NULL },
    { 2, "Restar dos numeros",      -1, PIDE_DOS_NUMEROS, evaluar_resta,          NULL },
    { 3, "Multiplicar dos numeros", -1, PIDE_DOS_NUMEROS, evaluar_multiplicacion, NULL },
    { 4, "Dividir dos numeros",     -1, PIDE_DOS_NUMEROS, evaluar_division,       NULL },
};
static const Menu menu_aritmetica = { "Aritmetica", opciones_aritmetica, CANTIDAD(opciones_aritmetica) };

/* Submenu de logica (valores 0 = falso, 1 = verdadero) */
static const Opcion opciones_logica[] = {
    { 1, "Y (AND)",  90, PIDE_DOS_BOOLEANOS, evaluar_y,   NULL },
    { 2, "O (OR)",   90, PIDE_DOS_BOOLEANOS, evaluar_o,   NULL },
    { 3, "O exclusivo (XOR)", 90, PIDE_DOS_BOOLEANOS, evaluar_xor, NULL },
    { 4, "NO (NOT)", 90, PIDE_UN_BOOLEANO,   evaluar_no,  NULL },
};
static const Menu menu_logica = { "Logica", opciones_logica, CANTIDAD(opciones_logica) };

/* Menu principal (dificultad: 100 = muy facil, 0 = imposible) */
static const Opcion opciones_principal[] = {
    { 1, "Encontrar las llaves de la casa",   100, PIDE_NADA,  evaluar_texto, NULL },
    { 2, "Hacer el sandwich perfecto",         80, PIDE_NADA,  evaluar_texto, NULL },
    { 3, "Aprender a tocar guitarra",          60, PIDE_NADA,  evaluar_texto, NULL },
    { 4, "Ganar un torneo de ajedrez",         40, PIDE_NADA,  evaluar_texto, NULL },
    { 5, "Terminar la tarea de SO a tiempo",   20, PIDE_NADA,  evaluar_texto, NULL },
    { 6, "Solicitud libre",                    -1, PIDE_TEXTO, evaluar_texto, NULL },
    { 7, "Que Jerry deje de ser Jerry (imposible)", 0, PIDE_NADA, evaluar_texto, NULL },
    { 8, "Aritmetica...",                      -1, PIDE_NADA,  NULL, &menu_aritmetica },
    { 9, "Logica...",                          -1, PIDE_NADA,  NULL, &menu_logica },
};
static const Menu menu_principal = { "Meeseeks Box", opciones_principal, CANTIDAD(opciones_principal) };

double ahora(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void enviar_int(int fd, int x)   { write(fd, &x, sizeof x); }
int  recibir_int(int fd)         { int x = 0; read(fd, &x, sizeof x); return x; }
void enviar_tarea(int fd, Tarea t) { write(fd, &t, sizeof t); }
Tarea recibir_tarea(int fd)      { Tarea t; memset(&t, 0, sizeof t); read(fd, &t, sizeof t); return t; }

/* Todo Meeseeks llama a esta funcion al nacer: obtiene N e i y saluda */
void presentarse(int nivel_padre, int *N, int *i)
{
    asignar_nivel_instancia(nivel_padre, N, i);
    srand(getpid() ^ time(NULL));
    printf("%*sHi, I'm Mr. Meeseeks! Look at Meeeee. (%d, %d, %d, %d)\n",
           (*N - 1) * 2, "", getpid(), getppid(), *N, *i);
}

/* Crea un Meeseeks hijo que ejecuta 'cuerpo' con los descriptores 'fd' */
pid_t crear_meeseeks(void (*cuerpo)(int nivel_padre, const int *fd),
                     int nivel_padre, const int *fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        cuerpo(nivel_padre, fd);
        exit(0);
    }
    return pid;
}

/* ======================================================================= */
/*  Caso A: solicitudes de texto (simuladas)                               */
/* ======================================================================= */

/* Hijos vivos del proceso actual (para terminarlos si llega SIGTERM) */
static pid_t mis_hijos[MAX_HIJOS];
static int   n_mis_hijos = 0;
static char  despedida[128];

/* Ayudantes segun las instrucciones */
int ayudantes_necesarios(int d)
{
    if (d > 85) return 1;
    if (d > 45) return 2;
    return 5;
}

/* Al repartir la tarea entre k ayudantes, a cada uno le toca mas facil:
 * la dificultad sube 1/k de lo que le falta para llegar a 100 */
int diluir(int d, int k)
{
    if (d == 0)
        return 0;                  /* lo imposible sigue siendo imposible */
    return d + (100 - d) / (k * k);
}

/* Si otro Meeseeks ya resolvio: termina a sus hijos, se despide y desaparece */
void al_recibir_sigterm(int sig)
{
    (void)sig;
    for (int j = 0; j < n_mis_hijos; j++)
        if (mis_hijos[j] > 0) {
            kill(mis_hijos[j], SIGTERM);
            waitpid(mis_hijos[j], NULL, 0);
        }
    write(STDOUT_FILENO, despedida, strlen(despedida));
    _exit(2);
}

void delegado_texto(int nivel_padre, const int *fd);

/* Crea k delegados, les envia la tarea por pipe y espera a que alguno la
 * resuelva. Al primer exito termina a los demas. Devuelve 1 si se resolvio. */
int delegar_texto(Tarea t, int k, int N)
{
    sigset_t bloqueo;
    sigemptyset(&bloqueo);
    sigaddset(&bloqueo, SIGTERM);
    sigprocmask(SIG_BLOCK, &bloqueo, NULL);     /* que no nos maten a medio fork */

    n_mis_hijos = 0;
    for (int j = 0; j < k; j++) {
        int p[2];
        pipe(p);
        mis_hijos[n_mis_hijos++] = crear_meeseeks(delegado_texto, N, p);
        close(p[0]);
        enviar_tarea(p[1], t);                  /* la solicitud viaja por pipe */
        close(p[1]);
    }
    sigprocmask(SIG_UNBLOCK, &bloqueo, NULL);

    int resuelto = 0, status;
    pid_t pid;
    while ((pid = wait(&status)) > 0) {
        for (int j = 0; j < n_mis_hijos; j++)
            if (mis_hijos[j] == pid)
                mis_hijos[j] = 0;               /* ya no esta vivo */
        if (!resuelto && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            resuelto = 1;
            for (int j = 0; j < n_mis_hijos; j++)
                if (mis_hijos[j] > 0)
                    kill(mis_hijos[j], SIGTERM);
        }
    }
    return resuelto;
}

/* Cuerpo de un delegado: recibe la tarea, la intenta y si falla pide ayuda */
void delegado_texto(int nivel_padre, const int *fd)
{
    int N, i;
    n_mis_hijos = 0;
    Tarea t = recibir_tarea(fd[0]);
    close(fd[0]);
    close(fd[1]);

    presentarse(nivel_padre, &N, &i);
    snprintf(despedida, sizeof despedida, "%*s[%d] Otro Meeseeks ya lo resolvio. Adios!\n",
             (N - 1) * 2, "", getpid());
    signal(SIGTERM, al_recibir_sigterm);
    sigset_t bloqueo;
    sigemptyset(&bloqueo);
    sigaddset(&bloqueo, SIGTERM);
    sigprocmask(SIG_UNBLOCK, &bloqueo, NULL);

    int ms = 500 + rand() % 4501;               /* entre 0.5 y 5 s */
    printf("%*s[%d] I am executing request #%d (dificultad %d, %.1f s)\n",
           (N - 1) * 2, "", getpid(), t.solicitud, t.dificultad, ms / 1000.0);
    usleep(ms * 1000);

    if (t.dificultad > 85 || rand() % 100 < t.dificultad) {
        printf("%*s[%d] Listo! Solicitud #%d completada. Gracias, adios!\n",
               (N - 1) * 2, "", getpid(), t.solicitud);
        exit(0);
    }

    if (N >= NIVEL_LIMITE) {
        printf("%*s[%d] No pude, y en el nivel %d ya no puedo pedir ayuda. Adios!\n",
               (N - 1) * 2, "", getpid(), N);
        exit(1);
    }

    int k = ayudantes_necesarios(t.dificultad);
    t.dificultad = diluir(t.dificultad, k);
    printf("%*s[%d] No pude! Invoco %d ayudantes (dificultad %d)\n",
           (N - 1) * 2, "", getpid(), k, t.dificultad);
    int ok = delegar_texto(t, k, N);
    if (ok)
        printf("%*s[%d] Mis ayudantes lo lograron. Adios!\n", (N - 1) * 2, "", getpid());
    else
        printf("%*s[%d] Ni mis ayudantes pudieron. Adios!\n", (N - 1) * 2, "", getpid());
    exit(ok ? 0 : 1);
}

int evaluar_texto(const Opcion *op, Tarea t, int N)
{
    (void)op;
    if (t.dificultad < 0) {                     /* caso 2: a la suerte del Meeseeks */
        t.dificultad = rand() % 101;
        printf("[%d] Lo dejo a mi suerte: dificultad %d\n", getpid(), t.dificultad);
    }
    int original = t.dificultad;
    int k = ayudantes_necesarios(t.dificultad);
    t.dificultad = diluir(t.dificultad, k);
    printf("[%d] Dificultad %d: delego en %d Meeseeks (dificultad %d cada uno)\n",
           getpid(), original, k, t.dificultad);
    return delegar_texto(t, k, N);
}

/* ======================================================================= */
/*  Caso B: aritmetica. fd[0] = recibe la tarea, fd[1] = envia resultado,  */
/*  fd[2] / fd[3] = pipe entre hermanos (si aplica)                        */
/* ======================================================================= */

/* Suma: un Meeseeks toma ambos numeros y los suma */
void m_sumador(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    printf("  [%d] I am executing request #%d: %d + %d\n", getpid(), t.solicitud, t.a, t.b);
    enviar_int(fd[1], t.a + t.b);
}

/* Resta: uno calcula la distancia entre los numeros ... */
void m_distancia(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int dist = t.a > t.b ? t.a - t.b : t.b - t.a;
    printf("  [%d] I am executing request #%d: distancia entre %d y %d = %d\n",
           getpid(), t.solicitud, t.a, t.b, dist);
    enviar_int(fd[1], dist);
}

/* ... y otro determina el signo */
void m_signo(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int signo = t.a >= t.b ? 1 : -1;
    printf("  [%d] I am executing request #%d: el signo de %d - %d es %c\n",
           getpid(), t.solicitud, t.a, t.b, signo > 0 ? '+' : '-');
    enviar_int(fd[1], signo);
}

/* Multiplicacion: el contador mantiene el multiplicador y le resta 1 por
 * iteracion; por cada iteracion avisa al acumulador (+1 o -1), 0 = fin */
void m_contador(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int restante = t.b < 0 ? -t.b : t.b;
    int paso = t.b < 0 ? -1 : 1;
    printf("  [%d] I am executing request #%d: cuento %d iteraciones\n",
           getpid(), t.solicitud, restante);
    while (restante > 0) {
        enviar_int(fd[3], paso);
        restante--;
    }
    enviar_int(fd[3], 0);
}

/* El acumulador suma (o resta) el multiplicando en cada iteracion */
void m_acumulador(int nivel_padre, const int *fd)
{
    int N, i, paso, acc = 0;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    printf("  [%d] I am executing request #%d: acumulo %d por iteracion\n",
           getpid(), t.solicitud, t.a);
    while ((paso = recibir_int(fd[2])) != 0)
        acc += paso > 0 ? t.a : -t.a;
    enviar_int(fd[1], acc);
}

/* Division: el divisor le pasa el divisor al dividendo y cuenta cuantas
 * veces cupo (cociente) */
void m_divisor(int nivel_padre, const int *fd)
{
    int N, i, veces = 0;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int divisor = t.b < 0 ? -t.b : t.b;
    printf("  [%d] I am executing request #%d: soy el divisor (%d), cuento las restas\n",
           getpid(), t.solicitud, divisor);
    enviar_int(fd[3], divisor);
    while (recibir_int(fd[2]) != 0)
        veces++;
    enviar_int(fd[1], veces);
}

/* El dividendo le resta el divisor mientras quepa, avisa cada resta al
 * divisor y le pasa lo que sobra al residuo. fd[4] = pipe al residuo */
void m_dividendo(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int resto = t.a < 0 ? -t.a : t.a;
    int divisor = recibir_int(fd[2]);
    printf("  [%d] I am executing request #%d: soy el dividendo (%d), resto %d mientras quepa\n",
           getpid(), t.solicitud, resto, divisor);
    while (resto >= divisor) {
        resto -= divisor;
        enviar_int(fd[3], 1);
    }
    enviar_int(fd[3], 0);
    enviar_int(fd[4], resto);
}

/* El residuo recibe lo que sobro y le pone el signo del dividendo */
void m_residuo(int nivel_padre, const int *fd)
{
    int N, i;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int resto = recibir_int(fd[2]);
    if (t.a < 0)
        resto = -resto;
    printf("  [%d] I am executing request #%d: soy el residuo, sobra %d\n",
           getpid(), t.solicitud, resto);
    enviar_int(fd[1], resto);
}

/* Crea un pipe para mandarle la tarea a un Meeseeks y otro para su respuesta */
void pipes_tarea(int bajada[2], int subida[2])
{
    pipe(bajada);
    pipe(subida);
}

int evaluar_suma(const Opcion *op, Tarea t, int N)
{
    int baj[2], sub[2];
    (void)op;
    pipes_tarea(baj, sub);
    int fd[] = { baj[0], sub[1] };
    crear_meeseeks(m_sumador, N, fd);
    enviar_tarea(baj[1], t);
    int r = recibir_int(sub[0]);
    while (wait(NULL) > 0);
    printf("[%d] Resultado: %d + %d = %d\n", getpid(), t.a, t.b, r);
    return 1;
}

int evaluar_resta(const Opcion *op, Tarea t, int N)
{
    int b1[2], s1[2], b2[2], s2[2];
    (void)op;
    pipes_tarea(b1, s1);
    pipes_tarea(b2, s2);
    int fd1[] = { b1[0], s1[1] };
    int fd2[] = { b2[0], s2[1] };
    crear_meeseeks(m_distancia, N, fd1);
    crear_meeseeks(m_signo, N, fd2);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    int dist = recibir_int(s1[0]);
    int signo = recibir_int(s2[0]);
    while (wait(NULL) > 0);
    printf("[%d] Resultado: %d - %d = %d\n", getpid(), t.a, t.b, signo * dist);
    return 1;
}

int evaluar_multiplicacion(const Opcion *op, Tarea t, int N)
{
    int b1[2], s1[2], b2[2], s2[2], entre[2];
    (void)op;
    pipes_tarea(b1, s1);
    pipes_tarea(b2, s2);
    pipe(entre);                                  /* contador -> acumulador */
    int fd_cont[] = { b1[0], s1[1], entre[0], entre[1] };
    int fd_acum[] = { b2[0], s2[1], entre[0], entre[1] };
    crear_meeseeks(m_contador, N, fd_cont);
    crear_meeseeks(m_acumulador, N, fd_acum);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    int r = recibir_int(s2[0]);
    while (wait(NULL) > 0);
    printf("[%d] Resultado: %d * %d = %d\n", getpid(), t.a, t.b, r);
    return 1;
}

int evaluar_division(const Opcion *op, Tarea t, int N)
{
    (void)op;
    if (t.b == 0) {
        printf("[%d] No se puede dividir entre cero. Lo siento!\n", getpid());
        return 0;
    }
    int b1[2], s1[2], b2[2], s2[2], b3[2], s3[2];
    int div_a_dvd[2], dvd_a_div[2], dvd_a_res[2];
    pipes_tarea(b1, s1);
    pipes_tarea(b2, s2);
    pipes_tarea(b3, s3);
    pipe(div_a_dvd);                              /* divisor  -> dividendo  */
    pipe(dvd_a_div);                              /* dividendo -> divisor   */
    pipe(dvd_a_res);                              /* dividendo -> residuo   */
    int fd_div[] = { b1[0], s1[1], dvd_a_div[0], div_a_dvd[1] };
    int fd_dvd[] = { b2[0], s2[1], div_a_dvd[0], dvd_a_div[1], dvd_a_res[1] };
    int fd_res[] = { b3[0], s3[1], dvd_a_res[0] };
    crear_meeseeks(m_divisor, N, fd_div);
    crear_meeseeks(m_dividendo, N, fd_dvd);
    crear_meeseeks(m_residuo, N, fd_res);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    enviar_tarea(b3[1], t);
    int cociente = recibir_int(s1[0]);
    int residuo = recibir_int(s3[0]);
    while (wait(NULL) > 0);
    if ((t.a < 0) != (t.b < 0))
        cociente = -cociente;
    printf("[%d] Resultado: %d / %d = %d, residuo %d\n", getpid(), t.a, t.b, cociente, residuo);
    return 1;
}

/* ======================================================================= */
/*  Logica (dificultad 90): fd[0] = recibe la tarea, fd[1] = resultado     */
/* ======================================================================= */

/* Delegado logico: trabaja un tiempo simulado y calcula la operacion */
void m_logico(int nivel_padre, const int *fd)
{
    int N, i, r;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre, &N, &i);
    int ms = 500 + rand() % 4501;
    printf("  [%d] I am executing request #%d: %s (%.1f s)\n",
           getpid(), t.solicitud, t.texto, ms / 1000.0);
    usleep(ms * 1000);
    switch (t.operacion) {
    case '&': r = t.a && t.b;   break;
    case '|': r = t.a || t.b;   break;
    case '^': r = !t.a != !t.b; break;
    default:  r = !t.a;         break;   /* '!' */
    }
    enviar_int(fd[1], r);
}

/* Crea los delegados que pide la dificultad y toma la primera respuesta */
int resolver_logica(const Opcion *op, Tarea t, int N, char operacion)
{
    int baj[2], sub[2];
    int k = ayudantes_necesarios(op->dificultad);
    t.operacion = operacion;
    pipes_tarea(baj, sub);
    int fd[] = { baj[0], sub[1] };
    printf("[%d] Dificultad %d: delego en %d Meeseeks\n", getpid(), op->dificultad, k);
    for (int j = 0; j < k; j++) {
        crear_meeseeks(m_logico, N, fd);
        enviar_tarea(baj[1], t);
    }
    int r = recibir_int(sub[0]);
    while (wait(NULL) > 0);
    if (operacion == '!')
        printf("[%d] Resultado: NO %d = %d\n", getpid(), t.a, r);
    else
        printf("[%d] Resultado: %d %s %d = %d\n", getpid(), t.a,
               operacion == '&' ? "Y" : operacion == '|' ? "O" : "XOR", t.b, r);
    return 1;
}

int evaluar_y(const Opcion *op, Tarea t, int N)   { return resolver_logica(op, t, N, '&'); }
int evaluar_o(const Opcion *op, Tarea t, int N)   { return resolver_logica(op, t, N, '|'); }
int evaluar_xor(const Opcion *op, Tarea t, int N) { return resolver_logica(op, t, N, '^'); }
int evaluar_no(const Opcion *op, Tarea t, int N)  { return resolver_logica(op, t, N, '!'); }

/* ======================================================================= */
/*  Evaluador y Box                                                        */
/* ======================================================================= */

/* Primer Meeseeks de cada solicitud: saluda, evalua y llama a la funcion
 * de la opcion elegida */
void evaluador(const Opcion *op, Tarea t)
{
    int N, i;
    presentarse(0, &N, &i);                       /* la Box es el nivel 0 */
    printf("[%d] I am evaluating request #%d: \"%s\"\n", getpid(), t.solicitud, t.texto);
    double t0 = ahora();
    int ok = op->funcion(op, t, N);
    printf("[%d] Solicitud #%d %s en %.2f s con %d Meeseeks. Adios!\n", getpid(), t.solicitud,
           ok ? "completada" : "NO completada", ahora() - t0, contadores_total());
    exit(ok ? 0 : 1);
}

/* Muestra un menu y devuelve la opcion elegida (entra a los submenus).
 * Devuelve NULL si el usuario elige 0 (salir / volver). */
const Opcion *elegir_opcion(const Menu *m, int es_principal)
{
    int id;
    while (1) {
        printf("\n===== %s (pid %d) =====\n", m->titulo, getpid());
        for (int j = 0; j < m->n; j++) {
            const Opcion *o = &m->opciones[j];
            if (o->dificultad >= 0)
                printf("%d) %s (dificultad %d)\n", o->id, o->nombre, o->dificultad);
            else
                printf("%d) %s\n", o->id, o->nombre);
        }
        printf("0) %s\n", es_principal ? "Salir" : "Volver");
        printf("Opcion: ");

        if (scanf("%d", &id) != 1 || id == 0)
            return NULL;
        for (int j = 0; j < m->n; j++) {
            const Opcion *o = &m->opciones[j];
            if (o->id != id)
                continue;
            if (o->submenu == NULL)
                return o;
            const Opcion *elegida = elegir_opcion(o->submenu, 0);
            if (elegida)
                return elegida;
            id = -1;                              /* volvio del submenu */
            break;
        }
        if (id != -1)
            printf("Opcion invalida\n");
    }
}

/* Lee un 0 o un 1 del usuario */
int leer_booleano(const char *pregunta)
{
    int v = 0;
    printf("%s (0 = falso, 1 = verdadero): ", pregunta);
    scanf("%d", &v);
    return v != 0;
}

/* Arma la tarea: copia la opcion y le pide al usuario los datos que falten */
Tarea preparar_tarea(const Opcion *op, int solicitud)
{
    Tarea t;
    memset(&t, 0, sizeof t);
    t.solicitud = solicitud;
    t.dificultad = op->dificultad;
    snprintf(t.texto, sizeof t.texto, "%s", op->nombre);

    switch (op->pide) {
    case PIDE_DOS_NUMEROS:
        printf("Primer numero: ");
        scanf("%d", &t.a);
        printf("Segundo numero: ");
        scanf("%d", &t.b);
        break;
    case PIDE_DOS_BOOLEANOS:
        t.a = leer_booleano("Primer valor");
        t.b = leer_booleano("Segundo valor");
        break;
    case PIDE_UN_BOOLEANO:
        t.a = leer_booleano("Valor");
        break;
    case PIDE_TEXTO: {
        char d[16];
        printf("Que necesitas? ");
        scanf(" %63[^\n]", t.texto);
        printf("Dificultad 0-100 (100 = muy facil) o 'r' para dejarlo a la suerte: ");
        scanf("%15s", d);
        t.dificultad = (d[0] == 'r' || d[0] == 'R') ? -1 : atoi(d);
        if (t.dificultad > 100)
            t.dificultad = 100;
        break;
    }
    }
    return t;
}

int main(void)
{
    int solicitud = 0;
    pid_t pid;

    setbuf(stdout, NULL);   /* sin buffer: fork no duplica la salida */
    contadores_crear();

    while (1) {
        const Opcion *op = elegir_opcion(&menu_principal, 1);
        if (op == NULL)
            break;

        Tarea t = preparar_tarea(op, ++solicitud);

        contadores_reiniciar();
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0)
            evaluador(op, t);                        /* hijo evaluador */

        waitpid(pid, NULL, 0);                       /* la Box espera */
    }

    printf("Adios!\n");
    contadores_destruir();
    return 0;
}
