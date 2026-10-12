# Tarea # 3 - Mr. Meeseeks

> Copia textual del enunciado de la Tarea 3 (IC-6600 Principios de Sistemas Operativos, II 2026).
> Solo se cambió el formato a Markdown (encabezados y listas); el texto del enunciado no se modificó.

---

## Mr. Meeseeks

Hi, I am Mr MEESEEKS! LOOK AT MEEEEE.

Mr. Meeseeks is the name of all the Meeseeks summoned by activating a Meeseeks Box. The Meeseeks appear in the fifth episode of the first season, "Meeseeks and Destroy" of the TV series "Rick and Morty". They are well known to inhabit planets across the universe.

- https://www.youtube.com/watch?v=qUYvIAP3qQk
- https://www.youtube.com/watch?v=gK8l0LsIqn0

---

## Box Mr.Meeseeks

### 0x01.

Implemente un programa Box de Mr.Meeseeks, que recibirá del usuario una solicitud en formato de texto, por ejemplo, "Quiero un café francés". O bien, podría solicitarse al Mr. Meeseeks realizar un cálculo en particular (una operación aritmética o lógica) o ejecutar un programa y esperar el resultado positivo o no del mismo.

Su programa Box creará entonces (mediante un system call fork) un Mr. Meeseeks para atender dicha solicitud.

El programa Box podrá recibir múltiples solicitudes consecuentes, hasta que el usuario decida terminar el proceso despidiéndose. Al cerrar el programa Box, el mismo deberá dar un breve informe de la cantidad de tareas solicitadas, el tiempo de duración de cada una, las tareas que se pudieron terminar y las que no, además, para cada caso se debe indicar la cantidad de Mr. Meeseeks que se crearon para solucionar el problema.

Al iniciar, cada Mr. Meeseeks dará el mensaje de bienvenida: "Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid, ppid, N, i)" donde pid y ppid serán los valores correspondientes al PID del proceso Meeseeks creado y su PPID, N indicará el nivel e i indicará el número de instancia. Al iniciar, este valor N será de 1 al igual que la instancia en 1, pues es el primero en el primer nivel.

Los Meeseeks atienden solicitudes, las resuelven y desaparecen. Algunas solicitudes pueden ser fáciles y otras muy difíciles.

Para todos los casos deberá tomar los tiempos de ejecución reales, sin tomar en consideración los tiempos de espera aleatorios que generen los procesos para la simulación.

#### A.

