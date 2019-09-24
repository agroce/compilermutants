int printf (const char *, ...);
short a;
short *b = &a;
unsigned long long **c;
unsigned long long ***d;
void fn1 ();
void
fn2 ()
{
    int e = 4;
    fn1 (&e);
    *b = e ^ (d = &c) == &c;
}

void
fn1 ()
{
}

int
main ()
{
    fn2 ();
    printf ("%d\n", a);
    return 0;
}
