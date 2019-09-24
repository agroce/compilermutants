int printf (const char *, ...);
int a;
static const int *b = &a;
int
main ()
{
    int *c = &a, *d = &a;
    *c = 1;
    *d = *b;
    printf ("%d\n", a);
    return 0;
}
