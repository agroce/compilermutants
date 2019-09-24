int printf (const char *, ...);
int a = 2;
unsigned short **b;
int *c = &a;
void fn1 ();
void
fn2 (p1)
{
    fn1 (a > p1);
}

void
fn1 (p1)
{
    *c = p1;
}

int
main ()
{
    unsigned short ***d = &b;
    fn2 (d == &b);
    printf ("%d\n", a);
    return 0;
}
