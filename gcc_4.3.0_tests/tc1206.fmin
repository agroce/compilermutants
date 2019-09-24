int printf (const char *, ...);
struct
{
    int f5:1;
}
a;
int b, d;
void
fn1 (p1)
{
    int c;
    c = 1;
    for (; c >= 0; c--)
        b = a.f5 = p1;
}

int
main ()
{
    fn1 (d | 1);
    printf ("%d\n", b);
    return 0;
}
