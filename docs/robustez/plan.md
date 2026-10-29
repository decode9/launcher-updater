# Plan · Robustez y seguridad del launcher antes del lanzamiento (0.1.1 → 0.2.0)

Fecha: 2026-10-28 · Base: diagnóstico de esta sesión (opción A) · Repo: `decode9-launcher`, rama `main` @ 81fc97e

## Contexto

El launcher aún no se ha lanzado. El diagnóstico encontró que el núcleo está sano (red, verificación de
archivos y recuperación ante cortes), pero detectó cuatro problemas urgentes:

- un release grande puede dejar el juego sin poder actualizarse nunca;
- se le puede colar una DLL ajena;
- la firma de las auto-actualizaciones se comprueba de forma débil;
- se pueden abrir dos launchers a la vez.

Además hay fallos de robustez en la instalación y de experiencia en la ventana. El objetivo es cerrar todo eso
antes de activar la firma con SignPath y publicar la 0.2.0, sin cambiar la estructura del proyecto
(`core` ← `platform` ← `app` ← `main.c`) y sin que nada que hoy funciona quede peor.

## En llano (para ti)

| Fase | Qué logra para el jugador | Tamaño |
|---|---|---|
| 1 · Urgentes | • Las actualizaciones grandes nunca atascan el juego.<br>• Los archivos viejos se limpian.<br>• No se cuela código ajeno.<br>• Solo se aceptan actualizaciones del launcher firmadas por ti, y no las de certificados revocados.<br>• Abrir el launcher dos veces solo trae al frente la ventana que ya estaba abierta. | M |
| 2 · Instalación robusta | • Un antivirus que bloquea un archivo un instante ya no provoca una redescarga completa.<br>• Un servidor mal configurado no puede romper la instalación.<br>• El launcher no entra en un bucle de auto-actualización.<br>• Los archivos de solo lectura no bloquean nada. | M |
| 3 · Ventana | • Play siempre responde y usa la información más reciente del servidor.<br>• Si no hay versión publicada, se dice claramente.<br>• La ventana no se congela al recargar las noticias.<br>• Se ve bien en pantallas con escalado.<br>• Al cerrarla se cierra de verdad. | M–L |

Al terminar cada fase te digo cómo comprobarla tú y espero tu visto bueno antes de la siguiente.

## Objetivos comprobables

1. Pasar de un release con 2.100 archivos a otro con 2.100 rutas distintas termina bien. Los archivos del release
   viejo desaparecen del disco y no queda ningún `.part` suelto.
2. Una DLL falsa junto a `launcher.exe` no se carga. Un ejecutable firmado por otra entidad con el mismo nombre
   no se acepta como actualización.
3. Nunca hay dos launchers instalando en la misma carpeta.
4. Ningún fallo pasajero de disco o antivirus obliga a volver a descargar el juego entero.
5. Ningún clic en Play se pierde, y Play nunca usa un estado del servidor viejo.

## Qué NO entra (y por qué)

| Queda fuera | Motivo |
|---|---|
| Rotar `launcher.log`, limpiar `.launcher/cache`, rutas de más de 260 caracteres | Diagnóstico: «puede esperar». No afectan la seguridad ni la instalación |
| Plazo total de red (R-02), revocación del CDN (R-03), renegociación TLS y 1xx (R-05/R-06), `Content-Range` (R-04) | Robustez de red de severidad baja. El SHA-256 con pin ya protege la integridad |
| `file` + `line` en la misma ruta vuelve a descargar (I7) | Requiere rediseñar cómo se registra la base. Es solo ancho de banda |
| Colisiones Unicode y dispositivos raros en rutas (I8), junctions (F1), PID reutilizado (S1), TOCTOU del `.new` (T3) | Baja probabilidad. Exigen un servidor malicioso o acceso local previo |
| Recuperar journals perdidos si `state.json` se corrompe | `state.json` solo se escribe con renombrado atómico, así que corromperlo requiere daño del disco |
| Imágenes de tema de 8192×8192 (H6), títulos largos (H15), ratón capturado (H14) | El contenido viene de tu servidor, que está verificado |
| Versiones de las acciones de GitHub, firma con SignPath | Van aparte, con su propio paso de publicación |

## Decisiones

