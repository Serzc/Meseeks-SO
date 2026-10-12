# meseeks.c: funciones

Flujo general:

```
Usuario --menu--> Box --fork()--> Evaluador (N=1, uno por solicitud)
                                     --fork() + pipe--> Delegados (N=2, 3, ...)
```

Compilar: `gcc -Wall -pthread meseeks.c -o program`

## Menús

```
Meeseeks Box                     Aritmetica...          Logica... (dificultad 90)
1-5) solicitudes de texto        1) Sumar               1) Y (AND)
6)   Solicitud libre             2) Restar              2) O (OR)
7)   Tarea imposible (dif. 0)    3) Multiplicar         3) O exclusivo (XOR)
8)   Aritmetica...               4) Dividir             4) NO (NOT)
9)   Logica...                   0) Volver              0) Volver
0)   Salir
```

Cada fila de un menú es un `Opcion`: `id`, `nombre`, `dificultad` (100 = muy fácil, 0 = imposible, -1 = no aplica), `pide` (qué datos se le piden al usuario), `funcion` (la ejecuta el evaluador y devuelve 1 si la solicitud se completó) y `submenu` (si no es NULL, la opción abre ese submenú).
Un `Menu` agrupa un título y su lista de opciones.

La **tarea imposible** es la misma función que la solicitud libre (`evaluar_texto`), solo que con dificultad 0.

## Datos que viajan por los pipes

`Tarea`: número de solicitud, texto pedido, dificultad, operandos `a` y `b`, y la operación lógica (`&`, `|`, `^`, `!`).

## Contadores compartidos (N e i)

Viven en memoria compartida (`mmap`) y los protege un semáforo POSIX (`sem_t mutex`).

| Función | Qué hace |
|---|---|
| `contadores_crear()` | Crea la memoria compartida y el semáforo. Se llama una vez en `main`, antes de cualquier `fork()`. |
| `contadores_reiniciar()` | Pone en cero los contadores antes de cada solicitud. |
| `asignar_nivel_instancia(nivel_padre, &N, &i)` | Sección crítica: N = nivel del padre + 1, i = siguiente instancia de ese nivel. |
| `contadores_total()` | Suma cuántos Meeseeks se crearon en la solicitud actual. |
| `contadores_destruir()` | Libera el semáforo y la memoria al salir. |

## Utilidades

| Función | Qué hace |
|---|---|
| `ahora()` | Segundos de reloj monotónico, para medir cuánto duró una solicitud. |
| `enviar_int` / `recibir_int` | Escriben o leen un `int` por un pipe. |
| `enviar_tarea` / `recibir_tarea` | Escriben o leen una `Tarea` por un pipe. |
| `presentarse(nivel_padre, &N, &i)` | Lo llama todo Meeseeks al nacer: obtiene N e i y dice "Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid, ppid, N, i)". |
| `crear_meeseeks(cuerpo, nivel_padre, fd)` | Hace `fork()`; el hijo ejecuta `cuerpo` con los descriptores `fd` y termina. Devuelve el pid del hijo. |
| `pipes_tarea(bajada, subida)` | Crea el pipe para mandar la tarea y el de la respuesta. |

## Solicitudes de texto (opciones 1 a 7)

| Función | Qué hace |
|---|---|
| `evaluar_texto(op, t, N)` | Si la dificultad es -1 la sortea (0 a 100, "a la suerte del Meeseeks"). Calcula cuántos delegados hacen falta, diluye la dificultad y llama a `delegar_texto`. |
| `ayudantes_necesarios(d)` | Según el enunciado: d > 85 → 1, 45 < d ≤ 85 → 2, d ≤ 45 → 5. |
| `diluir(d, k)` | Dificultad de cada ayudante: `d + (100 - d) / k`. Si d = 0 sigue en 0: lo imposible nunca se vuelve posible. |
| `delegar_texto(t, k, N)` | Crea k delegados, les manda la `Tarea` por pipe y espera. Al primer éxito termina a los demás con `SIGTERM`. Devuelve 1 si alguien la resolvió. |
| `delegado_texto(nivel_padre, fd)` | Lee la tarea del pipe, dice "I am executing request #k", trabaja un tiempo al azar (0.5 a 5 s) y la logra con probabilidad d/100 (siempre si d > 85). Si falla y su N es menor que 6 (`NIVEL_LIMITE`), invoca sus propios ayudantes; si N ≥ 6 ya no puede pedir ayuda y termina sin lograrlo. |
| `al_recibir_sigterm(sig)` | Si otro Meeseeks ya resolvió: termina a sus hijos, se despide y desaparece. |

