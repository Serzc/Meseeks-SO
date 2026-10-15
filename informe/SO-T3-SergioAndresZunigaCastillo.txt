/*
 * meseeks.c - Tarea 3, Box de Mr. Meeseeks
 *
 * Instituto Tecnologico de Costa Rica, Campus Tecnologico Central Cartago
 * Escuela de Ingenieria en Computacion, II Semestre 2026
 * Curso:      IC-6600 Principios de Sistemas Operativos
 * Profesor:   Esteban Arias Mendez
 * Estudiante: Sergio Andres Zuniga Castillo, carne 2024146884
 *
 * Flujo:  Usuario -(menu)-> Box -fork()-> Meeseeks evaluador (uno por solicitud)
 *         -fork() + pipes-> Meeseeks delegados que resuelven la solicitud.
 *
 * La solicitud baja por pipe; el resultado sube por pipe. Cuando un delegado lo
 * logra, su padre avisa por pipe a los hermanos para que terminen.
 * Cada opcion del menu tiene su propia funcion, que ejecuta el evaluador.
 * Documentacion de cada funcion: meseeks.md
 *
 * Compilar: gcc -Wall -pthread -o meseeks meseeks.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_NIVELES  32
#define MAX_HIJOS    10
#define MAX_TAREAS   100
#define NIVEL_LIMITE 6     /* un Meeseeks con N >= 6 ya no puede pedir ayuda */
#define STOP         1     /* mensaje padre -> hijo: ya se resolvio, terminen */

/* ======================================================================= */
/*  Contadores compartidos (memoria compartida + semaforo)                 */
/* ======================================================================= */

/* instancias[N] = cuantos Meeseeks se han creado en el nivel N.
 * El semaforo mutex protege el acceso a los contadores. */
typedef struct {
    sem_t mutex;
    int nivel_max;
    int instancias[MAX_NIVELES + 1];
    double espera;     /* segundos simulados en la ruta que resolvio la solicitud */
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
    cont->espera = 0;
    for (int n = 0; n <= MAX_NIVELES; n++)
        cont->instancias[n] = 0;
    sem_post(&cont->mutex);
}

/* Registra un Meeseeks nuevo: su N es el nivel del padre + 1.
 * Seccion critica con el semaforo. Devuelve N. */