- **D-01 · Archivos huérfanos.** Al empezar una instalación, después de deshacer los journals y antes de descargar,
  el launcher borra del disco los archivos que gestionaba y que el nuevo release ya no menciona (junto con su
  `<path>.part`), y luego los olvida en el estado. El orden es borrar → olvidar → guardar estado, así que es
  idempotente: si se corta a mitad, el siguiente arranque repite el borrado sin daño.
  Esto arregla I1, porque el estado nunca supera el tamaño del manifest, y también arregla I6.
  Solo se borra lo que el launcher escribió (lo que está en `state.json`). Nunca se borran partidas, mods ni
  configuración del usuario.
  *Descartadas:*
  - duplicar el tope del estado: suma unos 5 MB de memoria y no limpia el disco;
  - limpiar al final: el error ocurre antes de llegar al final.
- **D-02 · DLL.** Se enlaza con `/dependentloadflag:0x800` (System32 solamente; lld-link 23 lo soporta) y lo
  primero que hace `entry()` es llamar a `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`, para las DLL
  que Windows carga después. No se prohíben las rutas `.dll` en el manifest, porque el juego necesita las suyas.
- **D-03 · Firmante.** Se compara el *subject* completo y el *issuer* completo del certificado hoja
  (`CertNameToStrW`, X500), no solo el CN. Queda documentada una limitación: con la firma gratuita de SignPath
  Foundation, el firmante es «SignPath Foundation» para todos los proyectos que firma. La firma prueba «firmado por
  SignPath», no «compilado por decode9». La protección principal sigue siendo el SHA-256 del status con pin.
- **D-04 · Revocación.** Se activa `WTD_REVOKE_WHOLECHAIN` con consulta por red. Si la revocación no se puede
  comprobar, la actualización se rechaza (falla cerrado). El juego sigue jugable con el launcher actual (verificado
  en `window.c:304-308`) y se reintenta en el siguiente arranque. Se corrigen la spec y el comentario de
  `selfupdate.c` para que coincidan con el código.
- **D-05 · Una sola instancia.** Se usa un mutex con nombre `Local\decode9-launcher-<hash de la carpeta>`.
  - **Ventana:** si el mutex ya está tomado, trae al frente la ventana existente y sale con código 0.
  - **`--install`:** sale con un código nuevo, `4` («otro launcher está trabajando en esta carpeta»), documentado
    en el README.
  - **`--status` y `--theme-preview`:** no toman el mutex.
  - **`--swap`:** espera a que salga el proceso viejo, hace el reemplazo y **suelta el mutex antes** de relanzar
    `launcher.exe`. Si no, el relanzado se encontraría el mutex tomado y se cerraría.
- **D-06 · `state.json` ilegible.** Se añade al puerto de archivos `probe(path)`, que distingue
  existe / no existe / error. En Win32 se implementa con `GetFileAttributesW`.
  - Ilegible por bloqueo: `INSTALL_ERR_IO` sin escribir nada; se reintenta después.
  - Corrupto: se renombra a `state.json.bad` y se sigue como hoy.
- **D-07 · Validación del manifest.** Se rechaza el manifest entero si:
  - una ruta termina en `.part` (sufijo reservado);
  - una ruta es a la vez archivo y carpeta (`a` y `a/b`);
  - tiene más patches que `STATE_JOURNALS_MAX`;
  - la suma de tamaños desborda.

  Es el mismo estilo que la validación actual de `manifest.c` (`check_order`).
- **D-08 · Deshacer un journal verifica.** Después de `restore`, se calcula el hash del archivo. Si no coincide con
  la base, se olvida la entrada (y el `patch` o `file` decide) en vez de darla por buena.
- **D-09 · Bucle de auto-actualización.** Al arrancar, si el status anuncia una versión mayor pero el SHA-256 de mi
  propio exe es igual al del blob anunciado, la actualización ya se aplicó y el blob trae otra versión. En ese caso
  se registra el error y no se reintenta. No necesita guardar nada en disco.
- **D-10 · Solo lectura.** `fs_rename` y `fs_remove` quitan el atributo de solo lectura del destino y reintentan
  una vez. Después de crear `.new`, `proc_launch` se reintenta 3 veces cada 500 ms, igual que el renombrado.
- **D-11 · Flujo de la ventana testeable.** Las decisiones de la ventana pasan a `src/core/flow.{h,c}`, que es
  puro y no llama al sistema operativo:
  - entra: status, hora actual, «instalado al día», «interrumpido», «clic pendiente» y último error;
  - sale: estado de pantalla, clave de texto, Play habilitado y cuánto esperar.

  `window.c` queda como adaptador. Así se prueban en Linux los casos H2, H3, H8 y H11 (Clean Architecture: la regla
  de negocio, en `core`).
