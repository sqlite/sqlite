import os

def modify_vdbe():
    path = "src/vdbe.c"
    with open(path, "r") as f:
        content = f.read()

    # 1. Add SQLITE_BLOOM_HASH macro and trace counter at the top (after header)
    if '#ifndef SQLITE_BLOOM_HASH' not in content:
        target1 = '#include "vdbeInt.h"'
        replacement1 = '''#include "vdbeInt.h"
#ifndef SQLITE_BLOOM_HASH
# define SQLITE_BLOOM_HASH 0
#endif
static int bloom_trace_count = 0;'''
        content = content.replace(target1, replacement1, 1)

    # 2. Replace filterHash and add helpers
    # We find the start of the comment block for filterHash and the start of the next comment block
    start_marker = "/*\\n** Compute a bloom filter hash"
    end_marker = "/*\\n** For OP_Column"
    
    # Python string literals need careful escapes if they are supposed to match raw newlines in the file
    # But content.find matches raw characters. The file has literal \n as 0x0A.
    
    start_str = "/*\n** Compute a bloom filter hash"
    end_str = "/*\n** For OP_Column"
    
    start_idx = content.find(start_str)
    end_idx = content.find(end_str)
    
    if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
        replacement_helpers = '''/*
** Helpers for the 2-bit same-word Bloom filter experiment.
*/
static u64 bloomMix(u64 h){
  h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
  h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
  h = h ^ (h >> 31);
  return h;
}
static u64 bloomHashBytes(const u8 *z, int n, u64 h){
  while( n-- > 0 ){
    h = (h * 31) + *(z++);
  }
  return h;
}
static u32 bloomGetWord(const u8 *z, int i){
  u32 w;
  memcpy(&w, &z[i*4], 4);
  return w;
}
static void bloomPutWord(u8 *z, int i, u32 w){
  memcpy(&z[i*4], &w, 4);
}

/*
** Compute a bloom filter hash using pOp->p4.i registers from aMem[] beginning
** with pOp->p3.  Return the hash.
*/
static u64 filterHash(const Mem *aMem, const Op *pOp){
  int i, mx;
  u64 h = 0;

  assert( pOp->p4type==P4_INT32 );
  for(i=pOp->p3, mx=i+pOp->p4.i; i<mx; i++){
    const Mem *p = &aMem[i];
    if( p->flags & (MEM_Int|MEM_IntReal) ){
      h += p->u.i;
    }else if( p->flags & MEM_Real ){
      h += sqlite3VdbeIntValue(p);
    }else if( p->flags & (MEM_Str|MEM_Blob) ){
#if SQLITE_BLOOM_HASH == 0
      h += 4093 + (p->flags & (MEM_Str|MEM_Blob));
#elif SQLITE_BLOOM_HASH == 1
      h += bloomMix(4093 + (p->flags & (MEM_Str|MEM_Blob)));
#elif SQLITE_BLOOM_HASH == 2 || SQLITE_BLOOM_HASH == 3
      int n = p->n;
#if SQLITE_BLOOM_HASH == 3
      if( n>64 ) n = 64;
#endif
      h = bloomHashBytes((const u8*)p->z, n, h + 4093);
#endif
    }
  }
#if SQLITE_BLOOM_HASH >= 1
  h = bloomMix(h);
#endif
  return h;
}

'''
        content = content[:start_idx] + replacement_helpers + content[end_idx:]
    else:
        print(f"Failed to find markers: start={start_idx}, end={end_idx}")

    # 3. OP_FilterAdd
    op_add_target = '''  h %= (pIn1->n*8);
  pIn1->z[h/8] |= 1<<(h&7);'''
    op_add_replacement = '''#ifdef SQLITE_BLOOM2
  {
    u32 idx, b1, b2, mask, w;
    int nWord = pIn1->n / 4;
    if( nWord>0 ){
      idx = (u32)((h >> 0) % nWord);
      b1 = (u32)((h >> 32) & 31);
      b2 = (u32)((h >> 37) & 31);
      mask = (1u << b1) | (1u << b2);
      w = bloomGetWord((const u8*)pIn1->z, idx);
      bloomPutWord((u8*)pIn1->z, idx, w | mask);
      if( bloom_trace_count < 10 ){
        fprintf(stderr, "Bloom ADD: h=%llu idx=%u mask=%x w_after=%x nWord=%d hash_variant=%d\\n", h, idx, mask, w|mask, nWord, SQLITE_BLOOM_HASH);
        bloom_trace_count++;
      }
    }
  }
#else
  h %= (pIn1->n*8);
  pIn1->z[h/8] |= 1<<(h&7);
#endif'''
    content = content.replace(op_add_target, op_add_replacement, 1)

    # 4. OP_Filter
    op_filter_target = '''  h %= (pIn1->n*8);
  if( (pIn1->z[h/8] & (1<<(h&7)))==0 ){
    VdbeBranchTaken(1, 2);
    p->aCounter[SQLITE_STMTSTATUS_FILTER_HIT]++;
    goto jump_to_p2;
  }else{
    p->aCounter[SQLITE_STMTSTATUS_FILTER_MISS]++;
    VdbeBranchTaken(0, 2);
  }'''
    op_filter_replacement = '''#ifdef SQLITE_BLOOM2
  {
    u32 idx, b1, b2, mask, w;
    int nWord = pIn1->n / 4;
    if( nWord>0 ){
      idx = (u32)((h >> 0) % nWord);
      b1 = (u32)((h >> 32) & 31);
      b2 = (u32)((h >> 37) & 31);
      mask = (1u << b1) | (1u << b2);
      w = bloomGetWord((const u8*)pIn1->z, idx);
      if( bloom_trace_count < 20 ){
        fprintf(stderr, "Bloom Probe: h=%llu idx=%u mask=%x w=%x nWord=%d match=%d\\n", h, idx, mask, w, nWord, (w&mask)==mask);
        bloom_trace_count++;
      }
      if( (w & mask) != mask ){
        VdbeBranchTaken(1, 2);
        p->aCounter[SQLITE_STMTSTATUS_FILTER_HIT]++;
        goto jump_to_p2;
      }
    }
  }
#else
  h %= (pIn1->n*8);
  if( (pIn1->z[h/8] & (1<<(h&7)))==0 ){
    VdbeBranchTaken(1, 2);
    p->aCounter[SQLITE_STMTSTATUS_FILTER_HIT]++;
    goto jump_to_p2;
  }
#endif
  p->aCounter[SQLITE_STMTSTATUS_FILTER_MISS]++;
  VdbeBranchTaken(0, 2);'''
    content = content.replace(op_filter_target, op_filter_replacement, 1)

    with open(path, "w") as f:
        f.write(content)