int registrar_meeseeks(int nivel_padre)
{
    int N = nivel_padre + 1;
    if (N > MAX_NIVELES)
        N = MAX_NIVELES;
    sem_wait(&cont->mutex);
    cont->instancias[N]++;
    if (N > cont->nivel_max)
        cont->nivel_max = N;
    sem_post(&cont->mutex);
    return N;
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

/* El evaluador anota cuanto se espero en la simulacion (para el tiempo real) */
void reportar_espera(double segundos)
{
    sem_wait(&cont->mutex);
    cont->espera = segundos;
    sem_post(&cont->mutex);
}

double espera_reportada(void)
{
    sem_wait(&cont->mutex);
    double e = cont->espera;
    sem_post(&cont->mutex);
    return e;
}

void contadores_destruir(void)
{
    sem_destroy(&cont->mutex);
    munmap(cont, sizeof(Contadores));
}

/* ======================================================================= */
/*  Tipos, opciones del menu y utilidades                                  */
/* ======================================================================= */

/* Lo que viaja por el pipe del evaluador a sus delegados */
typedef struct {
    int solicitud;      /* numero de solicitud (#k)                     */
    char texto[64];     /* lo que se pidio                              */
    int dificultad;     /* 0..100, 100 = muy facil; -1 = a la suerte    */
    int duracion_ms;    /* duracion de cada intento; 0 = al azar        */
    int a, b;           /* operandos (aritmetica y logica)              */
    char operacion;     /* operacion logica: '&', '|', '^', '!'         */
    char comando[128];  /* programa por ejecutar                        */
} Tarea;

/* Lo que sube por el pipe de un hijo a su padre */
typedef struct {
    int resuelto;        /* 1 = alguien lo logro                          */
    int pid, ppid, N, i; /* quien lo logro                                */
    double espera;       /* s simulados en la ruta del evaluador al que lo logro */
} Resultado;

/* Que datos le pide la Box al usuario antes de crear al evaluador */
enum { PIDE_NADA, PIDE_DOS_NUMEROS, PIDE_DOS_BOOLEANOS, PIDE_UN_BOOLEANO, PIDE_TEXTO,
       PIDE_COMANDO, PIDE_CONFIG };

typedef struct Opcion Opcion;
typedef struct Menu Menu;

struct Opcion {
    int id;
    const char *nombre;
    int dificultad;                               /* -1 si no aplica          */
    int pide;                                     /* ver enum PIDE_*          */
    int (*funcion)(const Opcion *op, Tarea t);    /* la ejecuta el evaluador;
                                                     1 = completada           */
    const Menu *submenu;                          /* != NULL: abre un submenu */
};

struct Menu {
    const char *titulo;
    const Opcion *opciones;
    int n;
};

int evaluar_texto(const Opcion *op, Tarea t);
int evaluar_suma(const Opcion *op, Tarea t);
int evaluar_resta(const Opcion *op, Tarea t);
int evaluar_multiplicacion(const Opcion *op, Tarea t);
int evaluar_division(const Opcion *op, Tarea t);
int evaluar_y(const Opcion *op, Tarea t);
int evaluar_o(const Opcion *op, Tarea t);
int evaluar_xor(const Opcion *op, Tarea t);
int evaluar_no(const Opcion *op, Tarea t);
int evaluar_fecha(const Opcion *op, Tarea t);
int evaluar_listar(const Opcion *op, Tarea t);
int evaluar_falla(const Opcion *op, Tarea t);
int evaluar_comando(const Opcion *op, Tarea t);

#define CANTIDAD(arr) (int)(sizeof(arr) / sizeof(arr[0]))

/* Submenu general: solicitudes de texto (dificultad: 100 = muy facil, 0 = imposible) */
static const Opcion opciones_general[] = {
    { 1, "Encontrar las llaves de la casa",   100, PIDE_NADA,  evaluar_texto, NULL },
    { 2, "Hacer el sandwich perfecto",         80, PIDE_NADA,  evaluar_texto, NULL },
    { 3, "Aprender a tocar guitarra",          60, PIDE_NADA,  evaluar_texto, NULL },
    { 4, "Ganar un torneo de ajedrez",         40, PIDE_NADA,  evaluar_texto, NULL },
    { 5, "Terminar la tarea de SO a tiempo",   20, PIDE_NADA,  evaluar_texto, NULL },
    { 6, "Solicitud libre",                    -1, PIDE_TEXTO, evaluar_texto, NULL },
    { 7, "Que Jerry deje de ser Jerry (imposible)", 0, PIDE_NADA, evaluar_texto, NULL },
};
static const Menu menu_general = { "General", opciones_general, CANTIDAD(opciones_general) };

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

/* Submenu de programas externos (fork + exec) */
static const Opcion opciones_programas[] = {
    { 1, "Mostrar la fecha (date)",          -1, PIDE_NADA,    evaluar_fecha,   NULL },
    { 2, "Listar esta carpeta (ls -l)",      -1, PIDE_NADA,    evaluar_listar,  NULL },
    { 3, "Programa que siempre falla (false)", -1, PIDE_NADA,  evaluar_falla,   NULL },
    { 4, "Escribir un comando",              -1, PIDE_COMANDO, evaluar_comando, NULL },
};
static const Menu menu_programas = { "Programas", opciones_programas, CANTIDAD(opciones_programas) };

/* Menu principal */
static const Opcion opciones_principal[] = {
    { 1, "General...",       -1, PIDE_NADA,   NULL, &menu_general },
    { 2, "Aritmetica...",    -1, PIDE_NADA,   NULL, &menu_aritmetica },
    { 3, "Logica...",        -1, PIDE_NADA,   NULL, &menu_logica },
    { 4, "Programas...",     -1, PIDE_NADA,   NULL, &menu_programas },
    { 5, "Configuracion...", -1, PIDE_CONFIG, NULL, NULL },
};
static const Menu menu_principal = { "Meeseeks Box", opciones_principal, CANTIDAD(opciones_principal) };

/* Identidad del proceso actual (cada proceso tiene su propia copia) */
static int mi_nivel = 0;      /* N (0 = la Box)            */
static int mi_instancia = 0;  /* i: numero de hijo de su padre */

double ahora(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void enviar_int(int fd, int x)     { write(fd, &x, sizeof x); }
int  recibir_int(int fd)           { int x = 0; read(fd, &x, sizeof x); return x; }
void enviar_tarea(int fd, Tarea t) { write(fd, &t, sizeof t); }
Tarea recibir_tarea(int fd)        { Tarea t; memset(&t, 0, sizeof t); read(fd, &t, sizeof t); return t; }
void enviar_resultado(int fd, Resultado r) { write(fd, &r, sizeof r); }
void enviar_double(int fd, double x) { write(fd, &x, sizeof x); }
double recibir_double(int fd)      { double x = 0; read(fd, &x, sizeof x); return x; }

/* Mensaje de un Meeseeks: siempre con su pid y ppid, sangrado segun su nivel */
void decir(const char *fmt, ...)
{
    char msg[512], linea[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    int n = snprintf(linea, sizeof linea, "%*s[pid %d | ppid %d] %s\n",
                     (mi_nivel > 1 ? mi_nivel - 1 : 0) * 2, "", getpid(), getppid(), msg);
    write(STDOUT_FILENO, linea, n);
}

/* Todo Meeseeks llama a esta funcion al nacer: se registra (N) y saluda */
void presentarse(int nivel_padre)
{
    mi_nivel = registrar_meeseeks(nivel_padre);
    srand(getpid() ^ time(NULL));
    decir("Hi, I'm Mr. Meeseeks! Look at Meeeee. (%d, %d, %d, %d)",
          getpid(), getppid(), mi_nivel, mi_instancia);
}

/* Crea un Meeseeks hijo numero 'instancia' (su i) que ejecuta 'cuerpo' con
 * los descriptores 'fd'. Devuelve el pid del hijo. */
pid_t crear_meeseeks(void (*cuerpo)(int nivel_padre, const int *fd),
                     int nivel_padre, int instancia, const int *fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        mi_instancia = instancia;
        cuerpo(nivel_padre, fd);
        exit(0);
    }
    return pid;
}

/* Crea el pipe para mandar la tarea a un Meeseeks y el de su respuesta */
void pipes_tarea(int bajada[2], int subida[2])
{
    pipe(bajada);
    pipe(subida);
}

/* ======================================================================= */
/*  General: solicitudes de texto (simuladas)                              */
/* ======================================================================= */

/* Un hijo visto desde su padre */
typedef struct {
    pid_t pid;
    int baj;     /* para mandarle la tarea y el STOP */
    int sub;     /* para recibir su Resultado        */
    int vivo;    /* todavia no ha respondido         */
} Hijo;

/* Ayudantes segun las instrucciones */
int ayudantes_necesarios(int d)
{
    if (d > 85) return 1;
    if (d > 45) return 2;
    return 5;
}

/* Al repartir la tarea entre k ayudantes, a cada uno le toca mas facil */
int diluir(int d, int k)
{
    if (d == 0)
        return 0;                  /* lo imposible sigue siendo imposible */
    return d + (100 - d) / (k * k);
}

void delegado_texto(int nivel_padre, const int *fd);

/* Crea k delegados (i = 1..k) y le manda la tarea a cada uno por su pipe */
void crear_delegados(Tarea t, int k, Hijo h[])
{
    for (int j = 0; j < k; j++) {
        int baj[2], sub[2];
        pipes_tarea(baj, sub);
        int fd[] = { baj[0], sub[1], baj[1], sub[0] };
        h[j].pid = crear_meeseeks(delegado_texto, mi_nivel, j + 1, fd);
        close(baj[0]);
        close(sub[1]);
        h[j].baj = baj[1];
        h[j].sub = sub[0];
        h[j].vivo = 1;
        enviar_tarea(h[j].baj, t);            /* la solicitud viaja por pipe */
    }
}

/* Le avisa por pipe a los hijos que siguen trabajando (menos 'excepto') */
void detener_hijos(Hijo h[], int k, int excepto)
{
    for (int j = 0; j < k; j++)
        if (j != excepto && h[j].vivo)
            enviar_int(h[j].baj, STOP);
}

/* Espera a que algun hijo resuelva o a que todos fallen. Si fd_padre >= 0
 * tambien escucha al padre: si manda STOP, detiene a todos y *detenido = 1.
 * Al final espera (waitpid) a todos los hijos. */
Resultado supervisar(Hijo h[], int k, int fd_padre, int *detenido)
{
    Resultado r;
    struct pollfd pf[MAX_HIJOS + 1];
    int pendientes = k;

    memset(&r, 0, sizeof r);
    *detenido = 0;
    pf[0].fd = fd_padre;                      /* -1: poll lo ignora */
    pf[0].events = POLLIN;
    for (int j = 0; j < k; j++) {
        pf[j + 1].fd = h[j].sub;
        pf[j + 1].events = POLLIN;
    }

    while (pendientes > 0) {
        if (poll(pf, k + 1, -1) < 0)
            continue;
        if (fd_padre >= 0 && pf[0].revents) {     /* el padre ordena parar */
            recibir_int(fd_padre);
            *detenido = 1;
            detener_hijos(h, k, -1);
            break;
        }
        for (int j = 0; j < k; j++) {
            if (!h[j].vivo || !pf[j + 1].revents)
                continue;
            Resultado m;
            int n = read(h[j].sub, &m, sizeof m);
            h[j].vivo = 0;
            pf[j + 1].fd = -1;
            pendientes--;
            if (n == (int)sizeof m && m.resuelto) {
                r = m;                            /* alguien lo logro */
                detener_hijos(h, k, j);           /* avisar a los hermanos */
                pendientes = 0;
                break;
            }
        }
    }
    for (int j = 0; j < k; j++) {
        waitpid(h[j].pid, NULL, 0);
        close(h[j].baj);
        close(h[j].sub);
    }
    return r;
}

/* Cuerpo de un delegado: recibe la tarea, la intenta y si falla pide ayuda.
 * fd[0] = de su padre, fd[1] = a su padre. */
void delegado_texto(int nivel_padre, const int *fd)
{
    Resultado r;
    int detenido;
    close(fd[2]);
    close(fd[3]);
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    memset(&r, 0, sizeof r);

    int ms = t.duracion_ms > 0 ? t.duracion_ms : 500 + rand() % 4501;
    decir("I am executing request #%d (dificultad %d, %.1f s)", t.solicitud, t.dificultad, ms / 1000.0);

    /* Trabaja, pero atento por si su padre le avisa que ya se resolvio */
    struct pollfd pf = { fd[0], POLLIN, 0 };
    double t0 = ahora();
    if (poll(&pf, 1, ms) > 0) {
        recibir_int(fd[0]);
        decir("Otro Meeseeks ya lo resolvio. Adios!");
        exit(0);
    }
    double mi_espera = ahora() - t0;          /* tiempo simulado de este intento */

    if (t.dificultad > 85 || rand() % 100 < t.dificultad) {
        r.resuelto = 1;
        r.pid = getpid();
        r.ppid = getppid();
        r.N = mi_nivel;
        r.i = mi_instancia;
        r.espera = mi_espera;
        decir("Listo! Solicitud #%d completada. Gracias por dejarme servirle. Adios!", t.solicitud);
        enviar_resultado(fd[1], r);
        exit(0);
    }

    if (mi_nivel >= NIVEL_LIMITE) {
        decir("No pude, y en el nivel %d ya no puedo pedir ayuda. Adios!", mi_nivel);
        enviar_resultado(fd[1], r);
        exit(0);
    }

    Hijo h[MAX_HIJOS];
    int k = ayudantes_necesarios(t.dificultad);
    t.dificultad = diluir(t.dificultad, k);
    decir("No pude! Invoco %d ayudantes (dificultad %d)", k, t.dificultad);
    crear_delegados(t, k, h);
    r = supervisar(h, k, fd[0], &detenido);

    if (detenido)
        decir("Otro Meeseeks ya lo resolvio. Adios!");
    else if (r.resuelto)
        decir("Mi ayudante (%d, %d, %d, %d) lo logro, le aviso a mi padre. Adios!", r.pid, r.ppid, r.N, r.i);
    else
        decir("Ni mis ayudantes pudieron. Adios!");
    r.espera += mi_espera;                    /* la ruta pasa por este intento */
    if (!detenido)
        enviar_resultado(fd[1], r);
    exit(0);
}

int evaluar_texto(const Opcion *op, Tarea t)
{
    Hijo h[MAX_HIJOS];
    int detenido;
    (void)op;
    if (t.dificultad < 0) {                     /* caso 2: a la suerte del Meeseeks */
        t.dificultad = rand() % 101;
        decir("Lo dejo a mi suerte: dificultad %d", t.dificultad);
    }
    int original = t.dificultad;
    int k = ayudantes_necesarios(t.dificultad);
    t.dificultad = diluir(t.dificultad, k);
    decir("Dificultad %d: delego en %d Meeseeks (dificultad %d cada uno)", original, k, t.dificultad);

    crear_delegados(t, k, h);
    Resultado r = supervisar(h, k, -1, &detenido);
    reportar_espera(r.resuelto ? r.espera : -1);   /* -1: no hubo ruta ganadora */
    if (r.resuelto)
        decir("La solicitud la resolvio el Mr. Meeseeks (pid %d, ppid %d, N=%d, i=%d)",
              r.pid, r.ppid, r.N, r.i);
    else
        decir("Ningun Meeseeks pudo con la solicitud.");
    return r.resuelto;
}

/* ======================================================================= */
/*  Aritmetica. fd[0] = recibe la tarea, fd[1] = envia resultado,          */
/*  fd[2] / fd[3] (y fd[4]) = pipes entre hermanos (si aplica)             */
/* ======================================================================= */

/* Suma: un Meeseeks toma ambos numeros y los suma */
void m_sumador(int nivel_padre, const int *fd)
{
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    decir("I am executing request #%d: %d + %d", t.solicitud, t.a, t.b);
    enviar_int(fd[1], t.a + t.b);
}

/* Resta: uno calcula la distancia entre los numeros ... */
void m_distancia(int nivel_padre, const int *fd)
{
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int dist = t.a > t.b ? t.a - t.b : t.b - t.a;
    decir("I am executing request #%d: distancia entre %d y %d = %d", t.solicitud, t.a, t.b, dist);
    enviar_int(fd[1], dist);
}

/* ... y otro determina el signo */
void m_signo(int nivel_padre, const int *fd)
{
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int signo = t.a >= t.b ? 1 : -1;
    decir("I am executing request #%d: el signo de %d - %d es %c",
          t.solicitud, t.a, t.b, signo > 0 ? '+' : '-');
    enviar_int(fd[1], signo);
}

/* Multiplicacion: el contador mantiene el multiplicador y le resta 1 por
 * iteracion; por cada iteracion avisa al acumulador (+1 o -1), 0 = fin */
void m_contador(int nivel_padre, const int *fd)
{
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int restante = t.b < 0 ? -t.b : t.b;
    int paso = t.b < 0 ? -1 : 1;
    decir("I am executing request #%d: cuento %d iteraciones", t.solicitud, restante);
    while (restante > 0) {
        enviar_int(fd[3], paso);
        restante--;
    }
    enviar_int(fd[3], 0);
}

/* El acumulador suma (o resta) el multiplicando en cada iteracion */
void m_acumulador(int nivel_padre, const int *fd)
{
    int paso, acc = 0;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    decir("I am executing request #%d: acumulo %d por iteracion", t.solicitud, t.a);
    while ((paso = recibir_int(fd[2])) != 0)
        acc += paso > 0 ? t.a : -t.a;
    enviar_int(fd[1], acc);
}

/* Division: el divisor le pasa el divisor al dividendo y cuenta cuantas
 * veces cupo (cociente) */
void m_divisor(int nivel_padre, const int *fd)
{
    int veces = 0;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int divisor = t.b < 0 ? -t.b : t.b;
    decir("I am executing request #%d: soy el divisor (%d), cuento las restas", t.solicitud, divisor);
    enviar_int(fd[3], divisor);
    while (recibir_int(fd[2]) != 0)
        veces++;
    enviar_int(fd[1], veces);
}

/* El dividendo le resta el divisor mientras quepa, avisa cada resta al
 * divisor y le pasa lo que sobra al residuo. fd[4] = pipe al residuo */
void m_dividendo(int nivel_padre, const int *fd)
{
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int resto = t.a < 0 ? -t.a : t.a;
    int divisor = recibir_int(fd[2]);
    decir("I am executing request #%d: soy el dividendo (%d), resto %d mientras quepa",
          t.solicitud, resto, divisor);
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
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int resto = recibir_int(fd[2]);
    if (t.a < 0)
        resto = -resto;
    decir("I am executing request #%d: soy el residuo, sobra %d", t.solicitud, resto);
    enviar_int(fd[1], resto);
}

int evaluar_suma(const Opcion *op, Tarea t)
{
    int baj[2], sub[2];
    (void)op;
    pipes_tarea(baj, sub);
    int fd[] = { baj[0], sub[1] };
    crear_meeseeks(m_sumador, mi_nivel, 1, fd);
    enviar_tarea(baj[1], t);
    int r = recibir_int(sub[0]);
    while (wait(NULL) > 0);
    decir("Resultado: %d + %d = %d", t.a, t.b, r);
    return 1;
}

int evaluar_resta(const Opcion *op, Tarea t)
{
    int b1[2], s1[2], b2[2], s2[2];
    (void)op;
    pipes_tarea(b1, s1);
    pipes_tarea(b2, s2);
    int fd1[] = { b1[0], s1[1] };
    int fd2[] = { b2[0], s2[1] };
    crear_meeseeks(m_distancia, mi_nivel, 1, fd1);
    crear_meeseeks(m_signo, mi_nivel, 2, fd2);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    int dist = recibir_int(s1[0]);
    int signo = recibir_int(s2[0]);
    while (wait(NULL) > 0);
    decir("Resultado: %d - %d = %d", t.a, t.b, signo * dist);
    return 1;
}

int evaluar_multiplicacion(const Opcion *op, Tarea t)
{
    int b1[2], s1[2], b2[2], s2[2], entre[2];
    (void)op;
    pipes_tarea(b1, s1);
    pipes_tarea(b2, s2);
    pipe(entre);                                  /* contador -> acumulador */
    int fd_cont[] = { b1[0], s1[1], entre[0], entre[1] };
    int fd_acum[] = { b2[0], s2[1], entre[0], entre[1] };
    crear_meeseeks(m_contador, mi_nivel, 1, fd_cont);
    crear_meeseeks(m_acumulador, mi_nivel, 2, fd_acum);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    int r = recibir_int(s2[0]);
    while (wait(NULL) > 0);
    decir("Resultado: %d * %d = %d", t.a, t.b, r);
    return 1;
}

int evaluar_division(const Opcion *op, Tarea t)
{
    (void)op;
    if (t.b == 0) {
        decir("No se puede dividir entre cero. Lo siento!");
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
    crear_meeseeks(m_divisor, mi_nivel, 1, fd_div);
    crear_meeseeks(m_dividendo, mi_nivel, 2, fd_dvd);
    crear_meeseeks(m_residuo, mi_nivel, 3, fd_res);
    enviar_tarea(b1[1], t);
    enviar_tarea(b2[1], t);
    enviar_tarea(b3[1], t);
    int cociente = recibir_int(s1[0]);
    int residuo = recibir_int(s3[0]);
    while (wait(NULL) > 0);
    if ((t.a < 0) != (t.b < 0))
        cociente = -cociente;
    decir("Resultado: %d / %d = %d, residuo %d", t.a, t.b, cociente, residuo);
    return 1;
}

/* ======================================================================= */
/*  Logica (dificultad 90): fd[0] = recibe la tarea, fd[1] = resultado     */
/* ======================================================================= */

/* Delegado logico: trabaja un tiempo simulado y calcula la operacion */
void m_logico(int nivel_padre, const int *fd)
{
    int r;
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    int ms = t.duracion_ms > 0 ? t.duracion_ms : 500 + rand() % 4501;
    decir("I am executing request #%d: %s (%.1f s)", t.solicitud, t.texto, ms / 1000.0);
    double t0 = ahora();
    usleep(ms * 1000);
    double espera = ahora() - t0;
    switch (t.operacion) {
    case '&': r = t.a && t.b;   break;
    case '|': r = t.a || t.b;   break;
    case '^': r = !t.a != !t.b; break;
    default:  r = !t.a;         break;   /* '!' */
    }
    enviar_int(fd[1], r);
    enviar_double(fd[1], espera);
}

/* Crea los delegados que pide la dificultad y toma la primera respuesta */
int resolver_logica(const Opcion *op, Tarea t, char operacion)
{
    int baj[2], sub[2];
    int k = ayudantes_necesarios(op->dificultad);
    t.operacion = operacion;
    pipes_tarea(baj, sub);
    int fd[] = { baj[0], sub[1] };
    decir("Dificultad %d: delego en %d Meeseeks", op->dificultad, k);
    for (int j = 0; j < k; j++) {
        crear_meeseeks(m_logico, mi_nivel, j + 1, fd);
        enviar_tarea(baj[1], t);
    }
    int r = recibir_int(sub[0]);
    reportar_espera(recibir_double(sub[0]));
    while (wait(NULL) > 0);
    if (operacion == '!')
        decir("Resultado: NO %d = %d", t.a, r);
    else
        decir("Resultado: %d %s %d = %d", t.a,
              operacion == '&' ? "Y" : operacion == '|' ? "O" : "XOR", t.b, r);
    return 1;
}

int evaluar_y(const Opcion *op, Tarea t)   { return resolver_logica(op, t, '&'); }
int evaluar_o(const Opcion *op, Tarea t)   { return resolver_logica(op, t, '|'); }
int evaluar_xor(const Opcion *op, Tarea t) { return resolver_logica(op, t, '^'); }
int evaluar_no(const Opcion *op, Tarea t)  { return resolver_logica(op, t, '!'); }

/* ======================================================================= */
/*  Programas externos: fork + exec. fd[0] = tarea, fd[1] = codigo salida  */
/* ======================================================================= */

/* Separa el comando en argumentos (por espacios). Devuelve cuantos hay. */
int separar_argumentos(char *comando, char *argv[], int max)
{
    int argc = 0;
    char *parte = strtok(comando, " \t");
    while (parte && argc < max - 1) {
        argv[argc++] = parte;
        parte = strtok(NULL, " \t");
    }
    argv[argc] = NULL;
    return argc;
}

/* Ejecuta el programa con exec en un proceso nuevo y reporta su codigo de salida */
void m_ejecutor(int nivel_padre, const int *fd)
{
    char *argv[16];
    Tarea t = recibir_tarea(fd[0]);
    presentarse(nivel_padre);
    decir("I am executing request #%d: ejecuto \"%s\"", t.solicitud, t.comando);
    separar_argumentos(t.comando, argv, 16);

    double t0 = ahora();
    pid_t pid = fork();
    if (pid == 0) {
        int nulo = open("/dev/null", O_RDONLY);   /* el programa no lee de la terminal */
        dup2(nulo, STDIN_FILENO);
        signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv);
        perror(argv[0]);                          /* solo llega aqui si exec fallo */
        _exit(127);
    }
    int status, codigo;
    waitpid(pid, &status, 0);
    codigo = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    decir("El programa termino con codigo %d en %.3f s", codigo, ahora() - t0);
    enviar_int(fd[1], codigo);
}

/* El evaluador delega la ejecucion en un Meeseeks: completada si sale con 0 */
int ejecutar_programa(Tarea t, const char *comando)
{
    int baj[2], sub[2];
    if (comando)
        snprintf(t.comando, sizeof t.comando, "%s", comando);
    if (t.comando[0] == '\0') {
        decir("No hay ningun comando que ejecutar.");
        return 0;
    }
    pipes_tarea(baj, sub);
    int fd[] = { baj[0], sub[1] };
    crear_meeseeks(m_ejecutor, mi_nivel, 1, fd);
    enviar_tarea(baj[1], t);
    int codigo = recibir_int(sub[0]);
    while (wait(NULL) > 0);
    if (codigo == 0)
        decir("Codigo 0: el programa termino bien.");
    else
        decir("Codigo %d: el programa fallo.", codigo);
    return codigo == 0;
}

int evaluar_fecha(const Opcion *op, Tarea t)   { (void)op; return ejecutar_programa(t, "date"); }
int evaluar_listar(const Opcion *op, Tarea t)  { (void)op; return ejecutar_programa(t, "ls -l"); }
int evaluar_falla(const Opcion *op, Tarea t)   { (void)op; return ejecutar_programa(t, "false"); }
int evaluar_comando(const Opcion *op, Tarea t) { (void)op; return ejecutar_programa(t, NULL); }

/* ======================================================================= */
/*  Evaluador                                                              */
/* ======================================================================= */

/* Primer Meeseeks de cada solicitud (N=1, i=1): saluda, evalua y llama a la
 * funcion de la opcion elegida. Sale con 0 si la solicitud se completo. */
void evaluador(const Opcion *op, Tarea t)
{
    setpgid(0, 0);                  /* grupo propio: la Box puede terminarlo todo */
    mi_instancia = 1;
    presentarse(0);                 /* la Box es el nivel 0 */
    decir("I am evaluating request #%d: \"%s\"", t.solicitud, t.texto);
    int ok = op->funcion(op, t);
    decir("Solicitud #%d %s. Adios!", t.solicitud, ok ? "completada" : "NO completada");
    exit(ok ? 0 : 1);
}

/* ======================================================================= */
/*  Box: configuracion, tiempo maximo, informe y menu                      */
/* ======================================================================= */

static int tiempo_maximo = 300;      /* segundos antes del caos planetario     */
static int duracion_intento_ms = 0;  /* 0 = al azar entre 0.5 y 5 s           */
static int caos_termina = 1;         /* 1 = terminar todo; 0 = avisar y seguir */

/* Lo que la Box recuerda de cada solicitud para el informe */
typedef struct {
    int solicitud;
    char nombre[64];
    double segundos;    /* tiempo total                         */
    double real;        /* tiempo total sin las esperas simuladas */
    int completada;
    int meeseeks;
    int caos;
} Registro;

static Registro registros[MAX_TAREAS];
static int n_registros = 0;

static volatile sig_atomic_t tiempo_agotado = 0;

/* Llega SIGALRM: se cumplio el tiempo maximo de la solicitud */
void al_vencer_tiempo(int sig)
{
    (void)sig;
    tiempo_agotado = 1;
}

/* Espera al evaluador vigilando el tiempo maximo con alarm(). Si se acaba y
 * caos_termina esta activo, termina a todo el grupo de procesos (*caos = 1). */
int esperar_evaluador(pid_t pid, int *caos)
{
    struct sigaction sa;
    int status = 0;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = al_vencer_tiempo;           /* sin SA_RESTART: waitpid se interrumpe */
    sigaction(SIGALRM, &sa, NULL);
    tiempo_agotado = 0;
    *caos = 0;
    alarm(tiempo_maximo);

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR || !tiempo_agotado)
            continue;
        tiempo_agotado = 0;
        if (caos_termina) {
            printf("\n*** CAOS PLANETARIO *** Se superaron los %d s. La Box termina a todos los Meeseeks.\n",
                   tiempo_maximo);
            kill(-pid, SIGKILL);                  /* todo el grupo del evaluador */
            waitpid(pid, &status, 0);
            sem_init(&cont->mutex, 1, 1);         /* alguien pudo morir con el semaforo tomado */
            *caos = 1;
            break;
        }
        printf("\n[Box] Se superaron los %d s; sigo esperando a los Meeseeks...\n", tiempo_maximo);
    }
    alarm(0);
    return status;
}

