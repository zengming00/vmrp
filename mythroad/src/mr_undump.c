/*
** $Id: lundump.c,v 1.49 2003/04/07 20:34:20 lhf Exp $
** load pre-compiled Lua chunks
** See Copyright Notice in lua.h
*/

#define mr_undump_c


#include "./h/mr_undump.h"
#include "./h/mr_debug.h"
#include "./h/mr_func.h"
#include "./h/mr_mem.h"
#include "./h/mr_opcodes.h"
#include "./h/mr_string.h"
#include "./h/mr_zio.h"

#define LoadByte (lu_byte) ezgetc

typedef struct {
    mrp_State* L;
    ZIO* Z;
    Mbuffer* b;
    int swap;
    const char* name;
} LoadState;

static void unexpectedEOZ(LoadState* S) {
    mr_G_runerror(S->L, "err:1001 in %s", S->name);  // unexpected end of file in %s
}

static int ezgetc(LoadState* S) {
    int c = zgetc(S->Z);
    if (c == EOZ) unexpectedEOZ(S);
    return c;
}

static void ezread(LoadState* S, void* b, int n) {
    int r = mr_Z_read(S->Z, b, n);
    if (r != 0) unexpectedEOZ(S);
}

static void LoadBlock(LoadState* S, void* b, size_t size) {
    if (S->swap) {
        char* p = (char*)b + size - 1;
        int n = size;
        while (n--) *p-- = (char)ezgetc(S);
    } else
        ezread(S, b, size);
}

static void LoadVector(LoadState* S, void* b, int m, size_t size) {
    if (S->swap) {
        char* q = (char*)b;
        while (m--) {
            char* p = q + size - 1;
            int n = size;
            while (n--) *p-- = (char)ezgetc(S);
            q += size;
        }
    } else
        ezread(S, b, m * size);
}

static int LoadInt(LoadState* S) {
    int x;
    LoadBlock(S, &x, sizeof(x));
    if (x < 0) mr_G_runerror(S->L, "err:1000 in XX", S->name);  // bad integer in %s
    return x;
}

static size_t LoadSize(LoadState* S) {
    size_t x;
    LoadBlock(S, &x, sizeof(x));
    return x;
}

static mrp_Number LoadNumber(LoadState* S) {
    mrp_Number x;
    LoadBlock(S, &x, sizeof(x));
    return x;
}

static TString* LoadString(LoadState* S) {
    size_t size = LoadSize(S);
    if (size == 0)
        return NULL;
    else {
        char* s = mr_Z_openspace(S->L, S->b, size);
        ezread(S, s, size);
        return mr_S_newlstr(S->L, s, size - 1); /* remove trailing '\0' */
    }
}

static void LoadCode(LoadState* S, Proto* f) {
    int size = LoadInt(S);
    f->code = mr_M_newvector(S->L, size, Instruction);
    f->sizecode = size;
    LoadVector(S, f->code, size, sizeof(*f->code));
}

static void LoadLocals(LoadState* S, Proto* f) {
    int i, n;
    n = LoadInt(S);
    f->locvars = mr_M_newvector(S->L, n, LocVar);
    f->sizelocvars = n;
    for (i = 0; i < n; i++) {
        f->locvars[i].varname = LoadString(S);
        f->locvars[i].startpc = LoadInt(S);
        f->locvars[i].endpc = LoadInt(S);
    }
}

static void LoadLines(LoadState* S, Proto* f) {
    int size = LoadInt(S);
    f->lineinfo = mr_M_newvector(S->L, size, int);
    f->sizelineinfo = size;
    LoadVector(S, f->lineinfo, size, sizeof(*f->lineinfo));
}

static void LoadUpvalues(LoadState* S, Proto* f) {
    int i, n;
    n = LoadInt(S);
    if (n != 0 && n != f->nups)
        mr_G_runerror(S->L, "err:1002 in %s:%d:%d",
                      S->name, n, f->nups);  //bad nupvalues in %s: read %d; expected %d
    f->upvalues = mr_M_newvector(S->L, n, TString*);
    f->sizeupvalues = n;
    for (i = 0; i < n; i++) f->upvalues[i] = LoadString(S);
}

static Proto* LoadFunction(LoadState* S, TString* p);

