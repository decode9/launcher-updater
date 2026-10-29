# Plan · Tema con layout «marco + logo desbordado» (launcher 0.2.0 → 0.3.0)

## Contexto

Hay un mockup de launcher (logo que sobresale del marco, marco con degradado, pestañas de noticias en lista
con etiqueta/título/fecha, botones redondos laterales, Options, Language, Play grande, barra de carga con estado)
y 14 assets en `~/Downloads/Launcher/Launcher/`. El launcher es genérico (el CI tiene un denylist que prohíbe el
nombre del juego en el repo): todo lo visual viene de `theme.json`. Hoy el motor de temas **no puede** reproducir
ese layout. Objetivo: añadir al motor las capacidades genéricas que faltan (sirven a cualquier juego), dibujar en
vectores lo que no necesita imagen, y entregar el tema concreto con sus assets **fuera del repo**. Se mantiene
la conexión con el servidor (estado, noticias, Play, progreso) y la estructura `core ← platform ← app ← main.c`.

Decisiones del usuario: Options = menú nativo del launcher; el tema propio va en carpeta aparte.

## Qué falta en el motor (brechas vs. mockup)

| Mockup | Hoy | Cambio genérico |
|---|---|---|
| Logo y Play salen del marco; fondo transparente | Ventana opaca, `SetWindowRgn` redondeado | `window.transparent: true` → ventana por píxel (WS_EX_LAYERED + `UpdateLayeredWindow`, DIB 32 bpp PARGB). Píxeles alfa 0 dejan pasar el clic |
| Marco amarillo→morado (`Layer 1.png`, 422 KB) | Solo `fill` plano | Campo de estilo `gradient: {"colors": [2–4 colores], "angle": 0–359}` (GDI+ line brush con preset blend). Se dibuja, no se usa la imagen |
| Lista de noticias con etiqueta, título y fecha | `news` = carrusel de una noticia | Nuevo tipo `news_list`: filas `row_height`, `tag_width`, `date_width`, `gap`; `tag_style` (fill/color/radius); clic en fila abre su link; hover por fila |
| Pestañas News/Events/Updates/Info, «+» | — | Acción `filter` con campo `category` (`""` = todas) y nuevo estado de estilo `selected` para la pestaña activa. `news` y `news_list` muestran solo esa categoría. «+» = `open_url` existente |
| Etiqueta «Info/Updates», fecha `10-28` | Noticia: image, title, link | Protocolo (aditivo): noticia `category` (texto corto) y `date` (`YYYY-MM-DD`). Etiqueta = string `news.category.<cat>` (o la categoría tal cual). Fecha con `date_format` (vars `{yyyy}{mm}{dd}`, por defecto `{mm}-{dd}`) |
| Language | Idioma = Windows | Acción `language`: menú nativo con los idiomas del tema (nombre = string `language.name` de cada idioma). Se guarda en `.launcher/prefs.json`; cambia textos, título y títulos de noticias sin reiniciar |
| Options | — | Acción `options`: menú nativo «Verificar y reparar archivos» (instalación sin fast path, sin lanzar el juego), «Abrir carpeta del juego» y, si `launcher.json` trae `settings: {exe, args}`, «Configuración del juego». Textos `menu.*` con fallback en inglés |
| Botones con imagen sin variante hover | Solo cambio de imagen/fill | Campos de estilo `brightness` (−100..100) y `opacity` (0..100) vía ColorMatrix: hover/pressed/disabled sin assets extra |

Todo es aditivo: `schema_version` sigue en 1; el tema nuevo pide `min_launcher_version: "0.3.0"` (los launchers viejos se auto-actualizan primero).

## Assets: cuáles se usan y qué se dibuja

- **Imagen (únicos del juego):** `Logo.png`, `play.png`, `options.png`, `lang.png`, `newaccount.png`, `socialmedia.png`, `store.png`, `x.png`, `-.png`.
- **Dibujado en vectores:** marco (`Layer 1.png` → `gradient` + borde negro 3 px + radio), panel blanco de noticias (`news.png`), panel inferior (`LOADERBARDOWN.png`), barra azul (`bar loading.png` → `progress` fill + radio), texto «loading…» (`loading_.png` → elemento `status` con fuente/color), pestañas, filas y etiquetas.
- Coordenadas medidas del mockup en píxeles lógicos; si la ventana no cabe en pantalla, el ajuste DPI existente la reduce.

## Fases

### Fase 1 · Modelo de tema (core, sin Windows) — S/M
- `src/core/theme.h`: `THEME_ST_SELECTED`; `THEME_EL_NEWS_LIST`; `THEME_ACT_FILTER/LANGUAGE/OPTIONS`; en `theme_style` gradiente (colores + ángulo), `brightness`, `opacity`; en `theme_element` `category`, campos de `news_list`, `tag_style`; `transparent` en el modelo de ventana.
- `src/core/theme.c` (estilos/ventana) y `src/core/theme_elements.c` (tabla `TYPES` + acciones), siguiendo el patrón «Adding an element type» de `docs/theming.md`.
- `docs/theme.schema.json` (bloques `if/then`), `tests/test_theme.c`, `tests/test_theme_schema.py`.
- Vigilar tamaño de `theme_model` (~47 KiB, estático) y los límites existentes.

