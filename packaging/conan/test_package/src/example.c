// Smoke test for the packaged libleptris: parse a document through the
// public API and free it.
#include <leptris.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    const char* xml = "<?xml version=\"1.0\"?>\n"
                      "<catalog><book id=\"1\">leptris</book></catalog>\n";

    LeptrisStatus status = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (status != LEPTRIS_OK || !doc) {
        fprintf(stderr, "parse failed (status %d)\n", (int)status);
        return 1;
    }
    printf("parsed %zu bytes\n", strlen(xml));
    leptris_document_free(doc);
    return 0;
}