- **D-12 · Tema.** Se añade la clave `status.no_release` en `docs/theming.md`, en el schema y en el tema neutral,
  con texto por defecto. Los temas existentes siguen siendo válidos, porque las claves son opcionales y tienen un
  texto por defecto.

Preguntas abiertas: ninguna bloquea. Las decisiones D-04 (falla cerrado) y D-05 (código de salida 4) son las que más
cambian el comportamiento visible. Si prefieres otra cosa, dímelo al revisar el plan.

## Invariantes (ninguna fase las rompe)

| ID | Regla | Qué la hace cumplir |
|---|---|---|
| I-1 | Ningún archivo aparece bajo su nombre sin bytes verificados | `test_install.c`: matar el proceso en cada operación |
| I-2 | `core` no incluye cabeceras de plataforma | revisión y compilación de `tests/run.sh` (sin Windows) |
| I-3 | Sin CRT: marcos de pila ≤ 4 KiB, sin división de 64 bits | `./build.sh` (el enlazado falla si no) |
| I-4 | Solo se borran archivos listados en `state.json` o temporales propios | prueba nueva de D-01 con archivos del usuario presentes |
| I-5 | Las 65 pruebas actuales siguen pasando | `tests/run.sh` |

## Fases

### Fase 1 · Urgentes (M)

**Qué cierra:** I1, I6, W1, T1, T2, P1/H1.

| Tarea | Archivos | Depende de |
|---|---|---|
| 1.1 Huérfanos (D-01) | `src/core/install.c` (`prune` → `retire_orphans` antes de `plan`), `install.h` (comentario de pasos), `docs/spec.md`, `tests/test_install.c` | — |
| 1.2 DLL (D-02) | `build.sh`, `src/main.c`, `src/platform/win32/sys.{c,h}`, `kernel32.def` | — |
| 1.3 Firma (D-03, D-04) | `src/platform/win32/trust.{c,h}`, `crypt32.def`, `src/app/selfupdate.c`, `docs/spec.md` §Self-update | — |
| 1.4 Instancia única (D-05) | `sys.{c,h}` (o `proc.c`), `src/app/app.c` (`app_begin`), `window.c` (traer al frente), `selfupdate.c` (`--swap`), `README.md` | 1.2 y 1.3 (comparten archivos) |

1.1, 1.2 y 1.3 van en paralelo; 1.4 va después.

**Pruebas nuevas:**

- el caso de 2.100 + 2.100 rutas, a partir de la prueba que ya existe en `scratchpad/poc/poc.c`;
- los huérfanos se borran y los archivos que no son del launcher se conservan;
- matar el proceso durante el borrado de huérfanos y comprobar que se recupera.

**Riesgos:**

- Borrar un archivo que el juego necesitaba porque el manifest lo olvidó por error. *Mitigación:* el log lista cada
  archivo borrado, y la prueba I-4.
- La revocación con red puede tardar en el primer arranque. *Mitigación:* solo ocurre durante una auto-actualización.

**Puerta de salida:**

- `tests/run.sh` en verde (65 pruebas anteriores más las nuevas);
- `./build.sh` OK;
- lista de términos prohibidos limpia;
- prueba en Wine con el servidor de pruebas: estado, instalación, ventana, y dos launchers abiertos (uno trae al
  frente al otro).

**Cómo lo compruebas tú:**

1. En la carpeta del proyecto ejecuta `tests/run.sh`. Debe terminar con «all tests passed».
2. Te dejo una carpeta de prueba lista. Haz doble clic dos veces seguidas en `launcher.exe`: debe haber una sola
   ventana.
3. Cambia el release de ejemplo por uno con otros archivos, pulsa Play y mira la carpeta: los archivos viejos ya no
   están.

**Vuelta atrás:** cada fase es un commit en la rama `robustez-0.2`, y `git revert` la deshace.
Hay que volver atrás si falla cualquier prueba anterior, o si en Wine el launcher no arranca o no instala.

### Fase 2 · Instalación robusta (M)

**Qué cierra:** I2, I3, I4, I5, I9, R-01/S3, S2, S4.

