# meseeks.c: funciones

Flujo general:

```
Usuario --menu--> Box --fork()--> Evaluador (N=1, i=1, uno por solicitud)
                                     --fork() + pipes--> Delegados (N=2, 3, ...)
```

- La **solicitud baja por pipe** (padre → hijo) y el **resultado sube por pipe** (hijo → padre).
- Cuando un delegado lo logra, su padre avisa por pipe (`STOP`) a los hermanos; cada uno se despide y avisa a sus propios hijos. El evaluador informa quién lo logró (pid, ppid, N, i).
- **i es por padre**: los hijos de cada Meeseeks se numeran 1, 2, 3...
- Todo mensaje de un Meeseeks sale con `[pid X | ppid Y]`, sangrado según su nivel.

Compilar: `gcc -Wall -pthread meseeks.c -o program`

## Menús

```
Meeseeks Box
1) General...        1-5) solicitudes de texto, 6) Solicitud libre, 7) Tarea imposible (dif. 0)
2) Aritmetica...     1) Sumar, 2) Restar, 3) Multiplicar, 4) Dividir
3) Logica...         1) Y (AND), 2) O (OR), 3) XOR, 4) NO (NOT)   (dificultad 90)
4) Programas...      1) date, 2) ls -l, 3) false, 4) Escribir un comando
5) Configuracion...  tiempo maximo, duracion de cada intento, que hacer al superar el tiempo
0) Salir             (en cada submenu, 0 = Volver)
```

Cada fila de un menú es un `Opcion`: `id`, `nombre`, `dificultad` (100 = muy fácil, 0 = imposible, -1 = no aplica), `pide` (qué datos se le piden al usuario), `funcion` (la ejecuta el evaluador y devuelve 1 si la solicitud se completó) y `submenu` (si no es NULL, la opción abre ese submenú). Un `Menu` agrupa un título y su lista de opciones.

La **tarea imposible** es la misma función que la solicitud libre (`evaluar_texto`) con dificultad 0.

## Datos que viajan por los pipes

- `Tarea` (baja): número de solicitud, texto, dificultad, duración de cada intento (0 = al azar), operandos `a` y `b`, operación lógica y comando por ejecutar.
- `Resultado` (sube): `resuelto`, los datos de quién lo logró (pid, ppid, N, i) y `espera`: los segundos simulados en la ruta desde el evaluador hasta el que lo logró.
- `STOP` (baja): un `int` que le dice a un hijo que ya se resolvió y debe terminar.

## Contadores compartidos

Viven en memoria compartida (`mmap`) y los protege un semáforo POSIX (`sem_t mutex`).

| Función | Qué hace |
|---|---|
| `contadores_crear()` | Crea la memoria compartida y el semáforo, antes de cualquier `fork()`. |
| `contadores_reiniciar()` | Pone en cero los contadores antes de cada solicitud. |
| `registrar_meeseeks(nivel_padre)` | Sección crítica: cuenta un Meeseeks más en el nivel N = nivel del padre + 1 y devuelve N. |
| `contadores_total()` | Cuántos Meeseeks se crearon en la solicitud actual (para el informe). |
| `reportar_espera(s)` / `espera_reportada()` | El evaluador anota cuánto se esperó en la simulación; la Box lo lee para calcular el tiempo real. |
| `contadores_destruir()` | Libera el semáforo y la memoria al salir. |

## Utilidades

| Función | Qué hace |
|---|---|
| `ahora()` | Segundos de reloj monotónico. |
| `enviar_int` / `recibir_int`, `enviar_tarea` / `recibir_tarea`, `enviar_resultado` | Escriben o leen por un pipe. |
| `decir(fmt, ...)` | Mensaje de un Meeseeks: siempre con su pid y ppid, sangrado según su nivel. |
| `presentarse(nivel_padre)` | Lo llama todo Meeseeks al nacer: se registra (N) y dice "Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid, ppid, N, i)". |
| `crear_meeseeks(cuerpo, nivel_padre, instancia, fd)` | Hace `fork()`; el hijo toma su i = `instancia` y ejecuta `cuerpo` con los descriptores `fd`. |
| `pipes_tarea(bajada, subida)` | Crea el pipe para mandar la tarea y el de la respuesta. |

## General: solicitudes de texto

| Función | Qué hace |
|---|---|
| `evaluar_texto(op, t)` | Si la dificultad es -1 la sortea (0 a 100). Calcula cuántos delegados hacen falta, diluye la dificultad, los crea y los supervisa. Informa quién lo logró. |
| `ayudantes_necesarios(d)` | Según el enunciado: d > 85 → 1, 45 < d ≤ 85 → 2, d ≤ 45 → 5. |
| `diluir(d, k)` | Dificultad de cada ayudante: `d + (100 - d) / (k * k)`. Si d = 0 sigue en 0. |
| `crear_delegados(t, k, h)` | Crea k delegados (i = 1..k), cada uno con su pipe de bajada y de subida, y le manda la tarea a cada uno. |
| `supervisar(h, k, fd_padre, &detenido)` | Con `poll()` escucha a la vez los pipes de los hijos y el del padre. Si un hijo lo logra, manda `STOP` a sus hermanos; si el padre manda `STOP`, se lo pasa a todos. Al final espera a todos los hijos (`waitpid`). |
| `detener_hijos(h, k, excepto)` | Manda `STOP` por pipe a los hijos que siguen trabajando. |
| `delegado_texto(nivel_padre, fd)` | Recibe la tarea, dice "I am executing request #k" y trabaja el tiempo del intento atento a un `STOP` de su padre (y mide cuánto esperó). Lo logra con probabilidad d/100 (siempre si d > 85) y lo avisa por pipe. Si falla y su N es menor que 6 invoca ayudantes; con N ≥ 6 ya no puede y avisa que no lo logró. |

