# Corre los experimentos E1 a E4 de la Box (ver informe). Uso: gcc -Wall -pthread -o program ../meseeks.c && python3 experimentos.py
import subprocess, re, csv, sys
def sesion(entrada):
    out = subprocess.run(["./program"], input=entrada, capture_output=True, text=True, timeout=900).stdout
    filas = []
    for l in out.split("\n"):
        m = re.match(r"^(\d+)\s+(.{36})\s+([\d.]+)s\s+(?:([\d.]+)s|-)\s+(completada|no completada|caos)\s+(\d+)$", l)
        if m:
            filas.append(dict(n=int(m[1]), nombre=m[2].strip(), total=float(m[3]),
                              real=float(m[4]) if m[4] else None, estado=m[5], meeseeks=int(m[6])))
    return filas, out
w = csv.writer(open("experimentos.csv", "w"))
w.writerow(["exp", "config", "nombre", "total", "real", "estado", "meeseeks"])
def guardar(exp, config, filas):
    for f in filas: w.writerow([exp, config, f["nombre"], f["total"], f["real"], f["estado"], f["meeseeks"]])
R = 20
# E1: dificultad, intentos fijos de 0.5 s
for op, nombre in [(1,"100"),(2,"80"),(3,"60"),(4,"40"),(5,"20")]:
    filas,_ = sesion("5\n2\n0.5\n0\n" + f"1\n{op}\n"*R + "0\n"); guardar("E1", nombre, filas)
for d in [1, 0]:
    filas,_ = sesion("5\n2\n0.5\n0\n" + f"1\n6\nSolicitud libre d={d}\n{d}\n"*R + "0\n"); guardar("E1", str(d), filas)
print("E1 listo", flush=True)
# E2: misma dificultad con intentos al azar (0.5 a 5 s)
for op, nombre in [(2,"80"),(4,"40"),(5,"20")]:
    filas,_ = sesion(f"1\n{op}\n"*10 + "0\n"); guardar("E2", nombre, filas)
print("E2 listo", flush=True)
# E3: un Meeseeks o pocos: aritmetica, logica (0.5 s) y programas
filas,_ = sesion("5\n2\n0.5\n0\n" + "2\n1\n12\n30\n"*10 + "2\n2\n12\n30\n"*10 + "2\n3\n12\n30\n"*10 +
                 "2\n4\n300\n7\n"*10 + "3\n1\n1\n0\n"*5 + "4\n1\n"*10 + "4\n2\n"*10 + "4\n3\n"*10 + "0\n")
guardar("E3", "-", filas)
print("E3 listo", flush=True)
# E4: tiempo maximo de 3 s en modo "seguir esperando", intentos al azar
for d in [1, 0]:
    filas,_ = sesion("5\n1\n3\n3\n0\n" + f"1\n6\nMuy dificil d={d}\n{d}\n"*3 + "0\n"); guardar("E4", f"seguir d={d}", filas)
filas,_ = sesion("5\n1\n3\n0\n1\n7\n"); guardar("E4", "caos d=0", filas)
print("E4 listo", flush=True)
