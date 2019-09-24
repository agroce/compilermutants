This repository contains ground truth information for triaging bugs in Spidermonkey 1.6 and GCC 4.3.0, plus mutants of the compiler source code.  These benchmarks were used in these papers:

- Josie Holmes and Alex Groce. Causal Distance-Metric-Based Assistance for Debugging After Compiler Fuzzing.
IEEE International Symposium on Software Reliability Engineering, pages 166-177, Memphis, Tennessee, October 2018.

- Yuanli Pei, Arpit Christi, Xiaoli Fern, Alex Groce, and Weng-Keen Wong. Taming a Fuzzer Using Delta Debugging
Trails. International Workshop on Software Mining, Shenzhen, China, December 2014.

- Yang Chen, Alex Groce, Chaoqiang Zhang, Weng-Keen Wong, Xiaoli Fern, Eric Eide, and John Regehr. Taming
Compiler Fuzzers. ACM SIGPLAN Conference on Programming Language Design and Implementation, pages
197-208, Seattle, Washington, June 2013.

The mutants subdirectories, given source code for the relevant compilers, should be fairly obvious in meaning.  The tests directories contain the "tamed" bugs.  The `.fmin` files containt the minimized failing inputs, the `.rev` files contain a unique identifier, indicating which bug the `.fmin` file triggers, and the `.cov` files contain code coverage for executing the failing tests.
