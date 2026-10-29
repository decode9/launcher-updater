# Contexto para agentes · temas-0.3

Léelo en lugar de recorrer el repo. Plan aprobado: `docs/temas-0.3/plan.md`. Si necesitas algo que no está
aquí, lee solo ese tramo (`grep -n`, `sed -n a,bp`), nunca archivos enteros grandes (`install.c` 881 líneas,
`theme.c` 629, `ui.c` 594).

## Qué es
Launcher genérico de Windows (i686, GUI) en C11 **sin CRT ni SDK**: habla con un feed server (`/v1/status`,
blobs por SHA-256), instala releases de forma atómica/journaled y dibuja una ventana definida 100 % por
`theme.json`. El juego concreto nunca se nombra en el repo (ver denylist).

## Capas y regla de dependencia (`core ← platform ← app ← main.c`)
- `src/core/` — portable, sin cabeceras de plataforma (invariante I-2), se compila en el host para tests.
  `theme.h/.c` (modelo y parseo), `theme_elements.c` (tabla `TYPES` de tipos + acciones de botón),
  `theme_internal.h` (`theme_ctx`, `theme_ctx_string/uint/blob`, `theme_type_entry`), `status.c/.h`
  (`/v1/status`, noticias), `display.c/.h` (funciones puras: tamaños, %, cuenta atrás, `display_action_enabled`,
  escala DPI), `flow.c/.h` (máquina de estados de la ventana: eventos → `flow_step`), `install.c/.h` (motor),
  `state.c/.h` (`state.json`), `config.c/.h` (`launcher.json`), `json`, `str`, `sha256`, `path`, `manifest`…
- `src/platform/win32/` — Win32 a mano: `win32.h` (tipos + imports kernel32), `fs`, `sys` (flags, idioma,
  ticks), `proc`, `thread`, `http/tls/trust`, `install_port.c` (puertos fs/net del motor), `rt.c`
  (memset/memcpy/memmove/memcmp, `_fltused`), `*.def` (imports). Solo esta carpeta incluye `win32.h`.
  - `ui/`: `ui.h` (API pública de la ventana), `ui.c` (ventana WS_POPUP, `SetWindowRgn` redondeado, back buffer
    DIB + `BitBlt`, buzón entre hilos `g_box` + `WM_UI_MAIL`, `activate()` despacha clics, `open_url` con
    `ShellExecuteW`, hover/pressed, DPI en `apply_window`), `ui_draw.c` (`fill_box`, `draw_border`,
    `draw_stretched`, `draw_cover`, `draw_text`, `draw_news`, `draw_progress`, `draw_element`, `ui_draw`,
    `ui_hit`, `interaction()` → estado de estilo, `ui_enabled`), `ui_res.c` (GDI+ start, decodificar imágenes a
    PARGB, fuentes de blob/sistema, imágenes de noticias), `ui_internal.h` (`ui_window g_ui`: lang, scale,
    state, news[], hover, pressed, images[], fonts[]; `g_ui_theme`), `gdiplus.h` y `winui.h` (declaraciones).
- `src/app/` — orquestación (puede usar core y platform): `app.c` (`app_fetch_status`, `app_parse_status`,
  `app_install_release`, `app_release_is_current`, `app_status`/`app_config` globales), `window.c` (modo
  ventana: hilo UI + un worker), `preview.c` (`--theme-preview`), `cache.c/.h` (`.launcher/cache/`),
  `selfupdate.c`, `log.c`. `app_internal.h` = estado compartido.
- `src/main.c` — solo elige modo: `--theme-preview`, `--swap`, `--install`, `--status`, o ventana.

## Convenciones (obligatorias)
- **Cabecera por archivo**: bloque `/* nombre.c - Qué hace y por qué ... */` en inglés; comentarios en inglés,
  explicando el porqué y contratos (ver `theme.h`, `install.h`). Docs de diseño en español en `docs/<tema>/`.
