int printf (const char *, ...);
int a[3];
static int *b = &a[2];
void
fn1 ()
{
    *b = 1;
}

int
main ()
{
    int *c = &a[2];
    fn1 ();
    *b = *c;
    printf ("%d\n", a[2]);
    return 0;
}