static void LoadConstants(LoadState* S, Proto* f) {
    int i, n;
    n = LoadInt(S);
    f->k = mr_M_newvector(S->L, n, TObject);
    f->sizek = n;
    for (i = 0; i < n; i++) {
        TObject* o = &f->k[i];
        int t = LoadByte(S);
        switch (t) {
            case MRP_TNUMBER:
                setnvalue(o, LoadNumber(S));
                break;
            case MRP_TSTRING:
                setsvalue2n(o, LoadString(S));
                break;
            case MRP_TNIL:
                setnilvalue(o);
                break;
            default:
                mr_G_runerror(S->L, "err:1003 in %s:%d", t, S->name);  //bad constant type (%d) in %s
                break;
        }
    }
    n = LoadInt(S);
    f->p = mr_M_newvector(S->L, n, Proto*);
    f->sizep = n;
    for (i = 0; i < n; i++) f->p[i] = LoadFunction(S, f->source);
}

static Proto* LoadFunction(LoadState* S, TString* p) {
    Proto* f = mr_F_newproto(S->L);
    f->source = LoadString(S);
    if (f->source == NULL) f->source = p;
    f->lineDefined = LoadInt(S);
    f->nups = LoadByte(S);
    f->numparams = LoadByte(S);
    f->is_vararg = LoadByte(S);
    f->maxstacksize = LoadByte(S);
    LoadLines(S, f);
    LoadLocals(S, f);
    LoadUpvalues(S, f);
    LoadConstants(S, f);
    LoadCode(S, f);
#ifndef TRUST_BINARIES
    if (!mr_G_checkcode(f)) mr_G_runerror(S->L, "err:1004 in %s", S->name);  //bad code in %s
#endif
    return f;
}

static void LoadSignature(LoadState* S) {
    const char* s = MRP_SIGNATURE;
    //#ifdef COMPATIBILITY01
    //   const char* s1=MRP_SIGNATURE;
    //   char tempch;
    //   while (*s!=0 && ((((tempch=ezgetc(S))==*s))||(tempch==*s1)))
    //   {
    //      ++s;
    //      ++s1;
    //   }
    //#else
    while (*s != 0 && ezgetc(S) == *s)
        ++s;
    //#endif
    if (*s != 0) mr_G_runerror(S->L, "err:1005 in %s", S->name);  //bad signature in %s
}

static void TestSize(LoadState* S, int s, const char* what) {
    int r = LoadByte(S);
    if (r != s)
        mr_G_runerror(S->L, "err:1006 in %s:%s:%d:%d", S->name, what, s, r);
    //mr_G_runerror(S->L,"virtual machine mismatch in %s: "
    //	"size of %s is %d but read %d",S->name,what,s,r);
}

#define TESTSIZE(s, w) TestSize(S, s, w)
#define V(v) v / 16, v % 16

static void LoadHeader(LoadState* S) {
    int version;
    mrp_Number x, tx = TEST_NUMBER;
    LoadSignature(S);
    version = LoadByte(S);
    if (version > VERSION)
        mr_G_runerror(S->L, "err:1007 in %s:%d:%d:%d:%d",
                      //mr_G_runerror(S->L,"%s's ver too new: meet version %d.%d; %d.%d expected",
                      S->name, V(version), V(VERSION));
    if (version < VERSION_50) /* check last major change */
        mr_G_runerror(S->L, "err:1007 in %s:%d:%d:%d:%d",
                      //mr_G_runerror(S->L,"%s's ver too old: "
                      //	"meet version %d.%d; %d.%d expected ",
                      S->name, V(version), V(VERSION_50));
    S->swap = (mr_U_endianness() != LoadByte(S)); /* need to swap bytes? */
    if (version > VERSION_50) {
    } else {
        TESTSIZE(sizeof(int), "int");
        TESTSIZE(sizeof(size_t), "size_t");
        TESTSIZE(sizeof(Instruction), "Instruction");
        TESTSIZE(SIZE_OP, "OP");
        TESTSIZE(SIZE_A, "A");
        TESTSIZE(SIZE_B, "B");
        TESTSIZE(SIZE_C, "C");
        TESTSIZE(sizeof(mrp_Number), "number");
        x = LoadNumber(S);
        if ((long)x != (long)tx)                             /* disregard errors in last bits of fraction */
            mr_G_runerror(S->L, "err:1008 in %s", S->name);  //unknown number format in %s
    }
}

static Proto* LoadChunk(LoadState* S) {
    LoadHeader(S);
    return LoadFunction(S, NULL);
}

/*
** load precompiled chunk
*/
Proto* mr_U_undump(mrp_State* L, ZIO* Z, Mbuffer* buff) {
    LoadState S;
    const char* s = zname(Z);
    if (*s == '$')
        S.name = s + 1;
    else if (*s == MRP_SIGNATURE[0])
        S.name = "buffer";
    else
        S.name = s;
    S.L = L;
    S.Z = Z;
    S.b = buff;
    return LoadChunk(&S);
}

/*
** find byte order
*/
int mr_U_endianness(void) {
    int x = 1;
    return *(char*)&x;
}
