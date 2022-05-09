#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"


team_t team = {
    "hyeRexx : Explicit",    /* Team name */
    "Hyerin Seok",/* First member's full name */
    "caco3.rinrin@gmail.com",/* First member's email address */
    "", /* Second member's full name (leave blank if none) */
    ""};/* Second member's email address (leave blank if none) */


#define ALIGNMENT 8 // single word (4) or double word (8) alignment
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // rounds up to the nearest multiple of ALIGNMENT
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


// [added] Basic constants and macros for manipulationg the free list

#define WSIZE 4                                                         // 헤더, 풋터 사이즈 word
#define DSIZE 8                                                         // 페이로드 사이즈 단위, double word
#define CHUNKSIZE (1<<12)                                               // 힙 확장 단위 
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))                            // 사이즈와 할당 여부 리턴
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)                                     // 사이즈+할당 정보에서 사이즈만 남김
#define GET_ALLOC(p) (GET(p) & 0x1)                                     // 사이즈+할당 정보에서 할당 정보만 남김
#define HDRP(bp) ((char *)(bp)-WSIZE)                                   // 헤더 포인터
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)            // 풋터 포인터
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))   // 다음 블럭 포인터
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp)-DSIZE)))   // 이전 블럭 포인터

// [added] explicit macro
#define ROOT (*(void **)(heap_listp))
#define GET_ROOT (GET(ROOT))

#define GET_P_PTR(bp) ((void *)(bp) + WSIZE)           // *next에서 *prev로 이동
#define NEXT_FREE(bp) (*(void **)(bp))                 // *NEXT에 담긴 주소값이 가리키는 값으로 이동 : next free bp
#define PREV_FREE(bp) (*(void **)(GET_P_BLKP(bp)))     // *PREV에 담긴 주소값이 가리키는 값으로 이동 : prev free bp
#define SET_ROOT(bp) (PUT(ROOT, bp))                   // ROOT를 bp로 설정함
#define SET_PREV(bp, val) (PUT(GET_P_PTR(bp), val))    // PREV를 val로 설정함
#define SET_NEXT(bp, val) (PUT(bp, val))               // NEXT를 val로 설정함

// prototype
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static void *heap_listp;  // 프롤로그 블럭 포인터

int mm_init(void) { 
    if((heap_listp = mem_sbrk(6*WSIZE)) == (void *)-1) { // 패딩 1, 헤더 1, 페이로드 2, 풋터 1, 에필로그 1
        return -1;
    }

    PUT(heap_listp, 0);                            // 미사용 패딩 블럭
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더 (자기 블럭의 전체 용량을 말함 ~PACK)
    PUT(heap_listp + (2 * WSIZE), 0);              // 프롤로그 페이로드 : PREV PTR
    PUT(heap_listp + (3 * WSIZE), 0);              // 프롤로그 페이로드 : NEXT PTR
    PUT(heap_listp + (4 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 풋터 (자기 블럭의 전체 용량을 말함 ~PACK)
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));     // 에필로그 블럭
    heap_listp += (2 * WSIZE);                     // heap_listp == bp == NEXT로 초기화

    if(extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }

    return 0;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    // 헤더, 풋터 세팅
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    // PREV FREE, NEXT FREE 처리
    SET_NEXT(bp, GET_ROOT); // bp의 NEXT로 기존 ROOT 세팅
    SET_PREV(GET_P_PTR(NEXT_FREE(bp)), GET_ROOT); // 기존 ROOT의 PREV를 지금 ROOT로 변경
    SET_ROOT(bp); // bp를 ROOT로 잡음

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));  // 에필로그 처리

    return coalesce(bp);
}

