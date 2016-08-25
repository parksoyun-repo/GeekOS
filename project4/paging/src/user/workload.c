#include <conio.h>
#include <string.h>

void Recurse(int x)
{
    volatile int stuff[512];

    if (x == 0) return;

    stuff[0] = x;
    Recurse(x-1);
}

int main(int argc, char **argv)
{
    int depth = 1024;

    if (argc > 1) {
	depth = atoi(argv[1]);
	Print("Depth is %d\n", depth);
    }

    Recurse(depth);

    return 0;
}
