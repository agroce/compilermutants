int printf (const char *, ...);
struct S3
{
    int f1;
    int f4;
    long f5;
    int f6;
}
a, *b = &a;
int
main ()
{
    struct S3 c = { 0, 0, 0, 1 };
    *b = c;
    *b = *b;
    printf ("%d\n", a.f6);
    return 0;
}
