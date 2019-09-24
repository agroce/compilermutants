int printf (const char *, ...);
union U4
{
    int f0;
    int f1;
};
int a;
int *b;
int
fn1 (union U4 p1)
{
    for (; a != 1; a = 1)
        if (p1.f0)
            return 0;
    return 0;
}

int
main ()
{
    union U4 c = { };
    union U4 *d = &c;
    int *e = &c.f0;
    *d = c;
    fn1 (*d);
    b = e;
    printf ("%d\n", a);
    return 0;
}
