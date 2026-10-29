/* Host-only: module entry detection and Line parsing of module_patches.h. */
#include <assert.h>
#include <stdio.h>
#include "../src/module_patches.h"

int main(void) {
    (void)xm_merge; (void)xm_title_is; (void)gp_main_entry; (void)gp_setting_set; (void)gp_same_resolution;
    (void)gp_setting; (void)gp_list; (void)gp_game_version; (void)gp_sfo;
    assert(mp_is_module("PreyDll.prx") && mp_is_module("a.SPRX") && !mp_is_module("eboot.bin") && !mp_is_module(".prx"));
    const char *x = "<Metadata Title=\"Prey\" Name=\"T\" AppVer=\"01.12\" AppElf=\"PreyDll.prx\"><PatchList>"
                    "<Line Type=\"bytes\" Address=\"0x2227001\" Value=\"52 0A\"/>"
                    "<Line Type=\"bytes32\" Address=\"10\" Value=\"0x1234\"/>"
                    "<Line Type=\"float32\" Address=\"0x20\" Value=\"1.0\"/>"
                    "<Line Type=\"utf8\" Address=\"0x30\" Value=\"ab\"/></PatchList></Metadata>";
    int n; mp_line *l = mp_lines(x, x + strlen(x), &n);
    assert(l && n == 4);
    assert(l[0].offset == 0x2227001 && l[0].len == 2 && l[0].data[0] == 0x52 && l[0].data[1] == 0x0a);
    assert(l[1].offset == 0x10 && l[1].len == 4 && l[1].data[0] == 0x34 && l[1].data[1] == 0x12 && !l[1].data[2]);
    float f; memcpy(&f, l[2].data, 4); assert(l[2].len == 4 && f == 1.0f);
    assert(l[3].len == 2 && !memcmp(l[3].data, "ab", 2));
    assert(!l[0].module[0]);
    free(l);
    const char *y = "<Metadata AppElf=\"engine.prx\"><PatchList><Line Type=\"bytes\" Address=\"0x10\" Value=\"01\"/>"
                    "<Line Type=\"bytes\" Address=\"0x20\" Value=\"02\" Module=\"materialsystem_ps4.prx\"/></PatchList></Metadata>";
    l = mp_lines(y, y + strlen(y), &n);
    assert(l && n == 2 && !l[0].module[0] && !strcmp(l[1].module, "materialsystem_ps4.prx") && l[1].offset == 0x20);
    free(l);
    const char *bad[] = {"<Line Type=\"mask\" Address=\"0\" Value=\"00\"/>", "<Line Type=\"bytes\" Address=\"0\" Value=\"123\"/>",
                         "<Line Type=\"bytes\" Address=\"zz\" Value=\"00\"/>"};
    for (int i = 0; i < 3; i++) assert(!mp_lines(bad[i], bad[i] + strlen(bad[i]), &n));
    puts("module_patches_test: ok");
    return 0;
}