| Tarea | Archivos |
|---|---|
| 2.1 Validación del manifest (D-07) | `src/core/manifest.c`, `src/core/path.c`, `tests/test_manifest.c` |
| 2.2 `state.json` y journals (D-06, D-08) | `src/core/install.{c,h}` (puerto `probe`), `src/platform/win32/install_port.c`, `tests/test_install.c` (puerto falso) |
| 2.3 Auto-actualización y archivos (D-09, D-10) | `src/app/selfupdate.c`, `src/platform/win32/fs.c`, `src/platform/win32/proc.c` |

Las tres tareas van en paralelo, porque no comparten archivos.

**Pruebas nuevas:**

- un manifest con `x` y `x.part`, otro con `a` y `a/b`, otro con 1.025 patches y otro con tamaños que desbordan:
  todos se rechazan;
- `state.json` bloqueado da `INSTALL_ERR_IO` sin escribir nada;
- `state.json` corrupto queda como `.bad`;
- un archivo modificado fuera del launcher antes de deshacer su journal se olvida y no se da por bueno.

**Riesgos:**

- Rechazar un manifest que hoy es válido. *Mitigación:* los manifests de ejemplo (`make-release.py`) deben seguir
  pasando.

**Puerta de salida:** la misma que en la fase 1. Además, en Wine, un `launcher.exe` de solo lectura se actualiza bien.

**Cómo lo compruebas tú:**

1. `tests/run.sh` en verde.
2. En la carpeta de prueba, marca un archivo del juego como «solo lectura» y pulsa Play: actualiza sin error.
3. Te preparo un servidor que anuncia una versión con el ejecutable equivocado. Abre el launcher dos veces: solo lo
   intenta una vez y el log dice por qué.

**Vuelta atrás:** revertir el commit de la fase.

### Fase 3 · Ventana (M–L)

**Qué cierra:** H2, H3, H4, H5, H7, H8, H11, y la deuda H16 en su parte de flujo.

| Tarea | Archivos | Depende de |
|---|---|---|
| 3.1 `core/flow` con pruebas (D-11) | `src/core/flow.{h,c}` (nuevo), `tests/test_flow.c`. Reproduce el comportamiento actual y corrige: el clic pendiente no se borra (H2); Play pide un status fresco antes de instalar o lanzar (H3); estado «sin versión publicada» con Play deshabilitado (H8); reintentos crecientes de 5 a 60 s más un desfase aleatorio cuando venció la cuenta regresiva (H11) | — |
| 3.2 Conectar `window.c` al flujo (D-12) | `src/app/window.c`, `docs/theming.md`, `docs/theme.schema.json`, `examples/theme-neutral/theme.json`, `docs/spec.md` §Launcher flow | 3.1 |
| 3.3 Noticias y cierre | `window.c` (`refresh_news` solo si cambió la lista), `src/platform/win32/ui/ui.c` (conservar la noticia del carrusel), `src/app/app.c`/`cache.c` (cancelar descargas al salir), `selfupdate.c` (no lanzar `.new` si se está saliendo) (H4, H7) | 3.2 |
| 3.4 DPI | `ui/ui.c`, `ui_draw.c`, `ui_res.c`, `res/launcher.manifest`: escalar coordenadas y fuentes del tema por el DPI del sistema y ajustar la ventana al área de trabajo (H5) | 3.2; en paralelo con 3.3 si no tocan las mismas funciones de `ui.c` |

**Riesgos:**

- Cambiar sin querer el comportamiento actual de la ventana. *Mitigación:* `test_flow.c` describe primero el
  comportamiento de hoy y luego los cambios.
- Que el escalado por DPI desajuste los temas. *Mitigación:* capturas en Wine al 100 %, 150 % y 200 % con el tema
  neutral y el de demostración.

**Puerta de salida:**

- `tests/run.sh` en verde (incluido `test_flow`);
- `./build.sh` OK;
- schema de temas válido;
- capturas al 100/150/200 % revisadas por mí;
- recorrido en Wine: cerrado → cuenta regresiva → abierto → Play → instala → lanza `fake-game`.

**Cómo lo compruebas tú:**

1. Con el servidor de pruebas, deja el launcher abierto y pulsa Play justo cuando aparece «comprobando»: debe
   empezar igual.
2. Quita la versión publicada del servidor: debe decir que no hay versión y Play queda gris.
3. Cambia el escalado de pantalla a 150 %: la ventana se ve del mismo tamaño relativo.
4. Cierra la ventana durante una descarga: el proceso desaparece del Administrador de tareas en unos segundos.

