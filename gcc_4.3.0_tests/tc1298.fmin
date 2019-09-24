int printf (const char *, ...);
int a = 1;
static const int *b = &a;
int
main ()
{
    int *c = &a;
    a = 0;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
