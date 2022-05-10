#include "mm.h"
#include "memlib.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

team_t team = {"hyeRexx : Explicit",     /* Team name */
               "Hyerin Seok",            /* First member's full name */
               "caco3.rinrin@gmail.com", /* First member's email address */
               "",  /* Second member's full name (leave blank if none) */
               ""}; /* Second member's email address (leave blank if none) */

#define ALIGNMENT 8  // single word (4) or double word (8) alignment
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)  // rounds up to the nearest multiple of ALIGNMENT

/* MACROS */
// [added] Basic constants and macros for manipulationg the free list
#define WSIZE 4                                                         // 헤더, 풋터 사이즈 word
#define DSIZE 8                                                         // 페이로드 사이즈 단위, double word
#define CHUNKSIZE (1 << 12)                                             // 힙 확장 단위
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))                            // 사이즈와 할당 여부 리턴
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)                                     // 블럭 사이즈 리턴
#define GET_ALLOC(p) (GET(p) & 0x1)                                     // 할당 정보 리턴
#define HDRP(bp) ((char *)(bp)-WSIZE)                                   // 헤더 포인터
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)            // 풋터 포인터
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))   // 다음 블럭 포인터
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))     // 이전 블럭 포인터

// [added] explicit only macro
#define SET_ADRS(p, val) (*((void **)(p)) = (void *)(val))  // NEXT/PREV 주소값 저장
#define NXTW(bp) ((char *)(bp))                             // NEXT가 저장된 WORD 리턴
#define PRVW(bp) ((char *)(bp) + WSIZE)                     // PREV가 저장된 WORD 리턴
#define NEXT_FREE(bp) (*((void **)(NXTW(bp))))              // NEXT 가용 영역 리턴
#define PREV_FREE(bp) (*((void **)(PRVW(bp))))              // PREV 가용 영역 리턴



/* Prototype */
static void *extend_heap(size_t words);     // 힙 확장
static void *coalesce(void *bp);            // 앞 뒤의 가용 블럭 확인 후 결합
static void *find_fit(size_t asize);        // 할당 가능한 영역 탐색
static void place(void *bp, size_t asize);  // 가용 영역 헤더-풋터 세팅 및 분할
static void set_root(void *bp);             // 해제 가용 영역 루트로 세팅
static void link_freeblck(void *bp);        // 가용 영역 결합 후 PREV_F, NEXT_F 재처리


/* 힙 초기화 */
static void *heap_listp;  // FREE ROOT ptr
int mm_init(void) {
    if ((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1) {
        return -1;
    }

    PUT(heap_listp, 0);                                 // padding
    PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1));  // prologue header
    SET_ADRS(heap_listp + (2 * WSIZE), NULL);           // prologue nextFREEptr
    SET_ADRS(heap_listp + (3 * WSIZE), NULL);           // prologue prevFREEptr
    PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1));  // prologue footer
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));          // epilogue header
    heap_listp += (2 * WSIZE);                          // the first bp = prologue nextFREEptr

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    return 0;
}

/* 메모리 할당 */
void *mm_malloc(size_t size) {
    size_t asize;       // 실제 할당할 사이즈
    size_t extendsize;  // 확장 사이즈
    void *bp;           // 반환할 블럭 위치

    // 무효 요청 필터
    if (size == 0) {    
        return NULL;
    }

    // alignment 적용한 할당 사이즈 계산 (DSIZE 기준 구분, MIN = 2 * DSIZE)
    if (size <= DSIZE) {
        asize = 2 * DSIZE;
    } else {
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    // 할당 가능한 영역 확인 : find_fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 할당 가능한 영역 없을 시 힙 확장
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/* 할당 블럭 회수 */
void mm_free(void *bp) { 
  size_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));  // 헤더 갱신 : 사이즈 | free
  PUT(FTRP(bp), PACK(size, 0));  // 풋터 갱신 : 사이즈 | free

  coalesce(bp); // 인접한 가용 영역 확인
}

/* 재할당 */
void *mm_realloc(void *bp, size_t size) {
    void *oldbp = bp;
    void *newbp;
    size_t copySize;

    newbp = mm_malloc(size);
    if (newbp == NULL) {
        return NULL;
    }

    copySize = GET_SIZE(HDRP(oldbp)) - DSIZE;

    if (size < copySize) {
        copySize = size;
    }
    
    memcpy(newbp, oldbp, copySize);
    mm_free(oldbp);
    return newbp;
}

/* 힙 확장 */
static void *extend_heap(size_t words) {
    void *bp;
    size_t size;

    // 실제 할당에 필요한 사이즈 계산 및 할당 (alignment)
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    // 프롤로그, 에필로그 세팅
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    // 인접 가용 영역 확인
    return coalesce(bp);
}

