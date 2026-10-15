#include <assert.h>
#include <stdio.h>
#include "../src/xml_merge.h"

static const char target[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Patch>\n    <TitleID>\n        <ID>CUSA00207</ID>\n    </TitleID>\n"
    "    <Metadata Title=\"Bloodborne\"\n              Name=\"Skip Intro\"\n              Author=\"illusion\"\n"
    "              AppVer=\"01.09\">\n        <PatchList>\n            <Line Type=\"bytes\" Address=\"0x1\" Value=\"0x00\"/>\n"
    "        </PatchList>\n    </Metadata>\n"
    "    <Metadata Title=\"Bloodborne\" Name=\"4K (PS4 Pro mode)\" Note=\"a > b\" AppVer=\"01.09\">\n"
    "        <PatchList><Line Type=\"bytes\" Address=\"0x2\" Value=\"0x01\"/></PatchList>\n    </Metadata>\n</Patch>\n";

static const char ours[] =
    "<Patch>\n  <Metadata Title=\"Bloodborne\" Name=\"4K (PS4 Pro mode)\" Note=\"a > b\" AppVer=\"01.09\">\n"
    "    <PatchList><Line Type=\"bytes\" Address=\"0x2\" Value=\"0x02\"/></PatchList>\n  </Metadata>\n"
    "  <Metadata Title=\"Bloodborne\" Name=\"1440p (PS4 Pro mode)\" AppVer=\"01.09\">\n"
    "    <PatchList><Line Type=\"bytes\" Address=\"0x3\" Value=\"0x03\"/></PatchList>\n  </Metadata>\n</Patch>\n";

int main(void) {
    assert(xm_title_is(target, sizeof target - 1, " bloodborne", 11));
    assert(!xm_title_is(target, sizeof target - 1, "Bloodborne: Network Test", 24));
    char *out; size_t n; int added, replaced;
    assert(!xm_merge(target, sizeof target - 1, ours, sizeof ours - 1, &out, &n, &added, &replaced));
    assert(added == 1 && replaced == 1 && strlen(out) == n);
    assert(strstr(out, "Name=\"Skip Intro\""));                       /* other entries kept */
    assert(strstr(out, "Value=\"0x02\"") && !strstr(out, "Value=\"0x01\""));
    assert(strstr(out, "Name=\"1440p (PS4 Pro mode)\"") < strstr(out, "</Patch>"));
    assert(strstr(out, "<ID>CUSA00207</ID>"));

    char *again; size_t m;
    assert(!xm_merge(out, n, ours, sizeof ours - 1, &again, &m, &added, &replaced));
    assert(added == 0 && replaced == 0 && m == n && !memcmp(again, out, n));   /* second merge changes nothing */
    free(again); free(out);

    assert(xm_merge("<Patch>", 7, ours, sizeof ours - 1, &out, &n, &added, &replaced) == -1);   /* no </Patch> */
    puts("PASS XML merge: replace by Name and AppVer, add missing entries, keep others, idempotent");
    return 0;
}