- Nombres: `snake_case`, prefijo de módulo (`theme_`, `status_`, `display_`, `flow_`, `ui_`, `app_`, `cache_`);
  enums/consts `MODULO_MAYUS` (`THEME_ACT_*`, `THEME_ST_*`, `THEME_EL_*`, `FLOW_*`); globales de archivo `g_`;
  `static` para todo lo no exportado. Guardas `LAUNCHER_CORE_X_H` / `LAUNCHER_WIN32_X_H` / `LAUNCHER_APP_X_H`.
- **Sin asignación dinámica**: buffers `static` o del llamante, límites fijos con `#define` (p. ej.
  `THEME_MAX_ELEMENTS 64`, `THEME_MAX_STRINGS 128`, `THEME_POOL_BYTES 32768`, `STATUS_NEWS_MAX 16`,
  `STATUS_TITLE_BYTES 256`, `CONFIG_ARGS_MAX 16`). Lo que excede el límite se ignora con un aviso, no es error.
  `theme_model` ≈47 KiB, siempre `static` (hay varias copias: `g_theme`, `g_next`, `g_ui_theme`, `g_box_theme`);
  vigila su crecimiento.
- **Sin CRT**: en core usa `core/str.h` (`str_len`, `str_eq`, `str_copy`, `str_copy_n`, `str_starts_with`,
  `mem_eq`, `u64_to_dec`, `dec_to_u64`, `hex_*`, `strbuf` + `sb_add*`). En core no hay `memcpy`: se copia con
  bucles/structs (`theme.c` tiene `zero()`, `install.c` `copy_bytes()`). En platform/app, `memset/memcpy/memmove`
  vienen de `platform/win32/rt.h`. **Sin división de 64 bits** (no hay `__aulldiv`): shifts y 32 bits.
- Compilación: `clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -std=c11 -Wall -Wextra
  -Werror`. Cualquier warning rompe el build. **Marco de pila > 4 KiB = fallo de enlace** (no hay `__chkstk`):
  buffers grandes van `static`.
- **Imports Win32/GDI+**: añadir una función = (1) línea `Nombre@N` (stdcall, N = bytes de argumentos) en el
  `.def` de su DLL en `src/platform/win32/` (`user32.def`, `gdi32.def`, `shell32.def`, `gdiplus.def`…; build.sh
  los pasa por `llvm-dlltool`), y (2) `IMPORT tipo WINAPI Nombre(...);` en `win32.h` (kernel32), `ui/winui.h`
  (user32/gdi32/shell32/shlwapi) o `ui/gdiplus.h` (GDI+, con sus tipos/constantes a mano). Funciones que no
  existen en XP/7 se cargan con `GetProcAddress` (patrón `system_dpi()` en `ui.c`), nunca import estático.
- Core tolerante: claves desconocidas se ignoran; valor inválido → default + flag `THEME_WARN_*`; todo cambio de
  tema/protocolo es **aditivo** (`schema_version` sigue en 1; versiones viejas ignoran lo nuevo).

## Tests y comandos
- `./build.sh` → `build/launcher.exe` (clang + lld-link + llvm-rc; también embebe `examples/theme-neutral`).
- `tests/run.sh` → compila `src/core/*.c` + cada `tests/test_*.c` con libc del host y ASan/UBSan; luego
  `tests/test_theme_schema.py` (valida `examples/theme-*/theme.json` y `examples/dev-server/example-root/theme.json`
  contra `docs/theme.schema.json` y comprueba blobs; sin `jsonschema` se salta esa parte salvo
  `REQUIRE_JSONSCHEMA=1`, como en CI). Solo core es testeable en host; app/ui se prueban con wine.
