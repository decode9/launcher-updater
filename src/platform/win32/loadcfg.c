/*
 * loadcfg.c - The PE load configuration directory, declared by hand because there is no C
 * runtime to provide it.
 *
 * The linker points the image's Load Config data directory at the symbol _load_config_used.
 * Without it lld-link ignores /dependentloadflag ("will have no effect"), so the loader would
 * resolve the static imports (crypt32, wintrust, secur32, gdiplus...) with the default DLL
 * search order, which starts in the folder of the exe: a DLL planted next to launcher.exe
 * (e.g. in Downloads) would be loaded. build.sh links with /dependentloadflag:0x800
 * (LOAD_LIBRARY_SEARCH_SYSTEM32) and lld-link writes that value into DependentLoadFlags below.
 *
 * Layout: IMAGE_LOAD_CONFIG_DIRECTORY32 up to SEHandlerCount (72 bytes, the Windows 7 form).
 * Size tells the loader how many fields are present; later fields (CFG, CHPE...) are absent
 * and read as zero. The SafeSEH table stays empty because the build links with /safeseh:no
 * and nothing here installs SEH handlers. SecurityCookie is 0: there is no /GS cookie.
 */
#include <stdint.h>

typedef struct {
    uint32_t size;
    uint32_t time_date_stamp;
    uint16_t major_version, minor_version;
    uint32_t global_flags_clear;
    uint32_t global_flags_set;
    uint32_t critical_section_default_timeout;
    uint32_t de_commit_free_block_threshold;
    uint32_t de_commit_total_free_threshold;
    uint32_t lock_prefix_table;
    uint32_t maximum_allocation_size;
    uint32_t virtual_memory_threshold;
    uint32_t process_heap_flags;
    uint32_t process_affinity_mask;
    uint16_t csd_version;
    uint16_t dependent_load_flags; /* offset 0x36; filled in by lld-link /dependentloadflag */
    uint32_t edit_list;
    uint32_t security_cookie;
    uint32_t se_handler_table;
    uint32_t se_handler_count;
} load_config32;

_Static_assert(sizeof(load_config32) == 72, "IMAGE_LOAD_CONFIG_DIRECTORY32 (Windows 7 size)");
_Static_assert(__builtin_offsetof(load_config32, dependent_load_flags) == 0x36,
               "DependentLoadFlags offset");

/* The name is fixed by the linker (x86 decoration adds the leading underscore: __load_config_used).
 * dependent_load_flags is also set here so the value does not depend on the linker flag alone. */
const load_config32 _load_config_used = {
    .size = sizeof(load_config32),
    .dependent_load_flags = 0x800, /* LOAD_LIBRARY_SEARCH_SYSTEM32 */
};