**Límite de niveles.** Ningún Meeseeks con N ≥ 6 crea hijos. Si ninguno lo logra, el fracaso sube hasta el evaluador, que reporta "NO completada".
Con dificultad 0 eso siempre pasa: 1 + 5 + 25 + 125 + 625 + 3125 = 3906 Meeseeks (unos 20 s).
Con dificultad 1 la tarea sí se puede lograr, porque la dilución la vuelve más fácil en cada nivel.

## Aritmética (submenú)

En cada Meeseeks: `fd[0]` recibe la tarea del evaluador, `fd[1]` le devuelve el resultado, `fd[2]`/`fd[3]` (y `fd[4]`) son pipes entre hermanos.

| Función | Meeseeks | Qué hace |
|---|:-:|---|
| `evaluar_suma` | 1 | Crea al sumador y muestra el resultado. |
| `m_sumador` | | Toma ambos números y los suma. |
| `evaluar_resta` | 2 | Crea distancia y signo y junta: `signo * distancia`. |
| `m_distancia` | | Calcula la distancia entre los números, `abs(a - b)`. |
| `m_signo` | | Decide el signo: + si a ≥ b, - si no. |
| `evaluar_multiplicacion` | 2 | Crea contador y acumulador unidos por un pipe. |
| `m_contador` | | Mantiene el multiplicador y le resta 1 por iteración; por cada una avisa al acumulador (+1 o -1 si es negativo); 0 = fin. |
| `m_acumulador` | | Suma (o resta) el multiplicando en cada iteración y devuelve el total. |
| `evaluar_division` | 3 | Si el divisor es 0 informa el error (no completada); si no, crea divisor, dividendo y residuo y pone el signo del cociente. |
| `m_divisor` | | Le pasa el divisor al dividendo y cuenta cuántas veces cupo (cociente). |
| `m_dividendo` | | Le resta el divisor mientras quepa, avisa cada resta y le pasa lo que sobra al residuo. |
| `m_residuo` | | Recibe lo que sobró y le pone el signo del dividendo. |

## Lógica (submenú, dificultad 90)

| Función | Qué hace |
|---|---|
| `evaluar_y`, `evaluar_o`, `evaluar_xor`, `evaluar_no` | Una por opción; cada una llama a `resolver_logica` con su operación. |
| `resolver_logica(op, t, N, operacion)` | Crea los delegados que pide la dificultad (con 90, uno), les manda la tarea por pipe y muestra la primera respuesta. |
| `m_logico(nivel_padre, fd)` | Trabaja un tiempo simulado (0.5 a 5 s) y calcula AND, OR, XOR o NOT. |

## Evaluador y Box

| Función | Qué hace |
|---|---|
| `evaluador(op, t)` | Primer Meeseeks de cada solicitud (N=1): saluda, dice "I am evaluating request #k", ejecuta `op->funcion` y reporta si se completó, el tiempo y cuántos Meeseeks se usaron. |
| `elegir_opcion(menu, es_principal)` | Muestra un menú, lee la opción y entra a los submenús. Devuelve la opción elegida, o NULL si el usuario elige 0 (salir o volver). |
| `leer_booleano(pregunta)` | Pide un valor 0 o 1. |
| `preparar_tarea(op, solicitud)` | Arma la `Tarea` y le pide al usuario lo que la opción necesite: dos números, uno o dos valores lógicos, o el texto y la dificultad (número o `r` = a la suerte) de la solicitud libre. |
| `main()` | La Box: menú → `preparar_tarea` → `fork()` del evaluador → espera. 0 = salir. |
