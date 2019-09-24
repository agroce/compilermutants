int printf (const char *, ...);
int a, b;
static int *c = &b;
static const int *d = &b;
int
main ()
{
    a = 0;
    for (; a != 1; a++)
    {
        b = 1;
        if (*d)
            break;
    }
    *c ^= 0;
    printf ("%d\n", a);
    return 0;
}
