int printf (const char *, ...);
int a;
static const int *b = &a;
static int *c = &a;
void
fn1 ()
{
    int *d = &a;
    *d = 1;
}

int
main ()
{
    fn1 ();
    *c = *b;
    printf ("%d\n", a);
    return 0;
}