- Test nuevo: `tests/test_<x>.c` con `#include "testlib.h"`, `TEST(nombre) { CHECK(..); CHECK_EQ_U64(a,b);
  CHECK_STR(a,b); }`, `int main(void){ RUN(nombre); ... return test_report(); }` (ver final de un test existente).
  run.sh recoge `test_*.c` solo. Hoy ≈112 tests (theme 11, status 9, display 8, flow 22, install 24, config 6…).
- `tools/check-denylist.sh`: falla si cualquier archivo (también `build/launcher.exe`) contiene un término de
  `tools/denylist.txt` (base64). **Nunca escribas el nombre del juego** en el repo (ni código, ni docs, ni nombres
  de carpeta/archivo, ni commits). El tema propio vive fuera: `~/Proyectos/TheEmpire/launcher-theme-<juego>/`.
- Wine: `/usr/bin/wine build/launcher.exe --theme-preview <carpeta> [--preview-state ready|installing|locked|
  checking|error] [--lang es]`. Builds/tests largos en segundo plano.
- Commits: uno por fase, estilo `Phase N (temas-0.3): ...`.

## Cómo fluye el tema
1. **Parse** (`theme_parse`): ventana (`parse_window`: width/height/radius/draggable/background), tokens, fuentes,
   strings por idioma, elementos. Por elemento (`parse_element` en `theme.c`): tipo por `theme_type_find`, `rect`
   (recortado a la ventana), `text`, `visible_when`, `align`; estilo base (color/font del elemento) → `normal` →
   cada estado (`hover/pressed/disabled`) hereda de normal vía `overlay_style` (fill, color, border, track,
   border_width, radius, image, font). Nombres en `STYLE_STATES[THEME_ST_COUNT]`. Luego la `parse` del tipo
   (`theme_elements.c`): `parse_button` valida `action` contra su tabla `actions[]` y `url` https. Patrón «Adding
   an element type» en `docs/theming.md` §188 (enum, parse fn, línea en `TYPES`, dibujo, schema `if/then`, docs,
   test). Textos: `theme_text(theme, lang, key)` con cadena lang → base → `en` → la clave.
2. **Blobs requeridos**: `theme_required_blobs` (imágenes y fuentes deduplicadas). Si añades campos con blob,
   regístralos con `theme_ctx_blob` para que entren aquí.
3. **Caché** (`window.c`): `load_start_theme` usa `.launcher/cache/theme.json` si parsea, no pide launcher más
   nuevo y tiene todos los blobs; si no, el neutral embebido. `refresh_theme` (worker): si cambió el SHA del tema
   del status → descarga, parsea en `g_next`, comprueba `min_launcher_version`, `blobs_cached(&g_next,1)`,
   guarda, copia a `g_theme` y `ui_set_theme`.
4. **UI**: `ui_set_theme` copia al buzón; el hilo UI aplica (`apply_mail` → `ui_res_load_theme`, `apply_window`)
   y repinta. `ui_draw` aplica la escala DPI con una transformación de mundo; `ui_hit` desescala el ratón.

## Ventana, acciones, noticias y estado
- Hilo UI (dueño de la ventana) + un worker (`window.c: worker → run_flow`) que hace red e instalación en serie
  (HTTP no es reentrante; `app_status` y los buffers de `app.c` solo los toca el worker). `ui_set_*` son seguras
  desde cualquier hilo (buzón con spin lock; gana el último valor).
- Clic: `ui.c: activate(i)` → `news` abre el link de la noticia actual; `MINIMIZE` y `OPEN_URL` se resuelven en
  `ui.c`; `PLAY` y `EXIT` llegan a `cfg.on_action` → `window.c: on_action`, que **solo señala** al worker
  (`g_play`, `g_cancel`, `event_set(&g_wake)`); nunca bloquea el hilo UI. Habilitado: `ui_enabled` →
  `display_action_enabled(action, state, play_enabled)`. Estado de estilo: `interaction()` en `ui_draw.c`.