**Límite de niveles:** ningún Meeseeks con N ≥ 6 crea hijos. Si nadie lo logra, el fracaso sube hasta el evaluador ("NO completada"). Con dificultad 0 eso siempre pasa (3906 Meeseeks).

## Aritmética

En cada Meeseeks: `fd[0]` recibe la tarea, `fd[1]` devuelve el resultado, `fd[2]`/`fd[3]` (y `fd[4]`) son pipes entre hermanos.

| Función | Meeseeks | Qué hace |
|---|:-:|---|
| `evaluar_suma` / `m_sumador` | 1 | Suma los dos números. |
| `evaluar_resta` / `m_distancia`, `m_signo` | 2 | Uno calcula `abs(a - b)`, otro el signo; el evaluador junta `signo * distancia`. |
| `evaluar_multiplicacion` / `m_contador`, `m_acumulador` | 2 | El contador baja el multiplicador de 1 en 1 y avisa cada vuelta (+1 o -1); el acumulador suma o resta el multiplicando. |
| `evaluar_division` / `m_divisor`, `m_dividendo`, `m_residuo` | 3 | El divisor pasa su valor y cuenta las restas (cociente), el dividendo resta mientras quepa, el residuo recibe lo que sobra. Dividir entre 0 → no completada. |

## Lógica (dificultad 90)

| Función | Qué hace |
|---|---|
| `evaluar_y`, `evaluar_o`, `evaluar_xor`, `evaluar_no` | Una por opción; llaman a `resolver_logica` con su operación. |
| `resolver_logica(op, t, operacion)` | Crea los delegados que pide la dificultad (con 90, uno) y muestra la primera respuesta. |
| `m_logico(nivel_padre, fd)` | Trabaja el tiempo del intento y calcula AND, OR, XOR o NOT. |

## Programas (fork + exec)

| Función | Qué hace |
|---|---|
| `evaluar_fecha`, `evaluar_listar`, `evaluar_falla`, `evaluar_comando` | Una por opción: `date`, `ls -l`, `false` o el comando que escriba el usuario. |
| `ejecutar_programa(t, comando)` | El evaluador delega en un Meeseeks y da la solicitud por completada solo si el código de salida es 0. |
| `m_ejecutor(nivel_padre, fd)` | Separa el comando en argumentos, hace `fork()` + `execvp()`, espera con `waitpid()`, mide el tiempo y devuelve el código de salida (127 si el programa no existe). |
| `separar_argumentos(comando, argv, max)` | Parte el comando por espacios. |

## Evaluador y Box

| Función | Qué hace |
|---|---|
| `evaluador(op, t)` | Primer Meeseeks de cada solicitud (N=1, i=1). Se pone en su propio grupo de procesos (`setpgid`), dice "I am evaluating request #k", ejecuta `op->funcion` y sale con 0 si se completó. |
| `esperar_evaluador(pid, &caos)` | La Box espera al evaluador con `waitpid()` y vigila el tiempo máximo con `alarm()` (`al_vencer_tiempo` atiende el `SIGALRM`). Al superarlo: **caos planetario** (termina a todo el grupo con `kill(-pid, SIGKILL)` y cierra la Box) o, si así se configuró, solo avisa y sigue esperando. |
| `informe()` | Al despedirse (o tras el caos): cuántas solicitudes hubo, cuántas se completaron, y para cada una su tiempo total, su tiempo real, su estado y su cantidad de Meeseeks. |

**Tiempo real.** El enunciado pide medir sin las esperas simuladas. Cada delegado mide cuánto esperó en su intento; el que lo logra manda su espera en el `Resultado` y cada padre le suma la suya al reenviarlo. Así el evaluador recibe la espera de toda la ruta ganadora y la Box calcula `tiempo real = tiempo total - espera de la ruta` (en lógica, la espera del único delegado; en aritmética y programas no hay esperas simuladas).
| `configurar()` | Submenú: tiempo máximo (300 s por defecto), duración de cada intento (0 = al azar entre 0.5 y 5 s) y qué hacer al superar el tiempo máximo. |
| `elegir_opcion(menu, es_principal)` | Muestra un menú y entra a los submenús. Devuelve la opción elegida o NULL (salir o volver). |
| `leer_booleano(pregunta)` | Pide un 0 o un 1. |
| `preparar_tarea(op, solicitud)` | Arma la `Tarea` y pide lo que la opción necesite: números, valores lógicos, texto y dificultad, o un comando. |
| `main()` | Menú → `preparar_tarea` → `fork()` del evaluador → `esperar_evaluador` → registra la solicitud. Al salir muestra el informe. |
