#include <stdio.h>
int main(void)
{
    char ln[4096];
    while (fgets(ln, sizeof(ln), stdin)) fflush(stdout);
    return 0;
}