- Flujo (`core/flow.h`): `flow_start` → FETCH → `fetch_facts` (status, auto-update, `refresh_theme`,
  `refresh_news`, facts) → `flow_on_status` → WAIT (`wait_step`, despierta por Play o timeout) / INSTALL
  (`install_step` → `app_install_release`) / LAUNCH (`launch_game`: `launch.exe` + args con `{server}`) / CLOSE.
  `apply_view` lleva `flow_view` (screen, key de status, play_enabled, countdown) a la UI.
- Noticias: `status_parse` elige el título por idioma (`pick_title`: lang → `en` → primero); link inválido = sin
  link. `refresh_news` cachea imágenes y solo reenvía con `ui_set_news` si la lista cambió
  (`status_news_equal`). En la UI el carrusel usa `g_ui.news_index` y `interval`.
- **Idioma hoy**: `sys_language` (ISO 639-1 de Windows) se llama en `window.c:app_window` (`g_lang`, pasado como
  `cfg.lang`), en `app.c:app_fetch_status` y `app.c:app_parse_status` (buffer local `char lang[8]`, menor que
  `THEME_NAME_BYTES`=32: ojo con `pt-BR`) y en `preview.c:app_preview` (`--lang` lo sobrescribe). `g_ui.lang`
  se copia en `ui_create`; no hay setter para cambiarlo después (habrá que añadir `ui_set_lang` al buzón).
- `install.h`: fast path = `is_current(version)` al inicio de `run()` (`install.c` ~799; `res->fast_path`); además
  `plan()` da por presente un archivo si `state_matches` (sha+tamaño registrados y tamaño en disco) **sin
  rehashear**. Una reparación con verificación completa debe saltarse ambas cosas (forzar `file_is`). Añade la
  opción como **último campo** de `install_ctx`: `app.c` lo inicializa posicionalmente (`{&install_files,
  &install_blobs, progress, user, cancel, app_exe_name}`), así los demás quedan a 0.
- `config.c: parse_launch` escribe directo en `cfg->launch_*`; para `settings` hay que factorizarlo para recibir
  el destino (exe + argc + args) y reutilizarlo. `launch.exe` debe pasar `path_is_safe_relative`.
- Escritura atómica: `cache_write_atomic(path, data, len)` (app/cache.h: `.part` + flush + `fs_rename`); en core,
  `install.c: save_state` (tmp + rename por puerto). `prefs.json` debe ir por `cache_write_atomic` en
  `.launcher/` (ruta nativa con `fs_join(app_dir, ...)`).
- Rutas/limites útiles: `APP_STATUS_MAX 65536`, `UI_STATUS_BYTES 256`, `FS_PATH_BYTES 1024`, `CACHE_DIR
  ".launcher/cache"`, estado `.launcher/state.json`.

## Qué NO tocar
- Endurecimiento de 0.2.0 (`docs/robustez/plan.md`, invariantes I-1..I-5): atomicidad/journal/orden de
  `install.c` (recover, `retire_orphans` + `owned`, `.part`, `state.json` ilegible → IO, corrupto → `.bad`),
  validación de `manifest.c`, `loadcfg.c` y `/dependentloadflag`, `sys_harden_dll_search`, `trust.c`
  (firmante + revocación), `instance.c` (instancia única, código 4, clase por carpeta), bucle de auto-update en
  `selfupdate.c`, cierre con espera de 3 s y `proc_terminate_self`, pinning TLS (`tls.c`, `http.c`).
  La opción de reparar solo **añade** un camino que no use atajos; no cambia el orden ni las garantías.
- Flags del linker y de compilación de `build.sh`; no añadir CRT, SDK ni librerías.
- No usar `GetWindowRect` y similares sin declararlos/importarlos; no imports estáticos de APIs posteriores a XP.
- `schema_version` (tema 1, status 1): no subirlos; todo es aditivo. No romper temas existentes en `examples/`.
- Assets originales en `~/Downloads/Launcher/Launcher/`: solo lectura.
