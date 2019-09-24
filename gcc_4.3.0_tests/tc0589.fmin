int printf (const char *, ...);
struct S1
{
    int f0;
    int f1;
    int f2;
    int f3;
    int f4;
};
int a;
void
fn1 ()
{
    struct S1 b = { };
    struct S1 *c = &b;
    int *d[][1] = { 0, &b.f0 };
    *c = b;
    int **e = &d[0][0];
    *e = 0;
    a = b.f2;
}

int
main ()
{
    fn1 ();
    printf ("%d\n", a);
    return 0;
}