Para las consultas textuales como "¡Quiero ser feliz!", se tratarán como una simulación de la realización de dicha tarea. Habrá dos posibles formas de determinar su dificultad: caso 1. Consultar al usuario la dificultad de la solicitud, caso 2. Dejarlo a la suerte del Mr. Meeseeks creado. En cualquier caso será con un valor de 0 a 100  el correspondiente a la probabilidad de dificultad, donde 100 es muy fácil (un solo Mr. Meeseeks lo solucionará) a 0 imposible (cientos o miles de Mr. Meeseeks no podrán con la tarea; podría generarse caos con los recursos del sistema hasta poder colapsar. En el caso 2, se debe estimar de forma aleatoria mediante una distribución de probabilidad para calcular el valor de dificultad de dicha solicitud. Para el caso de solicitudes imposibles, valoradas con 0, se debe establecer un plan de contingencia para evitar el colapso del sistema por la creación de miles de procesos. Debe documentar claramente las acciones que se tomarán en cada caso.

Se debe considerar también el tiempo que durará la solicitud en ser completada (la simulación) por el Mr. Meeseeks, esto será mediante un cálculo de tiempo aleatorio de entre 0.5 y 5 segundos, o un valor que defina el usuario (para efectos de la simulación).

Luego del tiempo de espera el Mr. Meeseeks determinará si podrá satisfacer o no la solicitud.

==> Para este punto deberá proponer una solución creativa e interesante (de acuerdo al contexto de los Mr. Meeseeks) de cómo calcular si Mr. Meeseeks podrá o no resolver el problema o si requerirá la ayuda de otros Mr. Meeseeks y cuántos. Considere que para un problema difícil la cantidad de Mr. Meeseeks que se van a requerir será muy alta también (tenga en cuenta el ejemplo del video). Haga uso de probabilidades; aproveche el valor de la dificultad del problema por resolver como base o referencia para sus cálculos. Un valor de 0, una tarea imposible, no debería generar una solución por parte del programa en ningún momento y se podrían estar creando cada vez más Mr. Meeseeks y durando incluso más tiempo del habitual cada uno.

Tenga en cuenta que para las tareas en un rango de 0 a 85, el Mr. Meeseeks evaluará la posibilidad de solicitar ayuda a otro Mr. Meeseeks para cumplir la solicitud. Para valores de 0 a 45 creará al menos 5 Mr. Meeseeks (acá N será N+1 con respecto a su padre, i tendrá valores 1, 2, 3, 4 y 5 para cada uno de los nuevos hijos), para valores de 45.01 a 85 creará al menos 2 Mr. Meeseeks nuevos  (acá N será N+1 con respecto a su padre, i permanece en 1 y el otro en 2). Se asume que de 85.01 a 100 la tarea la realizará 1 solo Mr. Meeseeks.

Los Mr. Meeseeks nuevos creados deberán volver a calcular la posibilidad de completar la tarea, tomando en cuenta que para tareas realizables (diferentes de 0) se debe ir disminuyendo y diluyendo la dificultad entre los distintos Mr. Meeseeks creados para que la tarea se vaya simplificando hasta que pueda ser ejecutada. Algo similar a la técnica de "añejamiento" (en planificación, aging).

Como se indicó antes, al iniciar, cada Mr. Meeseeks dará el mensaje de bienvenida: "Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid, ppid, N, i)".

#### B.

En el caso de los cálculos, se debe escribir o buscar un pequeño programa que reciba hileras de texto de operaciones aritméticas o lógicas y las procese y dé el resultado o un error. Se espera que sea un proceso sencillo que ejecuta un solo Mr. Meeseeks. Debe documentar el proceso, el programa y las consideraciones especiales.

#### C.

En el caso que se solicite la ejecución de un programa de terceros o externo se debe considerar el tiempo de ejecución del mismo y esperar el valor de retorno de dicho programa, algo que se acostumbra a hacer en programas en entonces Linux o UNIX que retornan valores como 0, 1 o incluso valores negativos o positivos mayores, para indicar que no hubo errores, que terminó con errores o bien con valores numéricos que indican tipos de errores. En ese caso solo se debe considerar si el programa terminó con éxito o no, para saber si el Mr. Meeseeks reportará la tarea como completada o no.

#### En General:

Deberá hacer uso de semáforos administrados por el sistema operativo para acceder a los recursos compartidos, como el contador de niveles o instancias. Debe hacer uso de pipes para comunicar a los Mr. Meeseeks entre sí, cada Mr. Meeseeks deberá pasar por un mensaje privado vía pipe la solicitud hecha por el usuario. No se podrá usar una variable global para transmitir mensajes, pero sí una variable compartida global para el contador de Niveles e instancias (si lo requiere). Se recomienda el uso de la biblioteca pthreads para los semáforos y otros.

Luego del tiempo tratando de resolver el problema, para las solicitudes con probabilidad de 85.01 a 100.0 el Mr. Meeseeks brindará un mensaje de respuesta al usuario indicando que la solicitud ha sido completada, agradecerá haberle servido y se despedirá del usuario con un mensaje final antes de desaparecer. Cuando un Mr. Meeseeks ha logrado resolver el problema; deberá comunicarlo vía Pipes (de IPC) a su padre; este lo comunicará a los hermanos del Mr. Meeseeks que solucionó el problema para que terminen, despidiéndose cada uno del usuario antes de desaparecer. El padre hará también lo mismo, comunicará a su padre la resolución del problema y se despedirá del usuario. Todos los procesos se irán despidiendo y desapareciendo recursivamente hacia atrás hasta llegar al padre original. Finalmente el Mr. Meeseeks original creado informará al usuario la resolución de la solicitud hecha y el Mr. Meeseeks que lo resolvió (con sus datos : pid, ppid, N, i).

Al terminar cada Mr. Meeseeks su trabajo asignado se devolverá el control al programa Box para esperar la siguiente solicitud.

Durante todo el proceso, el programa deberá reportar mediante mensajes al usuario lo que ocurre. Para todos los mensajes que envíen los Mr. Meeseeks al usuario se deberá indicar siempre el pid y ppid del proceso que genera el mensaje.

Recuerde que usted solo debe realizar un único programa, donde se inicializa el ambiente, se crearán los procesos Mr. Meeseeks necesarios hasta completar las solicitudes requeridas. Todos los procesos hijos ejecutarán el mismo código programado. Determine un tiempo máximo que debe transcurrir (configurable por el usuario) luego del cual se decretará un caos planetario y se deberá terminar la ejecución del programa y todos sus procesos asociados. El proceso padre deberá terminar todos los procesos hijos para concluir. Considere una base no menor a 5 minutos como punto de partida.

¿Qué pasa si para una solicitud muy difícil los Mr. Meeseeks no logran cumplir con la misión luego de los minutos máximos y no terminan el programa? ¿Cuánto tiempo adicional se necesitará para llegar a cumplir la misión? Realice algunas pruebas con este escenario, sin terminar el programa en el tiempo máximo establecido y documente sus resultados. Brinde observaciones y conclusiones importantes que pueda rescatar de estas pruebas.

---

### 0x02.

Con los tiempos tomados en sus programas en 0x01, realice comparaciones y analice los resultados.
Plante experimentos que permitan una comparación válida del comportamiento de los casos en cada escenario para su análisis.
Realice observaciones y conclusiones pertinentes.

Esta parte será muy importante en la evaluación de esta Tarea.

---

### Importante:

En su documentación deberá explicar y mostrar las secciones de código que implementan todas las partes y aspectos acá solicitados. Utilice diagramas o similares para explicar la organización de su código, sus estructuras, la comunicación entre procesos y el trabajo de los Meeseeks.

---

### Uso de Git

Todo el desarrollo de esta Tarea deberá realizarse usando Git, desde el inicio de su desarrollo hasta la entrega.
Se deberán registrar actividades en Git; no es simplemente llegar a copiar el código en el repositorio antes de la entrega.
Sino que debe ser su repositorio principal de trabajo y será parte de la evaluación.
Utilice un repositorio privado para su trabajo.

---

### Entrega

- Trabajo Individual
- Entrega Digital vía Google Classroom y su enlace de Git
- Reporte PDF + Archivo con código fuente en texto
- Formato nombre de archivos: SO-T3-SuNombreCompleto

- Entregar un documento en PDF, con formato completo: portada, introducción, índice, desarrollo y conclusiones. En el desarrollo de su reporte, explique la forma de implementación del programa y documente con ejemplos los resultados obtenidos; puede usar capturas de pantalla para ilustrar sus explicaciones.
- En el reporte deben aparecer al menos las salidas de ejecución de 3 corridas de cada caso, donde se muestre paso a paso el avance del programa y los resultados finales.
- Puede usar como referencia el código del ejemplo de las Lecturas: Caps. 13 y 14 y similares del libro Beginning Linux Programming 4th Ed. Además del código fork trabajado en clase, correspondiente al código del capítulo 3 del Silberchatz.
- Toda la tarea se debe desarrollar trabajando con el lenguaje C sobre Linux, únicamente.
- El programa en C debe tener un encabezado completo con sus datos y venir documentado internamente
