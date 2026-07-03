#include <stdio.h>
#include <stdint.h>
int main(void) {
    uint32_t n = 0x12345678;
    FILE *f = fopen("mem", "wb");
    fwrite(&n, sizeof(n), 1, f);
    fclose(f);
}