def modify_where():
    path = "src/where.c"
    with open(path, "r") as f:
        content = f.read()

    # Force sz = 10,000,000
    target = 'sz = sqlite3LogEstToInt(pTab->nRowLogEst);\n    if( sz<10000 ){'
    # Use exact indentation from where.c view earlier
    # Line 1327:     sz = sqlite3LogEstToInt(pTab->nRowLogEst);
    # Line 1328:     if( sz<10000 ){
    target = '    sz = sqlite3LogEstToInt(pTab->nRowLogEst);\n    if( sz<10000 ){'
    replacement = '    sz = sqlite3LogEstToInt(pTab->nRowLogEst);\n    sz = 10000000; // FORCE LARGE FILTER\n    if( sz<10000 ){'
    if 'FORCE LARGE FILTER' not in content:
        content = content.replace(target, replacement, 1)

    # Word alignment
    target_align = 'sz = (sz + 3) & ~3;'
    if target_align not in content:
        content = content.replace('sqlite3VdbeAddOp2(v, OP_Blob, (int)sz, pLevel->regFilter);',
                                  'sz = (sz + 3) & ~3;\n    sqlite3VdbeAddOp2(v, OP_Blob, (int)sz, pLevel->regFilter);', 1)

    # Relaxation
    target2 = 'const unsigned int reqFlags = (WHERE_SELFCULL|WHERE_COLUMN_EQ);'
    replacement2 = 'const unsigned int reqFlags = WHERE_COLUMN_EQ;'
    content = content.replace(target2, replacement2, 1)

    with open(path, "w") as f:
        f.write(content)

if __name__ == "__main__":
    modify_vdbe()
    modify_where()
    print("Done")
