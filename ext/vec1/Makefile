
CC = gcc

CFLAGS += -O3
CFLAGS += -DNDEBUG
CFLAGS += -Wextra -Werror
CFLAGS += -g

#CFLAGS += -DVEC1_THREADS=0
#CFLAGS += -fprofile-arcs -ftest-coverage -fprofile-update=atomic -lgcov
#CFLAGS += -fno-inline

#ASANFLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined
#CFLAGS += $(ASANFLAGS)

SIMDCFLAGS = -mavx2 -mfma

SRC = vec1.c

vec1.so: $(SRC)
	$(CC) $(CFLAGS) $(SIMDCFLAGS) $(SRC) -o vec1.so -shared -fPIC -lm

vec1.s: $(SRC)
	$(CC) $(CFLAGS) -g0 $(SIMDCFLAGS) $(SRC) -S

# Method for building multi-arch loadable libraries.
#
# To build a multi-arch library, the "vec1.c" file must be compiled twice.
# Once with VEC1SIMD defined as SCALAR, and once as AVX2. The -DVEC1SMD=AVX2
# compilation should pass flags to enable AVX2 support, the -DVEC1SMD=SCALAR
# build should not pass any such flags. Then both objects are linked into
# a single *.so file. The result works on hardware that supports AVX2, and 
# also on hardware that does not.
#
# Eventually, when AVX-512 support is added, there will be a third 
# compilation of vec1.c with flags to support AVX-512.
#
vec1multi.so: $(SRC)
	$(CC) -DVEC1SIMD=SCALAR -c $(CFLAGS) $(SRC) -o vec1scalar.o -fPIC
	$(CC) -DVEC1SIMD=AVX2 -c -mavx2 -mfma $(CFLAGS) $(SRC) -o vec1avx2.o -fPIC
	$(CC) vec1scalar.o vec1avx2.o -o vec1multi.so -shared -fPIC 

vec1_faiss.so: vec1_faiss.cpp
	g++ $(CFLAGS) vec1_faiss.cpp -o vec1_faiss.so -shared -fPIC -lfaiss

vec1testscalar.so: $(SRC)
	$(CC) $(CFLAGS) -DVEC1_TEST_SCALAR $(SRC) -o vec1testscalar.so -shared -fPIC -lm

vec1.a: $(SRC)
	$(CC) -DVEC1_STATIC $(CFLAGS) $(SIMDCFLAGS) -c $(SRC) -fPIC
	ar rcs vec1.a vec1.o

vec1_algo_test: tool/vec1_algo_test.c $(SRC)
	$(CC) -I. $(CFLAGS) $(SIMDCFLAGS) tool/vec1_algo_test.c -o vec1_algo_test -lm

clean:
	rm -f vec1.so vec1_faiss.so vec1multi.so vec1testscalar.so
	rm -f vec1.a
	rm -f vec1.o vec1scalar.o vec1avx2.o
	rm -f vec1.so-vec1.gcda vec1.so-vec1.gcno
	rm -f vec1.s


