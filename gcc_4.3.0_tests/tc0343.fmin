int printf (const char *, ...);
int a = 3;
static const int *b = &a;
int
main ()
{
    int *c = &a, *d = &a;
    if (*d)
        *d = *b != 0;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
