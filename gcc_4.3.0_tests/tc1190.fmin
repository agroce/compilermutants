int printf (const char *, ...);
int a, b = 1;
static const int *c = &b;
int
main ()
{
    b = 0;
    a = *c;
    b++;
    printf ("%d\n", a);
    return 0;
}
