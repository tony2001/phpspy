#include "phpspy.h"
#ifdef USE_LIBDW

typedef struct {
  const char *symbol;
  uint64_t   *raddr;
} dwarf_callback_args;

static int dwarf_module_callback(
    Dwfl_Module *mod,
    void **unused __attribute__((unused)),
    const char *name __attribute__((unused)),
    Dwarf_Addr start __attribute__((unused)),
    void *arg
);

static char *debuginfo_path = NULL;
static const Dwfl_Callbacks proc_callbacks = {
    dwfl_linux_proc_find_elf,
    dwfl_standard_find_debuginfo,
    NULL,
    &debuginfo_path
};

int get_symbol_addr(pid_t pid, const char *symbol, uint64_t *raddr) {
    Dwfl *dwfl = NULL;
    int ret = 0;
    const char *msg;
    dwarf_callback_args args = { symbol, raddr };

    do {
        int err = 0;
        dwfl = dwfl_begin(&proc_callbacks);
        if (dwfl == NULL) {
            fprintf(stderr, "get_symbol_addr: Error setting up DWARF reading. Details: %s\n", dwfl_errmsg(0));
            ret = 1;
            break;
        }

        err = dwfl_linux_proc_report(dwfl, pid);
        if (err != 0) {
            fprintf(stderr, "get_symbol_addr: Error reading from /proc. Details: %s\n", (msg = dwfl_errmsg(0)) ? msg : strerror(err));
            ret = 1;
            break;
        }

        if (dwfl_report_end(dwfl, NULL, NULL) != 0) {
            fprintf(stderr, "get_symbol_addr: Error reading from /proc. Details: %s\n", dwfl_errmsg(0));
            ret = 1;
            break;
        }

        *raddr = 0;
        if (dwfl_getmodules(dwfl, dwarf_module_callback, &args, 0) == -1) {
            fprintf(stderr, "get_symbol_addr: Error reading DWARF modules. Details: %s\n", dwfl_errmsg(0));
            ret = 1;
            break;
        } else if (*raddr == 0) {
            fprintf(stderr, "get_symbol_addr: Unable to find address of %s in the binary\n", symbol);
            ret = 1;
            break;
        }
    } while (0);

    dwfl_end(dwfl);
    return ret;
}

static int dwarf_module_callback(
    Dwfl_Module *mod,
    void **unused __attribute__((unused)),
    const char *name __attribute__((unused)),
    Dwarf_Addr start __attribute__((unused)),
    void *_args
) {
    dwarf_callback_args *args = (dwarf_callback_args *) _args;
    GElf_Sym sym;
    GElf_Addr value = 0;
    int i, n = dwfl_module_getsymtab(mod);

    for (i = 1; i < n; ++i) {
        const char *symbol_name = dwfl_module_getsym_info(mod, i, &sym, &value, NULL, NULL, NULL);
        if (symbol_name == NULL || symbol_name[0] == '\0') {
            continue;
        }
        switch (GELF_ST_TYPE(sym.st_info)) {
        case STT_SECTION:
        case STT_FILE:
        case STT_TLS:
            break;
        default:
            if (!strcmp(symbol_name, args->symbol) && value != 0) {
                *(args->raddr) = value;
                return DWARF_CB_ABORT;
            }
            break;
        }
    }

    return DWARF_CB_OK;
}

#else
typedef int __no_libdw;
#endif