/* Informe de la Box: todas las solicitudes atendidas */
void informe(void)
{
    int completadas = 0;
    for (int j = 0; j < n_registros; j++)
        completadas += registros[j].completada;
    printf("\n=================== INFORME DE LA MEESEEKS BOX ===================\n");
    printf("Solicitudes: %d   Completadas: %d   No completadas: %d\n\n",
           n_registros, completadas, n_registros - completadas);
    printf("%-4s %-36s %9s %9s  %-14s %8s\n", "#", "Solicitud", "T.total", "T.real", "Estado", "Meeseeks");
    for (int j = 0; j < n_registros; j++) {
        Registro *r = &registros[j];
        char real[16];
        if (r->real < 0)
            snprintf(real, sizeof real, "%9s", "-");
        else
            snprintf(real, sizeof real, "%8.4fs", r->real);
        printf("%-4d %-36.36s %8.4fs %s  %-14s %8d\n", r->solicitud, r->nombre, r->segundos,
               real, r->caos ? "caos" : r->completada ? "completada" : "no completada", r->meeseeks);
    }
    printf("T.real = tiempo total sin las esperas simuladas de los intentos (- = no hubo ruta ganadora)\n");
    printf("==================================================================\n");
}

/* Submenu de configuracion de la Box */
void configurar(void)
{
    int op, v;
    while (1) {
        printf("\n===== Configuracion =====\n");
        printf("1) Tiempo maximo por solicitud: %d s\n", tiempo_maximo);
        if (duracion_intento_ms > 0)
            printf("2) Duracion de cada intento: %.1f s\n", duracion_intento_ms / 1000.0);
        else
            printf("2) Duracion de cada intento: al azar (0.5 a 5 s)\n");
        printf("3) Al superar el tiempo maximo: %s\n",
               caos_termina ? "caos planetario (terminar todo)" : "solo avisar y seguir esperando");
        printf("0) Volver\nOpcion: ");
        if (scanf("%d", &op) != 1 || op == 0)
            return;
        if (op == 1) {
            printf("Tiempo maximo en segundos: ");
            if (scanf("%d", &v) == 1 && v > 0)
                tiempo_maximo = v;
        } else if (op == 2) {
            double s;
            printf("Duracion en segundos (0 = al azar): ");
            if (scanf("%lf", &s) == 1 && s >= 0)
                duracion_intento_ms = (int)(s * 1000);
        } else if (op == 3) {
            caos_termina = !caos_termina;
        }
    }
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
    t.duracion_ms = duracion_intento_ms;
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
    case PIDE_COMANDO:
        printf("Comando (ej. echo hola): ");
        scanf(" %127[^\n]", t.comando);
        snprintf(t.texto, sizeof t.texto, "Ejecutar: %.50s", t.comando);
        break;
    }
    return t;
}