// [added]
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 할당 상태 조사
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록의 할당 상태 조사 
    size_t size = GET_SIZE(HDRP(bp)); // 매개변수로 받은 bp 헤더포인트의 블록 사이즈 조사

    if (prev_alloc && next_alloc) { // Case 1 : prev next 모두 할당되어 있음
        return bp;
    } 
    
    else if (prev_alloc && !next_alloc) {   // Case 2 : prev 할당, next 가용
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 현재 블록의 사이즈와 다음 블록 사이즈 더하기
        PUT(HDRP(bp), PACK(size, 0));   // 헤더 새로 할당한 사이즈 정보로 업데이트
        PUT(FTRP(bp), PACK(size, 0));   // 풋터 새로 할당한 사이즈 정보로 업데이트

        // prev 유지, next 결합한 뒷블럭의 next로 교체
        SET_NEXT(bp, NEXT_FREE(NEXT_BLKP(bp)));    // 이 블럭의 NEXT를 더한 블록의 NEXT로 교체
    }

    else if (!prev_alloc && next_alloc) {   // Case 3: prev 가용, next 할당
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

        // 이전 블럭 next 수정, prev는 유지해도 오케이
        SET_NEXT(PREV_BLKP(bp), NEXT_FREE(bp)); // 이전 블럭의 next를 이 블럭의 next로 잡음
        bp = PREV_BLKP(bp);        
    }

    else {
      size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
      PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
      PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));  // 이쪽은 아직 bp를 못받았기 때문에 내 위치 기준으로 풋터를 찾아가야 함
      bp = PREV_BLKP(bp);
    }

    return bp; // 여기서 새로운 bp를 리턴
  }

// [added] 
static void *find_fit(size_t asize) {
  void *fit_p = heap_listp;  // 힙 리스트 포인터 위치 복사

  while (1) {
    size_t size = GET_SIZE(HDRP(fit_p));  // 해당 블럭 사이즈 및 할당 여부 검사

    if (size <= 0) {
      return NULL;
    }

    size_t allocated = GET_ALLOC(HDRP(fit_p));

    if (!allocated && size >= asize) {  // 사이즈 가능, free 상태인 경우
      return fit_p;                     // 해당 블럭 포인터 리턴
    }
    fit_p = NEXT_BLKP(fit_p);  // 다음 블럭으로 탐색 위치 이동
  }
} //1점 차이 남 ㅋㅋㅋㅋㅋ 물론 내가 만든게 1점 낮음


static void *find_fit(size_t asize) {
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
  return NULL; /* No fit */
}

//[added]
static void place(void *bp, size_t asize) {
    size_t block_size = GET_SIZE(HDRP(bp));
    if((block_size - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(block_size - asize, 0));
        PUT(FTRP(bp), PACK(block_size - asize, 0));
    }
    else {
        PUT(HDRP(bp), PACK(block_size, 1));
        PUT(FTRP(bp), PACK(block_size, 1));
    }
}

// Allocate memory
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if(size == 0) { // 할당 요청 사이즈가 0일 때
        return NULL;
    }
    
    if(size <= DSIZE) { // 할당 요청 사이즈가 더블워드 이하
        asize = 2 * DSIZE; // 최소 16바이트 할당됨. 기본 8(size 1), 헤더풋터 8
    }
    else { // 할당 요청 사이즈가 더블워드 이상 (size / DSIZE) * DSIZE + DSIZE(여분) + DSIZE(헤더풋터)
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    if((bp = find_fit(asize)) != NULL) {
        place(bp, asize);  // 아직 구현 안됨
        return bp;
    }

    // 요청한 용량 들어갈 자리를 찾지 못했을 때, 힙 추가 요청
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}


/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp) { //매개변수명 ptr에서 bp로 수정함
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0)); // 헤더에 PACK을 반영, 이 블럭을 free 상태로 수정
    PUT(FTRP(bp), PACK(size, 0)); // 풋터에 PACK을 반영, 이 블럭을 free 상태로 수정

    SET_NEXT(bp, GET_ROOT);  // bp의 NEXT로 기존 ROOT 세팅
    SET_PREV(GET_P_PTR(NEXT_FREE(bp)), GET_ROOT);  // 기존 ROOT의 PREV를 지금 ROOT로 변경
    SET_ROOT(bp);        // bp를 ROOT로 잡음

    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  newptr = mm_malloc(size);
  if (newptr == NULL) return NULL;
  copySize = *(size_t *)((char *)oldptr - WSIZE);
  if (size < copySize) copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}
