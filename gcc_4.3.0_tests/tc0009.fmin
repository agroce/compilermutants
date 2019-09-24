int printf (const char *, ...);
union
{
    int f1;
    int f2;
}
b;
unsigned char *a;
unsigned char **c;
int *d = &b.f2;
int
main ()
{
    *d = -(&a == (c = &a));
    printf ("%d\n", b.f1);
    return 0;
}
