int printf (const char *, ...);
union
{
    char f0;
    int f2;
}
a;
char *b = &a.f0;
char **c = &b;
int
main ()
{
    char ***d = &c;
    int e = 6L;
    ***d = d == &c ^ e;
    printf ("%d\n", a.f2);
    return 0;
}