int main(void)
{
    int solicitud = 0;
    pid_t pid;

    setbuf(stdout, NULL);       /* sin buffer: fork no duplica la salida */
    signal(SIGPIPE, SIG_IGN);   /* escribir a un pipe de alguien que ya termino no mata */
    contadores_crear();

    while (1) {
        const Opcion *op = elegir_opcion(&menu_principal, 1);
        if (op == NULL)
            break;
        if (op->pide == PIDE_CONFIG) {
            configurar();
            continue;
        }

        Tarea t = preparar_tarea(op, ++solicitud);

        contadores_reiniciar();
        double t0 = ahora();
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid == 0)
            evaluador(op, t);                        /* hijo evaluador */
        setpgid(pid, pid);

        int caos;
        int status = esperar_evaluador(pid, &caos);

        if (n_registros < MAX_TAREAS) {
            Registro *r = &registros[n_registros++];
            r->solicitud = t.solicitud;
            snprintf(r->nombre, sizeof r->nombre, "%s", t.texto);
            r->segundos = ahora() - t0;
            double espera = espera_reportada();
            r->real = (caos || espera < 0) ? -1 : r->segundos - espera;
            r->caos = caos;
            r->completada = !caos && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            r->meeseeks = contadores_total();
        }
        if (caos) {
            printf("El caos planetario obliga a cerrar la Meeseeks Box.\n");
            informe();
            contadores_destruir();
            return 1;
        }
    }

    printf("\nAdios!\n");
    informe();
    contadores_destruir();
    return 0;
}