/* 인접한 가용 영역 결합 */
static void *coalesce(void *bp) {
    // 이전, 이후 블록 할당 상태 조사
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // Case 1 : prev, next 모두 할당되어 있음
    if (prev_alloc && next_alloc) {
        set_root(bp);
        return bp;
    }

    // Case 2 : prev 할당, next 가용
    else if (prev_alloc && !next_alloc) {
        link_freeblck(NEXT_BLKP(bp));               // 다음 블럭의 NEXT 가져옴
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));      // 현재 블록 사이즈와 다음 블록 사이즈 합 계산
        PUT(HDRP(bp), PACK(size, 0));               // 헤더 새로 할당한 사이즈 정보로 업데이트
        PUT(FTRP(bp), PACK(size, 0));               // 풋터 새로 할당한 사이즈 정보로 업데이트
        set_root(bp);                               // 루트 세팅
    }

    // Case 3 : prev 가용, next 할당
    else if (!prev_alloc && next_alloc) {
        link_freeblck(PREV_BLKP(bp));               // 이전 블럭의 PREV 가져옴
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));      
        PUT(FTRP(bp), PACK(size, 0));               
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));    
        bp = PREV_BLKP(bp);                         // bp 이전 블록 사이즈 위치로 이동
        set_root(bp);                               
    }

    // Case 4 : prev, next 모두 가용
    else {
        link_freeblck(PREV_BLKP(bp));               // 이전 블럭의 PREV 가져옴
        link_freeblck(NEXT_BLKP(bp));               // 이전 블럭의 NEXT 가져옴
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));    
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));  
        bp = PREV_BLKP(bp);
        set_root(bp);
    }
    return bp;  // 새로운 bp 리턴
}

// 할당 가능 블록 탐색
static void *find_fit(size_t asize) {
    void *bp;

    // asize 이상의 블록을 찾을 때까지 NEXT FREE로 이동
    for (bp = NEXT_FREE(heap_listp); bp != NULL; bp = NEXT_FREE(bp)) {
        if (GET_SIZE(HDRP(bp)) >= asize) {
            return bp;
        }
    }
    return NULL; /* No fit */
}

// 가용 영역 헤더, 풋터 초기화 + 분할 수행
static void place(void *bp, size_t asize) {
    size_t block_size = GET_SIZE(HDRP(bp));

    // 분할 가능한 경우 (분할해도 최소 사이즈 이상이 남을 수 있음)
    if (block_size >= asize + (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));  // 헤더 갱신 : 사이즈 | 할당 처리
        PUT(FTRP(bp), PACK(asize, 1));  // 풋터 갱신 : 사이즈 | 할당 처리

        void *next = NEXT_FREE(bp);     // original NEXT FREE 보관
        void *prev = PREV_FREE(bp);     // original PREV FREE 보관

        bp = NEXT_BLKP(bp);                         // 분할한 블록으로 bp 갱신 (blocksize - asize)
        PUT(HDRP(bp), PACK(block_size - asize, 0)); // 분할 블록 헤더 갱신
        PUT(FTRP(bp), PACK(block_size - asize, 0)); // 분할 블록 풋터 갱신

        SET_ADRS(NXTW(bp), next);   // 분할 블록 NEXT set : 기존 next
        SET_ADRS(NXTW(prev), bp);   // 분할 블록 이전 FREE NEXT set : 이 블록(bp)
        SET_ADRS(PRVW(bp), prev);   // 분할 블록 PREV set : 기존 prev

        if (next != NULL) {             // NULL 역참조 방지
            SET_ADRS(PRVW(next), bp);   // 분할 블록 다음 FREE PREV set : 이 블록(bp)
        }
    }

    // 분할하면 최소 사이즈 미만이 남을 경우
    else {
        link_freeblck(bp);                   // 이 블럭을 FREE 리스트와 연결
        PUT(HDRP(bp), PACK(block_size, 1));  // 헤더 갱신 : 사이즈 | 할당 처리
        PUT(FTRP(bp), PACK(block_size, 1));  // 풋터 갱신 : 사이즈 | 할당 처리
    }
}

static void set_root(void *bp) {
    SET_ADRS(NXTW(bp), NEXT_FREE(heap_listp));      // bp NEXT = root NEXT
    SET_ADRS(PRVW(bp), heap_listp);                 // bp PREV = root

    if (NEXT_FREE(heap_listp) != NULL) {            // 리스트에 이미 가용 블록이 있는 경우
        SET_ADRS(PRVW(NEXT_FREE(heap_listp)), bp);  // 기존 루트의 PREV = bp
    }
    SET_ADRS(NXTW(heap_listp), bp);                 // root NEXT = bp

    return;
}

static void link_freeblck(void *bp) {
  void *prev = PREV_FREE(bp);    // bp의 PREV 가용 블록 저장
  void *next = NEXT_FREE(bp);    // bp의 NEXT 가용 블록 저장

  SET_ADRS(NXTW(prev), next);    // 기존 앞쪽 가용 블록의 NEXT 세팅 : bp의 NEXT
  if (next != NULL) {            // bp에 NEXT 가용 블록이 있는 경우
    SET_ADRS(PRVW(next), prev);  // 기존 뒤쪽 가용 블록의 PREV 세팅 : bp의 prev
  }
  return;
}