### Fase 2 · Protocolo, filtros y preferencias (core + app) — M
- `src/core/status.c/.h`: `category` y `date` opcionales por noticia (validados; inválido = vacío); `tests/test_status.c`.
- `src/core/display.c/.h`: funciones puras `display_news_filter(...)` y formato de fecha; `tests/test_display.c`.
- `src/core/flow.c/.h`: evento `flow_on_repair` (solo en `ready`/`error`, instala forzando verificación completa, no lanza); `src/core/install.*` opción para saltar el fast path; `tests/test_flow.c`, `tests/test_install.c`.
- `src/core/config.c/.h`: `settings: {exe, args}` opcional reutilizando el parseo de `launch`; `tests/test_config.c`.
- Idioma como una sola fuente de verdad en app: nuevo `src/app/prefs.c/.h` (`.launcher/prefs.json`, escritura atómica como `state.json`); `app.c`/`window.c`/`preview.c` usan `prefs → sys_language` en vez de `sys_language` directo, y re-parsean el último status al cambiar idioma.
- `docs/spec.md` (noticia `category`/`date`, `settings` en `launcher.json`), `examples/dev-server/` (status con categorías y fechas).

### Fase 3 · Ventana Win32 — M/L
- `src/platform/win32/ui/ui.c`: modo transparente (layered, sin `SetWindowRgn`, render → `UpdateLayeredWindow`; con DPI y arrastre actuales); texto con antialias en escala de grises en ese modo.
- `ui_draw.c`: `fill_box` con gradiente; `draw_stretched` con ImageAttributes (brightness/opacity); `draw_news_list` y su hit test por fila (`g_ui.hover_row`); estado `selected` en botones `filter`; `news` respeta el filtro.
- Menús nativos (`TrackPopupMenu`) para `language` y `options` en `ui.c`; acciones despachadas en `src/app/window.c` (filtro activo, idioma, reparar, abrir carpeta con `ShellExecute`, lanzar `settings`). Nuevas importaciones en los `.def` de `user32`/`shell32`/`gdiplus` si hacen falta.
- `preview.c`: noticias de muestra con categorías/fechas para `--theme-preview`.

### Fase 4 · Ejemplo genérico, docs y versión — S/M
- `tools/make-showcase-theme.py` + `examples/theme-showcase/` (mismo layout con arte generado por código, sin nombre de juego); que el CI lo valide con el schema.
- `docs/theming.md` (todas las novedades), `CHANGELOG.md` `[Unreleased]`, `VERSION` 0.3.0 al publicar (según `RELEASING.md`, no se etiqueta sin pedirlo).
- `tools/check-denylist.sh` limpio.

### Fase 5 · Tema propio (fuera del repo) — S
- `~/Proyectos/TheEmpire/launcher-theme-<juego>/`: `theme.json` (+ `images/` con los 9 assets usados, nombres kebab-case), `README.md` con cómo previsualizar y publicar como blob en el feed server, y textos `en`/`es`.
- Los assets originales quedan sin tocar en `~/Downloads`.

## Qué NO entra
- Animaciones (brillos, pulsos) y GIFs: el motor dibuja estados, no animaciones.
- Formulario de registro/login dentro del launcher: «nueva cuenta», redes y tienda son `open_url`.
- Pestañas con contenido distinto a noticias (p. ej. HTML): las pestañas solo filtran noticias por categoría.
- Traducir los textos horneados en las imágenes (Play/Options/Language): es arte del juego.

## Ejecución (reglas de contexto)
Primero un agente escribe `docs/temas-0.3/contexto.md` (≤2 páginas: capas, convenciones C11 sin CRT, límites, rutas, qué no tocar) y este plan va a `docs/temas-0.3/plan.md`. Luego un agente nuevo por tarea acotada (F1; F2 status+display; F2 flow+install+config; F2 prefs; F3 ventana layered; F3 dibujo; F3 menús/acciones; F4; F5), cada uno con el contexto + resumen de traspaso del anterior. Builds y tests en segundo plano; comprobaciones pasa/no pasa con Sonnet. Un commit por fase.

## Verificación
1. `./build.sh` y `tests/run.sh` verdes (incluye nuevos tests de theme, status, display, flow, config); `python3 tests/test_theme_schema.py` valida `examples/*` y el tema propio; `tools/check-denylist.sh` limpio.
2. `wine build/launcher.exe --theme-preview ~/Proyectos/TheEmpire/launcher-theme-<juego>` y captura comparada con el mockup: logo sobresale, esquinas transparentes, degradado, lista con pestañas, barra y «loading…»; `--preview-state ready/installing/locked` y `--lang es`.
3. Pestañas filtran; clic en fila abre link; Language cambia idioma al vuelo y persiste al reabrir; Options → reparar (re-hash completo y estado `ready`), abrir carpeta, configuración del juego.
4. Contra `examples/dev-server` (con categorías/fechas): Play instala y lanza `fake-game`, progreso visible; un launcher 0.2.0 con el tema nuevo se auto-actualiza por `min_launcher_version`.
5. Ventana a 100 %, 150 % y en pantalla 1366×768 (se reduce y Cerrar sigue accesible).
