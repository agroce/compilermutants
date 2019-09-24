int printf (const char *, ...);
int a;
static const int *b = &a;
int
main ()
{
    int *c = &a;
    a = 1;
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