**Vuelta atrás:** revertir el commit. Hay que volver atrás si `test_flow` o el recorrido en Wine fallan, o si un tema
existente se ve mal.

## Cómo se ejecuta (orquestación)

- **Antes de empezar:**
  - copio este plan a `docs/robustez/plan.md` (el proyecto no tiene convención de planes);
  - la lista de términos prohibidos debe seguir limpia;
  - creo la rama `robustez-0.2`;
  - línea base: 65 pruebas y 1.109 comprobaciones en verde, build de 119.808 bytes.
- **Agentes:** uno nuevo por tarea, de 40 a 60 pasos cada uno, con un encargo autosuficiente y un resumen de traspaso
  al terminar. Yo reviso el código de cada traspaso en los puntos de riesgo. La validación mecánica (pruebas, build,
  lista de términos prohibidos) la hace un agente con Sonnet. La revisión con criterio y las capturas las juzgo yo.
- **Por fase:** actualizo el CHANGELOG (`[Unreleased]`) y el estado en `docs/robustez/plan.md`, y te informo para tu
  visto bueno.
- **Requieren tu confirmación:** cada commit, subir la rama, la etiqueta `v0.2.0` y la publicación del release.

## Verificación de punta a punta (al final)

1. `tests/run.sh`, `./build.sh` y `tools/check-denylist.sh` en verde.
2. En Wine con `examples/dev-server`:
   - `launcher.exe --status` da el código 0;
   - `--install` en una carpeta vacía da el código 0 y los SHA-256 coinciden con los que imprime `make-release.py`;
   - un segundo release con rutas distintas deja limpia la carpeta;
   - `--install` con la ventana abierta da el código 4;
   - la ventana completa hasta lanzar `fake-game`;
   - la auto-actualización con `--swap` funciona y no entra en bucle.
3. Revisión final mía del diff completo contra este plan y contra las invariantes I-1 a I-5.

## Estado

### Fase 1 · Urgentes — hecha, commit fd7966b (2026-10-28)
- 1.1 Huérfanos (D-01): `retire_orphans` en `src/core/install.c`. Ajuste dentro de I-4: `state.json` guarda
  `owned` y solo se borran archivos entregados completos por una op `file`; los editados por `patch`/`line` se olvidan.
- 1.2 DLL (D-02): `src/platform/win32/loadcfg.c` (load config propio, sin él lld ignora `/dependentloadflag`),
  `sys_harden_dll_search()` vía `GetProcAddress` (no import estático, para no romper el arranque en sistemas viejos).
- 1.3 Firma (D-03, D-04): `trust_signer` con subject + issuer X.500; revocación de toda la cadena (sin raíz) online.
- 1.4 Instancia única (D-05): `src/platform/win32/instance.{c,h}`, clase de ventana por carpeta, código 4,
  `--swap` toma y suelta el bloqueo. Log abierto en modo append compartido para que el proceso rechazado registre.
- Puerta: `tests/run.sh` 72 pruebas / 1566 checks OK; `./build.sh` OK (123 904 bytes); denylist limpio;
  Wine e2e 6/6 (status 0, install 0 con hashes, huérfanos borrados y archivo del usuario intacto, install ocupado 4,
  segunda ventana 0, sin launcher.json 1, DependentLoadFlags 0x800).
- Sin verificar (requiere Windows real): DLL plantada ignorada, firma/revocación con exe firmado, traer al frente.

### Fase 2 · Instalación robusta — hecha, commit 5a57a49 (2026-10-28)
- 2.1 (D-07): `manifest.c` rechaza rutas `.part`, archivo+carpeta, >1024 patches y sumas que desbordan; las rutas
  se comprueban una vez tras ordenarlas (heap sort sin malloc, 4096 ops en ~5 ms). `MANIFEST_PATCHES_MAX` en
  `manifest.h`; `STATE_JOURNALS_MAX` lo usa.
- 2.2 (D-06, D-08): puerto `probe`; `state.json` ilegible → `INSTALL_ERR_IO` sin escribir; corrupto → `.bad`.
  Deshacer un journal calcula el hash y olvida la entrada si no coincide (coste: un hash completo por archivo
  parcheado en cada actualización).
