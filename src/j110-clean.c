#include <stdio.h>

int main(void)
{
    fputs("Cleaning is disabled: HP's J110 profile uses the DMF workflow, and this utility does not implement it. No command was sent.\n",
          stderr);
    return 2;
}