- 2.3 (D-09, D-10): sin bucle de auto-actualización si el blob anunciado es el exe en ejecución; `fs_rename` /
  `fs_remove` quitan el atributo de solo lectura tras «acceso denegado»; lanzar el `.new` se reintenta 3 veces.
- Ajuste: la prueba de huérfanos de la fase 1 usaba una ruta `.part` en el manifest, ahora prohibida; se cambió.
- Puerta: `tests/run.sh` verde; build 128 512 bytes; denylist limpio; Wine 6/6 (status, install con hashes,
  archivo de solo lectura actualizado, `state.json` corrupto → `.bad`, manifest con `.part` rechazado sin tocar
  nada, instalación limpia).

### Fase 3 · Ventana — hecha (2026-10-29)
- 3.1 (D-11): `src/core/flow.{h,c}` + `tests/test_flow.c` (22 pruebas). Sin fecha de apertura se sigue
  consultando cada 5 min (como hoy); los reintentos 5→60 s + 0–25 % solo cuando la cuenta regresiva venció.
- 3.2 (D-12): `window.c` es un adaptador del flujo; `display_action_enabled(..., play_enabled)` y
  `ui_set_play_enabled`; `/v1/status` acepta `release` ausente o `null`; clave `status.no_release` (en/es).
  Una plantilla con una variable vacía (p. ej. `v{version}` sin versión) no se dibuja.
- 3.3 (H4, H7): las noticias solo se reenvían a la ventana si cambió la lista y el carrusel conserva su
  noticia; bandera de salida única que corta descargas, reintentos y auto-actualización; al cerrar se espera
  al worker 3 s y si no, `TerminateProcess` sobre sí mismo (seguro: todo lo escrito en disco es atómico o
  con journal).
- 3.4 (H5): escala por DPI del sistema con una transformación GDI+ en un solo punto; ratón desescalado en
  `ui_hit`; la escala baja si la ventana no cabe en el área de trabajo (mín. 25 %). Capturas 100/150/200 %
  revisadas (a 200 % en 1080 px baja a 180 % y se ve entera).
- Puerta: `tests/run.sh` verde (schema validado con jsonschema en un entorno aparte); build 131 072 bytes;
  denylist limpio; Wine 7/7: cuenta regresiva → abierto sin reiniciar, Play → instala → lanza el juego,
  `--install` con ventana → 4, segunda ventana → 0, sin versión → texto y Play gris, cierre durante descarga
  → proceso fuera en <1 s y la descarga se reanuda después.
- Sin verificar (Windows real): clics a 150 %, per-monitor DPI (no soportado: solo DPI del sistema).

### Cierre — verificación final (2026-10-29)
- Wine de punta a punta 6/6 con `examples/dev-server`: `--status` 0; `--install` en carpeta vacía 0 con
  los SHA-256 de `make-release.py`; segundo release con otras rutas deja la carpeta limpia y el archivo
  del usuario intacto; `--install` con la ventana abierta 4; ventana → Play → instala → lanza
  `fake-game`; auto-actualización 0.1.1 → 0.1.2 con `--swap` sin bucle, y caso D-09 con un solo
  mensaje en el log y ninguna descarga.
- Revisión del diff completo: sin hallazgos altos. Se corrigieron dos de `retire_orphans`:
  - el `.part` de una descarga cortada cuya ruta sale del release quedaba para siempre: ahora la
    ruta se registra como pendiente (hash cero) antes de descargar, y se borra su `.part`;
  - un archivo que en el release siguiente pasa a carpeta (`a/b` → `a`) bloqueaba la instalación
    con `INSTALL_ERR_IO`: ahora se quitan las carpetas que quedan vacías (puerto `remove_dir`,
    `RemoveDirectoryW`, que solo borra carpetas vacías).
  - además, el `.part` junto a un archivo que el launcher solo editó ya no se borra (no es suyo).
- Puerta: `tests/run.sh` verde (`test_install` 24 pruebas; las dos nuevas fallan con el código
  anterior); build 132 608 bytes; denylist limpio. `VERSION` 0.2.0 y sección `[0.2.0]` del CHANGELOG.

## Dónde retomar
Todo lo del plan está hecho y la 0.2.0 está preparada en la rama `robustez-0.2` (sin subir).
Pendiente de tu confirmación: fusionar en `main`, la etiqueta `v0.2.0`, subirlas y con eso la
publicación del release. Antes de publicar conviene la prueba en Windows real (DLL plantada, firma
con SignPath, traer al frente, DPI con clics).
